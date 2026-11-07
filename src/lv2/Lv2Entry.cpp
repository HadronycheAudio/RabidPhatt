// Runs the plug-in's own VST3 processor behind an LV2 interface, in the same process and with no changes to it, so
// that the two can only differ if this file gets something wrong. It has audio, control ports and MIDI, and no
// panel, state or transport yet. tests/lv2/Lv2Compare.cpp compares its output with the VST3's, bit for bit.

#include "Parameters.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
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

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

// From the SDK's linuxmain.cpp.
extern "C" bool ModuleEntry (void*);
extern "C" bool ModuleExit ();

using namespace Steinberg;

namespace RabidPhatt {

namespace {

// Permanent from the first release, like the port symbols and indices in RabidPhatt.ttl.
constexpr const char* kUri = "https://github.com/HadronycheAudio/RabidPhatt";

enum Port : uint32_t
{
    kPortLeft,
    kPortRight,
    kPortEvents,
    kPortFirstControl // Then one for each ParamId, in order, so that new parameters only add ports at the end.
};

// More notes than this in one block are dropped, because run () can't allocate.
constexpr int kMaxEvents = 1024;

constexpr int kStereo = 2;

#define LV2ENTRY_NO_REFCOUNT                                                                  \
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }  \
    uint32 PLUGIN_API addRef () override { return 1; }                                        \
    uint32 PLUGIN_API release () override { return 1; }

class HostContext : public Vst::IHostApplication
{
public:
    tresult PLUGIN_API queryInterface (const TUID iid, void** object) override
    {
        if (FUnknownPrivate::iidEqual (iid, Vst::IHostApplication::iid) || FUnknownPrivate::iidEqual (iid, FUnknown::iid))
        {
            *object = static_cast<Vst::IHostApplication*> (this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef () override { return 1; }
    uint32 PLUGIN_API release () override { return 1; }

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

// A control port has one value per run (), which ParamStore::readChanges treats like a point at offset 0.
class OnePointQueue : public Vst::IParamValueQueue
{
public:
    LV2ENTRY_NO_REFCOUNT

    void set (Vst::ParamID id, double normalized)
    {
        _id = id;
        _normalized = normalized;
    }

    Vst::ParamID PLUGIN_API getParameterId () override { return _id; }
    int32 PLUGIN_API getPointCount () override { return 1; }

    tresult PLUGIN_API getPoint (int32 index, int32& offset, Vst::ParamValue& value) override
    {
        if (index != 0)
            return kResultFalse;
        offset = 0;
        value = _normalized;
        return kResultTrue;
    }

    tresult PLUGIN_API addPoint (int32, Vst::ParamValue, int32&) override { return kResultFalse; }

private:
    Vst::ParamID _id { 0 };
    double _normalized { 0.0 };
};

class Changes : public Vst::IParameterChanges
{
public:
    LV2ENTRY_NO_REFCOUNT

    void clear () { _count = 0; }
    void add (Vst::ParamID id, double normalized) { _queues[static_cast<size_t> (_count++)].set (id, normalized); }

    int32 PLUGIN_API getParameterCount () override { return _count; }

    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 index) override
    {
        return index >= 0 && index < _count ? &_queues[static_cast<size_t> (index)] : nullptr;
    }

    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID&, int32&) override { return nullptr; }

private:
    std::array<OnePointQueue, kParamCount> _queues;
    int32 _count { 0 };
};

class Events : public Vst::IEventList
{
public:
    LV2ENTRY_NO_REFCOUNT

    void clear () { _count = 0; }

    void add (const Vst::Event& event)
    {
        if (_count < kMaxEvents)
            _events[static_cast<size_t> (_count++)] = event;
    }

    int32 PLUGIN_API getEventCount () override { return _count; }

    tresult PLUGIN_API getEvent (int32 index, Vst::Event& event) override
    {
        if (index < 0 || index >= _count)
            return kResultFalse;
        event = _events[static_cast<size_t> (index)];
        return kResultOk;
    }

    tresult PLUGIN_API addEvent (Vst::Event&) override { return kResultFalse; }

private:
    std::array<Vst::Event, kMaxEvents> _events {};
    int32 _count { 0 };
};

// The handle is null on purpose. On Linux VSTGUI only uses it to find a resources folder, by removing three levels
// from the binary's path as if it were in a .vst3 bundle, and it crashes when a shallower LV2 path runs out, such as
// /opt/RabidPhatt.lv2/RabidPhatt.so. The panels load nothing from there, so without a handle it doesn't look.
std::mutex gModuleMutex; // The SDK's count of module users isn't safe across threads.

bool enterModule ()
{
    const std::lock_guard<std::mutex> lock (gModuleMutex);
    if (ModuleEntry (nullptr))
        return true;

    ModuleExit ();
    return false;
}

void exitModule ()
{
    const std::lock_guard<std::mutex> lock (gModuleMutex);
    ModuleExit ();
}

// A float can't hold every bottom of range, such as 0.001, and the nearest one is a little above it. This keeps
// a host's minimum and maximum the same as a VST3 host's 0 and 1.
double controlToNormalized (Vst::ParamID id, float value)
{
    const auto& spec = getParamSpec (id);
    if (value <= static_cast<float> (spec.min))
        return 0.0;
    if (value >= static_cast<float> (spec.max))
        return 1.0;
    return plainToNormalized (spec, value);
}

class Instance
{
public:
    ~Instance ()
    {
        if (_processor)
            _processor->release ();
        if (_component)
        {
            _component->terminate ();
            _component->release ();
        }
    }

    bool create (double sampleRate, int32 maxBlock, LV2_URID midiEvent)
    {
        _midiEvent = midiEvent;

        auto* factory = GetPluginFactory ();
        if (!factory)
            return false;

        TUID componentId {};
        bool isFound = false;
        for (int32 i = 0; i < factory->countClasses () && !isFound; ++i)
        {
            PClassInfo info;
            if (factory->getClassInfo (i, &info) == kResultOk && std::strcmp (info.category, kVstAudioEffectClass) == 0)
            {
                std::memcpy (componentId, info.cid, sizeof (TUID));
                isFound = true;
            }
        }
        const bool isCreated = isFound && factory->createInstance (componentId, Vst::IComponent::iid, reinterpret_cast<void**> (&_component)) == kResultOk;
        factory->release ();
        if (!isCreated || _component->initialize (&_hostContext) != kResultOk)
            return false;
        if (_component->queryInterface (Vst::IAudioProcessor::iid, reinterpret_cast<void**> (&_processor)) != kResultOk)
            return false;

        Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, maxBlock, sampleRate };
        if (_processor->setupProcessing (setup) != kResultOk)
            return false;
        _component->activateBus (Vst::kAudio, Vst::kOutput, 0, true);
        _component->activateBus (Vst::kEvent, Vst::kInput, 0, true);

        // The processor starts on its defaults, so a port holding its default doesn't count as a change. That
        // leaves those parameters at the exact value from ParamStore, instead of one that went through a float.
        for (Vst::ParamID id = 0; id < kParamCount; ++id)
            _lastControls[id] = static_cast<float> (getParamSpec (id).defaultValue);
        return true;
    }

    void connect (uint32_t port, void* data)
    {
        if (port == kPortLeft)
            _outputs[0] = static_cast<float*> (data);
        else if (port == kPortRight)
            _outputs[1] = static_cast<float*> (data);
        else if (port == kPortEvents)
            _eventInput = static_cast<const LV2_Atom_Sequence*> (data);
        else if (port - kPortFirstControl < kParamCount)
            _controls[port - kPortFirstControl] = static_cast<const float*> (data);
    }

    void activate ()
    {
        _component->setActive (true);
        _processor->setProcessing (true);
    }

    void deactivate ()
    {
        _processor->setProcessing (false);
        _component->setActive (false);
    }

    void run (uint32_t sampleCount)
    {
        readControls ();
        readEvents ();

        Vst::AudioBusBuffers output {};
        output.numChannels = kStereo;
        output.channelBuffers32 = _outputs.data ();

        Vst::ProcessData data {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = static_cast<int32> (sampleCount);
        data.numOutputs = 1;
        data.outputs = &output;
        data.inputParameterChanges = &_changes;
        data.inputEvents = &_events;
        _processor->process (data);
    }

private:
    void readControls ()
    {
        _changes.clear ();
        for (Vst::ParamID id = 0; id < kParamCount; ++id)
        {
            if (!_controls[id])
                continue;

            const float value = *_controls[id];
            if (value == _lastControls[id] || !std::isfinite (value))
                continue;

            _lastControls[id] = value;
            _changes.add (id, controlToNormalized (id, value));
        }
    }

    void readEvents ()
    {
        _events.clear ();
        if (!_eventInput)
            return;

        LV2_ATOM_SEQUENCE_FOREACH (_eventInput, event)
        {
            if (event->body.type != _midiEvent || event->body.size < 3)
                continue;

            const auto* message = reinterpret_cast<const uint8_t*> (event + 1);
            const int type = message[0] & 0xF0;
            const auto channel = static_cast<int16> (message[0] & 0x0F);
            const auto pitch = static_cast<int16> (message[1] & 0x7F);
            const float velocity = static_cast<float> (message[2] & 0x7F) / 127.0f;

            Vst::Event out {};
            out.sampleOffset = static_cast<int32> (event->time.frames);
            if (type == LV2_MIDI_MSG_NOTE_ON)
            {
                out.type = Vst::Event::kNoteOnEvent;
                out.noteOn.channel = channel;
                out.noteOn.pitch = pitch;
                out.noteOn.velocity = velocity;
                out.noteOn.noteId = -1;
                _events.add (out);
            }
            else if (type == LV2_MIDI_MSG_NOTE_OFF)
            {
                out.type = Vst::Event::kNoteOffEvent;
                out.noteOff.channel = channel;
                out.noteOff.pitch = pitch;
                out.noteOff.velocity = velocity;
                out.noteOff.noteId = -1;
                _events.add (out);
            }
        }
    }

    HostContext _hostContext;
    Vst::IComponent* _component { nullptr };
    Vst::IAudioProcessor* _processor { nullptr };
    LV2_URID _midiEvent { 0 };

    std::array<float*, kStereo> _outputs {};
    const LV2_Atom_Sequence* _eventInput { nullptr };
    std::array<const float*, kParamCount> _controls {};
    std::array<float, kParamCount> _lastControls {};

    Changes _changes;
    Events _events;
};

LV2_Handle instantiate (const LV2_Descriptor*, double sampleRate, const char*, const LV2_Feature* const* features)
{
    LV2_URID_Map* map = nullptr;
    const LV2_Options_Option* options = nullptr;
    bool isBlockBounded = false;
    for (int i = 0; features && features[i]; ++i)
    {
        if (std::strcmp (features[i]->URI, LV2_URID__map) == 0)
            map = static_cast<LV2_URID_Map*> (features[i]->data);
        else if (std::strcmp (features[i]->URI, LV2_OPTIONS__options) == 0)
            options = static_cast<const LV2_Options_Option*> (features[i]->data);
        else if (std::strcmp (features[i]->URI, LV2_BUF_SIZE__boundedBlockLength) == 0)
            isBlockBounded = true;
    }
    if (!map || !options || !isBlockBounded)
        return nullptr;

    // setupProcessing needs a maximum, which is why the Turtle requires it.
    const LV2_URID maxBlockKey = map->map (map->handle, LV2_BUF_SIZE__maxBlockLength);
    const LV2_URID atomInt = map->map (map->handle, LV2_ATOM__Int);
    int32 maxBlock = 0;
    for (int i = 0; options[i].key != 0 || options[i].value != nullptr; ++i)
    {
        if (options[i].key == maxBlockKey && options[i].type == atomInt)
            maxBlock = *static_cast<const int32_t*> (options[i].value);
    }
    if (maxBlock <= 0 || !enterModule ())
        return nullptr;

    auto* instance = new (std::nothrow) Instance;
    if (!instance || !instance->create (sampleRate, maxBlock, map->map (map->handle, LV2_MIDI__MidiEvent)))
    {
        delete instance;
        exitModule ();
        return nullptr;
    }
    return instance;
}

void connectPort (LV2_Handle handle, uint32_t port, void* data)
{
    static_cast<Instance*> (handle)->connect (port, data);
}

void activate (LV2_Handle handle)
{
    static_cast<Instance*> (handle)->activate ();
}

void run (LV2_Handle handle, uint32_t sampleCount)
{
    static_cast<Instance*> (handle)->run (sampleCount);
}

void deactivate (LV2_Handle handle)
{
    static_cast<Instance*> (handle)->deactivate ();
}

void cleanup (LV2_Handle handle)
{
    delete static_cast<Instance*> (handle);
    exitModule ();
}

const void* extensionData (const char*)
{
    return nullptr;
}

const LV2_Descriptor kDescriptor { kUri, instantiate, connectPort, activate, run, deactivate, cleanup, extensionData };

} // namespace

} // namespace RabidPhatt

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor (uint32_t index)
{
    return index == 0 ? &RabidPhatt::kDescriptor : nullptr;
}
