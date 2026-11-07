// Usage: Lv2Compare <VST3 .so> <LV2 .so> [scenario name contains]
//
// Plays the same notes and control changes through a plug-in's VST3 and its LV2, and compares their output bit
// for bit. Both run the same processor, so any difference is the LV2 wrapper's fault. Linux output is only ever
// compared with Linux output here.
//
// A control port holds a float, so the VST3 side gets the normalized value of the same float. "Float steps"
// below measures what that costs against a VST3 given unrounded values. It is reported, and can't fail.
//
// It also checks that run () doesn't allocate, and that a wrong note time and a wrong control value would be
// noticed, so that a pass means something.

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/urid/urid.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;

DEF_CLASS_IID (Vst::IEditController)
DEF_CLASS_IID (Vst::IComponent)
DEF_CLASS_IID (Vst::IAudioProcessor)
DEF_CLASS_IID (Vst::IHostApplication)

// Counting allocations here catches those inside the plug-in too, because this program exports these functions
// and the plug-in looks them up by name.
extern "C" void* __libc_malloc (size_t);
extern "C" void* __libc_calloc (size_t, size_t);
extern "C" void* __libc_realloc (void*, size_t);

namespace {
long gAllocations = 0;
}

extern "C" void* malloc (size_t size)
{
    ++gAllocations;
    return __libc_malloc (size);
}

extern "C" void* calloc (size_t count, size_t size)
{
    ++gAllocations;
    return __libc_calloc (count, size);
}

extern "C" void* realloc (void* memory, size_t size)
{
    ++gAllocations;
    return __libc_realloc (memory, size);
}

namespace {

constexpr int kMaxBlock = 4096;
constexpr double kPi = 3.14159265358979;

// These match src/lv2/RabidPhatt.ttl.
constexpr uint32_t kPortLeft = 0;
constexpr uint32_t kPortRight = 1;
constexpr uint32_t kPortEvents = 2;
constexpr uint32_t kPortFirstControl = 3;

constexpr Vst::ParamID kVolumeControl = 13; // kVolume in src/Parameters.h.

IPluginFactory* gFactory = nullptr;
TUID gComponentId;
TUID gControllerId;
const LV2_Descriptor* gDescriptor = nullptr;

#define LV2COMPARE_NO_REFCOUNT                                                                \
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }  \
    uint32 PLUGIN_API addRef () override { return 1; }                                        \
    uint32 PLUGIN_API release () override { return 1; }

class Host : public Vst::IHostApplication
{
public:
    LV2COMPARE_NO_REFCOUNT

    tresult PLUGIN_API getName (Vst::String128 name) override
    {
        name[0] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance (TUID, TUID, void** object) override
    {
        *object = nullptr;
        return kResultFalse;
    }
};

Host gHost;

class ValueQueue : public Vst::IParamValueQueue
{
public:
    LV2COMPARE_NO_REFCOUNT

    explicit ValueQueue (Vst::ParamID id) : _id (id) {}

    void add (int32 offset, double value) { _points.push_back ({ offset, value }); }

    Vst::ParamID PLUGIN_API getParameterId () override { return _id; }
    int32 PLUGIN_API getPointCount () override { return static_cast<int32> (_points.size ()); }

    tresult PLUGIN_API getPoint (int32 index, int32& offset, Vst::ParamValue& value) override
    {
        if (index < 0 || index >= getPointCount ())
            return kResultFalse;
        offset = _points[static_cast<size_t> (index)].first;
        value = _points[static_cast<size_t> (index)].second;
        return kResultTrue;
    }

    tresult PLUGIN_API addPoint (int32, Vst::ParamValue, int32&) override { return kResultFalse; }

private:
    Vst::ParamID _id;
    std::vector<std::pair<int32, double>> _points;
};

class ParameterChanges : public Vst::IParameterChanges
{
public:
    LV2COMPARE_NO_REFCOUNT

    int32 PLUGIN_API getParameterCount () override { return static_cast<int32> (queues.size ()); }

    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 index) override
    {
        return index >= 0 && index < getParameterCount () ? &queues[static_cast<size_t> (index)] : nullptr;
    }

    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID&, int32&) override { return nullptr; }

    std::vector<ValueQueue> queues;
};

class EventList : public Vst::IEventList
{
public:
    LV2COMPARE_NO_REFCOUNT

    int32 PLUGIN_API getEventCount () override { return static_cast<int32> (events.size ()); }

    tresult PLUGIN_API getEvent (int32 index, Vst::Event& event) override
    {
        if (index < 0 || index >= getEventCount ())
            return kResultFalse;
        event = events[static_cast<size_t> (index)];
        return kResultOk;
    }

    tresult PLUGIN_API addEvent (Vst::Event&) override { return kResultFalse; }

    std::vector<Vst::Event> events;
};

struct Note
{
    size_t position;
    uint8_t status; // MIDI's, with the channel.
    uint8_t pitch;
    uint8_t velocity;
};

// Junk where a plug-in should have written, so that output it leaves alone can't match by luck.
constexpr float kUnwritten = -12345.0f;

class Driver
{
public:
    virtual ~Driver () = default;

    // A control's final value for the next block, in plain units, as an LV2 port holds it.
    virtual void setControl (Vst::ParamID id, float plain) = 0;
    virtual void render (int count, const std::vector<Note>& notes, float* left, float* right) = 0;
    virtual void reactivate () = 0;
};

// Controller used only for its conversions, so that the wrapper's own are checked against it.
class Vst3Driver : public Driver
{
public:
    explicit Vst3Driver (double sampleRate)
    {
        gFactory->createInstance (gComponentId, Vst::IComponent::iid, reinterpret_cast<void**> (&_component));
        _component->initialize (&gHost);
        _component->queryInterface (Vst::IAudioProcessor::iid, reinterpret_cast<void**> (&_processor));
        gFactory->createInstance (gControllerId, Vst::IEditController::iid, reinterpret_cast<void**> (&controller));
        controller->initialize (&gHost);

        Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, kMaxBlock, sampleRate };
        _processor->setupProcessing (setup);
        _component->activateBus (Vst::kAudio, Vst::kOutput, 0, true);
        _component->activateBus (Vst::kEvent, Vst::kInput, 0, true);
        _component->setActive (true);
        _processor->setProcessing (true);
    }

    ~Vst3Driver () override
    {
        _processor->setProcessing (false);
        _component->setActive (false);
        controller->terminate ();
        controller->release ();
        _processor->release ();
        _component->terminate ();
        _component->release ();
    }

    void setControl (Vst::ParamID id, float plain) override { setNormalized (id, normalizedForControl (id, plain)); }

    void setNormalized (Vst::ParamID id, double normalized)
    {
        ValueQueue queue (id);
        queue.add (0, normalized);
        _changes.queues.push_back (queue);
    }

    // Only the last point counts, as a control port's one value per block assumes.
    void setRamp (Vst::ParamID id, double first, double last)
    {
        ValueQueue queue (id);
        queue.add (0, first);
        queue.add (kMaxBlock - 1, last);
        _changes.queues.push_back (queue);
    }

    double normalizedForControl (Vst::ParamID id, float plain)
    {
        if (plain <= static_cast<float> (controller->normalizedParamToPlain (id, 0.0)))
            return 0.0;
        if (plain >= static_cast<float> (controller->normalizedParamToPlain (id, 1.0)))
            return 1.0;
        return controller->plainParamToNormalized (id, plain);
    }

    void render (int count, const std::vector<Note>& notes, float* left, float* right) override
    {
        _events.events.clear ();
        for (const auto& note : notes)
        {
            Vst::Event event {};
            event.sampleOffset = static_cast<int32> (note.position);
            const float velocity = static_cast<float> (note.velocity) / 127.0f;
            if ((note.status & 0xF0) == 0x90)
            {
                event.type = Vst::Event::kNoteOnEvent;
                event.noteOn.channel = note.status & 0x0F;
                event.noteOn.pitch = note.pitch;
                event.noteOn.velocity = velocity;
                event.noteOn.noteId = -1;
            }
            else if ((note.status & 0xF0) == 0x80)
            {
                event.type = Vst::Event::kNoteOffEvent;
                event.noteOff.channel = note.status & 0x0F;
                event.noteOff.pitch = note.pitch;
                event.noteOff.velocity = velocity;
                event.noteOff.noteId = -1;
            }
            else
            {
                continue; // A VST3 host doesn't send other messages as events.
            }
            _events.events.push_back (event);
        }

        float* outputs[2] = { left, right };
        Vst::AudioBusBuffers output {};
        output.numChannels = 2;
        output.channelBuffers32 = outputs;

        Vst::ProcessData data {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = count;
        data.numOutputs = 1;
        data.outputs = &output;
        data.inputParameterChanges = _changes.queues.empty () ? nullptr : &_changes;
        data.inputEvents = &_events;
        _processor->process (data);
        _changes.queues.clear ();
    }

    void reactivate () override
    {
        _processor->setProcessing (false);
        _component->setActive (false);
        _component->setActive (true);
        _processor->setProcessing (true);
    }

    Vst::IEditController* controller { nullptr };

private:
    Vst::IComponent* _component { nullptr };
    Vst::IAudioProcessor* _processor { nullptr };
    ParameterChanges _changes;
    EventList _events;
};

// As little as a host can get away with.
class Urids
{
public:
    Urids ()
    {
        _map.handle = this;
        _map.map = [] (LV2_URID_Map_Handle handle, const char* uri) -> LV2_URID {
            auto& known = static_cast<Urids*> (handle)->_known;
            const auto found = known.find (uri);
            if (found != known.end ())
                return found->second;
            const auto urid = static_cast<LV2_URID> (known.size () + 1);
            known[uri] = urid;
            return urid;
        };
    }

    LV2_URID_Map* map () { return &_map; }
    LV2_URID get (const char* uri) { return _map.map (_map.handle, uri); }

private:
    LV2_URID_Map _map {};
    std::map<std::string, LV2_URID> _known;
};

Urids gUrids;

class Lv2Driver : public Driver
{
public:
    explicit Lv2Driver (double sampleRate)
    {
        _maxBlock = kMaxBlock;
        const LV2_Options_Option options[] = {
            { LV2_OPTIONS_INSTANCE, 0, gUrids.get (LV2_BUF_SIZE__maxBlockLength), sizeof (int32_t), gUrids.get (LV2_ATOM__Int), &_maxBlock },
            { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr },
        };
        const LV2_Feature mapFeature { LV2_URID__map, gUrids.map () };
        const LV2_Feature optionsFeature { LV2_OPTIONS__options, const_cast<LV2_Options_Option*> (options) };
        const LV2_Feature boundedFeature { LV2_BUF_SIZE__boundedBlockLength, nullptr };
        const LV2_Feature* const features[] = { &mapFeature, &optionsFeature, &boundedFeature, nullptr };

        _instance = gDescriptor->instantiate (gDescriptor, sampleRate, "", features);
        if (!_instance)
            return;

        // The port's values are what a host would load from the Turtle: Vst3Driver reads the same defaults.
        Vst3Driver reference (sampleRate);
        _controls.resize (static_cast<size_t> (reference.controller->getParameterCount ()));
        for (int32 i = 0; i < reference.controller->getParameterCount (); ++i)
        {
            Vst::ParameterInfo info {};
            reference.controller->getParameterInfo (i, info);
            _controls[info.id] = static_cast<float> (reference.controller->normalizedParamToPlain (info.id, info.defaultNormalizedValue));
            gDescriptor->connect_port (_instance, kPortFirstControl + info.id, &_controls[info.id]);
        }

        _eventBuffer.resize (64 * 1024);
        gDescriptor->connect_port (_instance, kPortEvents, _eventBuffer.data ());
        gDescriptor->activate (_instance);
    }

    ~Lv2Driver () override
    {
        if (!_instance)
            return;
        gDescriptor->deactivate (_instance);
        gDescriptor->cleanup (_instance);
    }

    bool isValid () const { return _instance != nullptr; }

    void setControl (Vst::ParamID id, float plain) override { _controls[id] = plain; }

    void render (int count, const std::vector<Note>& notes, float* left, float* right) override
    {
        // Ports can move between blocks, so they get connected every time, as some hosts do.
        gDescriptor->connect_port (_instance, kPortLeft, left);
        gDescriptor->connect_port (_instance, kPortRight, right);

        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*> (_eventBuffer.data ());
        const auto capacity = static_cast<uint32_t> (_eventBuffer.size () - sizeof (LV2_Atom));
        sequence->atom.type = gUrids.get (LV2_ATOM__Sequence);
        lv2_atom_sequence_clear (sequence);
        const LV2_URID midiEvent = gUrids.get (LV2_MIDI__MidiEvent);
        for (const auto& note : notes)
        {
            struct
            {
                LV2_Atom_Event header;
                uint8_t message[3];
            } event {};
            event.header.time.frames = static_cast<int64_t> (note.position);
            event.header.body.type = midiEvent;
            event.header.body.size = 3;
            event.message[0] = note.status;
            event.message[1] = note.pitch;
            event.message[2] = note.velocity;
            lv2_atom_sequence_append_event (sequence, capacity, &event.header);
        }

        const long before = gAllocations;
        gDescriptor->run (_instance, static_cast<uint32_t> (count));
        allocationsInRun += gAllocations - before;
    }

    void reactivate () override
    {
        gDescriptor->deactivate (_instance);
        gDescriptor->activate (_instance);
    }

    long allocationsInRun { 0 };

private:
    LV2_Handle _instance { nullptr };
    int32_t _maxBlock { 0 };
    std::vector<float> _controls;
    std::vector<uint8_t> _eventBuffer;
};

class Random
{
public:
    explicit Random (uint32_t seed) : _state (seed * 2654435761u + 0x9E3779B9u) {}

    uint32_t next ()
    {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        return _state;
    }

    double unit () { return next () / 4294967296.0; }

private:
    uint32_t _state;
};

// Sorted by position, as an LV2 event port has to be.
std::vector<Note> makeNotes (uint32_t seed, size_t count, double sampleRate)
{
    std::vector<Note> notes;
    Random random (seed);
    size_t position = 100;
    const auto step = static_cast<size_t> (0.3 * sampleRate);
    int previous = -1;
    while (position < count)
    {
        const int pitch = 36 + static_cast<int> (random.unit () * 60.0);
        notes.push_back ({ position, static_cast<uint8_t> (0x90 | (random.next () % 16)), static_cast<uint8_t> (pitch), static_cast<uint8_t> (1 + random.next () % 127) });
        if (previous >= 0)
        {
            // Sometimes legato, by letting go of the last note after the new one starts.
            const size_t release = random.unit () < 0.5 ? position + 50 : position - 50;
            notes.push_back ({ release, 0x80, static_cast<uint8_t> (previous), 0 });
        }
        previous = pitch;
        position += step + random.next () % step;
    }
    notes.push_back ({ count > 1000 ? count - 1000 : 0, 0x80, static_cast<uint8_t> (previous), 0 });
    std::stable_sort (notes.begin (), notes.end (), [] (const Note& a, const Note& b) { return a.position < b.position; });
    return notes;
}

using BlockAction = std::function<void (Driver&, size_t position, int count)>;

std::vector<float> render (Driver& driver, size_t total, const std::vector<int>& blockSizes, const std::vector<Note>& notes,
                           const BlockAction& atBlock = nullptr)
{
    std::vector<float> output (total * 2);
    std::vector<float> left (kMaxBlock);
    std::vector<float> right (kMaxBlock);
    std::vector<Note> blockNotes;

    size_t position = 0;
    size_t blockIndex = 0;
    size_t nextNote = 0;
    while (position < total)
    {
        const int count = static_cast<int> (std::min<size_t> (static_cast<size_t> (blockSizes[blockIndex++ % blockSizes.size ()]), total - position));
        if (atBlock)
            atBlock (driver, position, count);

        blockNotes.clear ();
        while (nextNote < notes.size () && notes[nextNote].position < position + static_cast<size_t> (count))
        {
            Note note = notes[nextNote++];
            note.position -= position;
            blockNotes.push_back (note);
        }

        std::fill (left.begin (), left.end (), kUnwritten);
        std::fill (right.begin (), right.end (), kUnwritten);
        driver.render (count, blockNotes, left.data (), right.data ());

        for (int i = 0; i < count; ++i)
        {
            output[(position + static_cast<size_t> (i)) * 2] = left[static_cast<size_t> (i)];
            output[(position + static_cast<size_t> (i)) * 2 + 1] = right[static_cast<size_t> (i)];
        }
        position += static_cast<size_t> (count);
    }
    return output;
}

// The plug-in's parameters, in host terms, read through the VST3 controller.
struct Control
{
    Vst::ParamID id;
    bool isAutomatable;
};

std::vector<Control> gControls;

float controlAt (Vst::IEditController* controller, Vst::ParamID id, double normalized)
{
    return static_cast<float> (controller->normalizedParamToPlain (id, normalized));
}

struct Scenario
{
    std::string name;
    double sampleRate;
    std::function<std::vector<float> (Driver&, Vst::IEditController*)> play;
};

constexpr size_t kSeconds3 = 144000;

std::vector<Scenario> makeScenarios ()
{
    std::vector<Scenario> scenarios;

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        scenarios.push_back ({ "default-" + std::to_string (static_cast<int> (rate)), rate, [rate] (Driver& driver, Vst::IEditController*) {
            const auto total = static_cast<size_t> (3.0 * rate);
            return render (driver, total, { 256 }, makeNotes (1, total, rate));
        } });
    }

    const auto setRandom = [] (Driver& driver, Vst::IEditController* controller, uint32_t seed) {
        Random random (seed * 7919u + 1u);
        for (const auto& control : gControls)
        {
            const double normalized = random.unit ();
            if (control.isAutomatable)
                driver.setControl (control.id, controlAt (controller, control.id, normalized));
        }
    };

    for (uint32_t seed = 1; seed <= 12; ++seed)
    {
        scenarios.push_back ({ "seeded-" + std::to_string (seed), 48000.0, [seed, setRandom] (Driver& driver, Vst::IEditController* controller) {
            setRandom (driver, controller, seed);
            return render (driver, kSeconds3, { 256 }, makeNotes (seed, kSeconds3, 48000.0));
        } });
    }

    const std::vector<std::vector<int>> patterns = { { 32 }, { 64 }, { 1024 }, { 4096 }, { 1 }, { 7, 233, 64, 1, 1000, 511 } };
    for (size_t i = 0; i < patterns.size (); ++i)
    {
        scenarios.push_back ({ "blocks-" + std::to_string (i), 48000.0, [pattern = patterns[i], setRandom] (Driver& driver, Vst::IEditController* controller) {
            setRandom (driver, controller, 3);
            const size_t total = pattern.size () == 1 && pattern[0] == 1 ? 24000 : kSeconds3;
            return render (driver, total, pattern, makeNotes (3, total, 48000.0));
        } });
    }

    for (const double end : { 0.0, 1.0 })
    {
        scenarios.push_back ({ "extreme-" + std::to_string (static_cast<int> (end)), 48000.0, [end] (Driver& driver, Vst::IEditController* controller) {
            // A VST3 host's true ends, which a float can't always hold: see controlToNormalized in the wrapper.
            // Volume stays up, because its bottom is silence, and two silent renders would prove nothing.
            for (const auto& control : gControls)
            {
                if (control.id == kVolumeControl)
                    continue;
                if (auto* vst3 = dynamic_cast<Vst3Driver*> (&driver))
                    vst3->setNormalized (control.id, end);
                else
                    driver.setControl (control.id, controlAt (controller, control.id, end));
            }
            return render (driver, kSeconds3, { 256 }, makeNotes (4, kSeconds3, 48000.0));
        } });
    }

    for (uint32_t seed = 1; seed <= 3; ++seed)
    {
        scenarios.push_back ({ "moving-" + std::to_string (seed), 48000.0, [seed] (Driver& driver, Vst::IEditController* controller) {
            Random random (seed + 500);
            std::vector<double> rates;
            std::vector<double> phases;
            for (size_t i = 0; i < gControls.size (); ++i)
            {
                rates.push_back (0.1 + 3.0 * random.unit ());
                phases.push_back (random.unit ());
            }
            const BlockAction moving = [&] (Driver& target, size_t position, int count) {
                for (size_t i = 0; i < gControls.size (); ++i)
                {
                    const auto valueAt = [&] (size_t at) {
                        return 0.5 + 0.5 * std::sin (2.0 * kPi * (rates[i] * static_cast<double> (at) / 48000.0 + phases[i]));
                    };
                    const float last = controlAt (controller, gControls[i].id, valueAt (position + static_cast<size_t> (count) - 1));
                    if (auto* vst3 = dynamic_cast<Vst3Driver*> (&target))
                        vst3->setRamp (gControls[i].id, valueAt (position), vst3->normalizedForControl (gControls[i].id, last));
                    else
                        target.setControl (gControls[i].id, last);
                }
            };
            return render (driver, kSeconds3, { 128 }, makeNotes (seed, kSeconds3, 48000.0), moving);
        } });
    }

    // Long gaps, so that the voice goes to sleep, and is woken by a note at the very start of a block.
    scenarios.push_back ({ "sleep", 48000.0, [] (Driver& driver, Vst::IEditController*) {
        std::vector<Note> notes;
        for (size_t start = 0; start < 480000; start += 96000)
        {
            notes.push_back ({ start + 5120, 0x90, 60, 100 });
            notes.push_back ({ start + 15000, 0x80, 60, 0 });
        }
        return render (driver, 480000, { 256 }, notes);
    } });

    // Things a host can send, that a VST3 host wouldn't pass on or that have special meanings in MIDI.
    scenarios.push_back ({ "note-edges", 48000.0, [] (Driver& driver, Vst::IEditController*) {
        const std::vector<Note> notes = {
            { 0, 0x90, 60, 100 },    // At a block's first sample.
            { 255, 0x90, 64, 100 },  // At its last.
            { 256, 0x80, 60, 0 },
            { 300, 0xB0, 64, 127 },  // Sustain and the like are ignored.
            { 301, 0x80, 99, 0 },    // A note that was never played.
            { 400, 0x90, 64, 0 },    // A note-on with no velocity is a note-off.
            { 2000, 0x9F, 127, 1 },  // Another channel, and the top of the range.
            { 2000, 0x8F, 127, 1 },  // Both in the same sample.
            { 2100, 0x90, 0, 127 },
            { 50000, 0x80, 0, 64 },
        };
        return render (driver, 96000, { 256 }, notes);
    } });

    // A control changing in the same block as a note, which matters while the voice is asleep.
    scenarios.push_back ({ "change-with-note", 48000.0, [] (Driver& driver, Vst::IEditController* controller) {
        const BlockAction change = [controller] (Driver& target, size_t position, int) {
            if (position == 25600)
                target.setControl (gControls[6].id, controlAt (controller, gControls[6].id, 0.2));
        };
        return render (driver, 96000, { 256 }, { { 25610, 0x90, 48, 100 }, { 60000, 0x80, 48, 0 } }, change);
    } });

    // Stopping and starting in place, which resets the voice.
    scenarios.push_back ({ "reactivate", 48000.0, [setRandom] (Driver& driver, Vst::IEditController* controller) {
        setRandom (driver, controller, 50);
        const auto notes = makeNotes (50, 96000, 48000.0);
        auto first = render (driver, 96000, { 256 }, notes);
        driver.reactivate ();
        const auto second = render (driver, 96000, { 256 }, notes);
        first.insert (first.end (), second.begin (), second.end ());
        return first;
    } });

    return scenarios;
}

std::string describeDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size () != b.size ())
        return "different lengths";
    size_t first = a.size ();
    size_t count = 0;
    double peak = 0.0;
    for (size_t i = 0; i < a.size (); ++i)
    {
        if (std::memcmp (&a[i], &b[i], sizeof (float)) != 0)
        {
            first = std::min (first, i);
            ++count;
            peak = std::max (peak, std::abs (static_cast<double> (a[i]) - static_cast<double> (b[i])));
        }
    }
    if (count == 0)
        return "";
    char text[128];
    std::snprintf (text, sizeof (text), "%zu of %zu samples differ, the first at %zu, peak %g", count, a.size (), first, peak);
    return text;
}

bool hasSound (const std::vector<float>& samples)
{
    return std::any_of (samples.begin (), samples.end (), [] (float sample) { return sample != 0.0f; });
}

// How far a VST3 given unrounded values is from one given the nearest float, which an LV2 host can't avoid.
void reportFloatSteps ()
{
    for (uint32_t seed = 1; seed <= 12; ++seed)
    {
        const auto notes = makeNotes (seed, kSeconds3, 48000.0);
        Vst3Driver rounded (48000.0);
        Vst3Driver exact (48000.0);
        Random random (seed * 7919u + 1u);
        for (const auto& control : gControls)
        {
            const double normalized = random.unit ();
            exact.setNormalized (control.id, normalized);
            rounded.setControl (control.id, controlAt (rounded.controller, control.id, normalized));
        }
        const auto a = render (exact, kSeconds3, { 256 }, notes);
        const auto b = render (rounded, kSeconds3, { 256 }, notes);

        double peakDifference = 0.0;
        double peak = 0.0;
        for (size_t i = 0; i < a.size (); ++i)
        {
            peakDifference = std::max (peakDifference, std::abs (static_cast<double> (a[i]) - static_cast<double> (b[i])));
            peak = std::max (peak, std::abs (static_cast<double> (a[i])));
        }
        std::printf ("float steps, seed %2u: peak %.3f, difference %.1f dB below full scale\n", seed, peak,
                     peakDifference > 0.0 ? -20.0 * std::log10 (peakDifference) : 999.0);
    }
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("Usage: Lv2Compare <VST3 .so> <LV2 .so> [scenario name contains]\n");
        return 2;
    }
    const std::string filter = argc > 3 ? argv[3] : "";

    void* vst3Module = dlopen (argv[1], RTLD_LAZY | RTLD_LOCAL);
    void* lv2Module = dlopen (argv[2], RTLD_LAZY | RTLD_LOCAL);
    if (!vst3Module || !lv2Module)
    {
        std::printf ("Could not load: %s\n", dlerror ());
        return 2;
    }

    auto moduleEntry = reinterpret_cast<bool (*) (void*)> (dlsym (vst3Module, "ModuleEntry"));
    auto moduleExit = reinterpret_cast<bool (*) ()> (dlsym (vst3Module, "ModuleExit"));
    auto getFactory = reinterpret_cast<GetFactoryProc> (dlsym (vst3Module, "GetPluginFactory"));
    auto getDescriptor = reinterpret_cast<const LV2_Descriptor* (*) (uint32_t)> (dlsym (lv2Module, "lv2_descriptor"));
    if (!moduleEntry || !moduleExit || !getFactory || !getDescriptor || !moduleEntry (vst3Module))
    {
        std::printf ("Missing entry points\n");
        return 2;
    }

    gFactory = getFactory ();
    for (int32 i = 0; i < gFactory->countClasses (); ++i)
    {
        PClassInfo info;
        gFactory->getClassInfo (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            std::memcpy (gComponentId, info.cid, sizeof (TUID));
        else if (std::strcmp (info.category, kVstComponentControllerClass) == 0)
            std::memcpy (gControllerId, info.cid, sizeof (TUID));
    }

    gDescriptor = getDescriptor (0);
    int failures = 0;
    const auto check = [&failures] (bool isOk, const std::string& what) {
        std::printf ("%s  %s\n", isOk ? "ok  " : "FAIL", what.c_str ());
        std::fflush (stdout);
        if (!isOk)
            ++failures;
    };

    check (gDescriptor != nullptr && getDescriptor (1) == nullptr, "one descriptor");
    if (!gDescriptor)
        return 1;
    std::printf ("uri %s\n", gDescriptor->URI);

    {
        Vst3Driver reference (48000.0);
        for (int32 i = 0; i < reference.controller->getParameterCount (); ++i)
        {
            Vst::ParameterInfo info {};
            reference.controller->getParameterInfo (i, info);
            gControls.push_back ({ info.id, (info.flags & Vst::ParameterInfo::kCanAutomate) != 0 });
        }
    }

    // Without what the Turtle requires, a plug-in has to refuse rather than crash.
    {
        const LV2_Feature* const none[] = { nullptr };
        check (gDescriptor->instantiate (gDescriptor, 48000.0, "", none) == nullptr, "refuses a host without the required features");
    }

    // The counter's own check: making an instance allocates, so it has to see that from inside the plug-in.
    {
        const long before = gAllocations;
        Lv2Driver driver (48000.0);
        check (driver.isValid (), "instantiates");
        check (gAllocations > before, "the allocation counter sees the plug-in's allocations");
    }

    for (const auto& scenario : makeScenarios ())
    {
        if (scenario.name.find (filter) == std::string::npos)
            continue;

        Vst3Driver vst3 (scenario.sampleRate);
        Lv2Driver lv2 (scenario.sampleRate);
        const auto expected = scenario.play (vst3, vst3.controller);
        const auto actual = scenario.play (lv2, vst3.controller);

        const auto difference = describeDifference (expected, actual);
        check (difference.empty (), scenario.name + (difference.empty () ? "" : ": " + difference));
        check (hasSound (expected), "  has sound");
        check (std::find (actual.begin (), actual.end (), kUnwritten) == actual.end (), "  writes every sample");
        check (lv2.allocationsInRun == 0, "  no allocations in run (): " + std::to_string (lv2.allocationsInRun));
    }

    if (filter.empty ())
    {
        // That this test can fail: one note a sample late, and one control a float's step away.
        const auto notes = makeNotes (7, kSeconds3, 48000.0);
        auto lateNotes = notes;
        lateNotes.front ().position += 1;
        {
            Lv2Driver a (48000.0);
            Lv2Driver b (48000.0);
            check (!describeDifference (render (a, kSeconds3, { 256 }, notes), render (b, kSeconds3, { 256 }, lateNotes)).empty (), "a note a sample late is noticed");
        }
        {
            Lv2Driver a (48000.0);
            Lv2Driver b (48000.0);
            b.setControl (gControls[6].id, std::nextafter (1000.0f, 2000.0f));
            check (!describeDifference (render (a, kSeconds3, { 256 }, notes), render (b, kSeconds3, { 256 }, notes)).empty (), "a control one float step away is noticed");
        }
        {
            // Two at once, because they share a module.
            Lv2Driver one (48000.0);
            Lv2Driver two (48000.0);
            Lv2Driver alone (48000.0);
            const BlockAction interleave = [&two, &notes] (Driver&, size_t position, int count) {
                std::vector<float> left (kMaxBlock);
                std::vector<float> right (kMaxBlock);
                if (position == 0)
                    two.render (count, { { 100, 0x90, 72, 90 } }, left.data (), right.data ());
                else
                    two.render (count, {}, left.data (), right.data ());
                (void) notes;
            };
            const auto together = render (one, kSeconds3, { 256 }, notes, interleave);
            const auto separate = render (alone, kSeconds3, { 256 }, notes);
            check (describeDifference (together, separate).empty (), "two instances don't disturb each other");
        }

        reportFloatSteps ();
    }

    gFactory->release ();
    moduleExit ();
    std::printf ("%d failure(s)\n", failures);
    // Not unloading, because the modules' static objects outlive this point in a test program, and hosts' unloading
    // is covered by the VST3's own tests.
    return failures == 0 ? 0 : 1;
}
