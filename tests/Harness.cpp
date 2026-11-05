#include "Harness.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "public.sdk/source/common/memorystream.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Steinberg;

DEF_CLASS_IID (Vst::IEditController)
DEF_CLASS_IID (Vst::IComponent)
DEF_CLASS_IID (Vst::IAudioProcessor)
DEF_CLASS_IID (Vst::IConnectionPoint)
DEF_CLASS_IID (Vst::IMessage)
DEF_CLASS_IID (Vst::IAttributeList)
DEF_CLASS_IID (Vst::IHostApplication)
DEF_CLASS_IID (IPlugView)

namespace RabidPhatt {
namespace Tests {

namespace {

constexpr double kPi = 3.14159265358979;

#define TESTS_NO_REFCOUNT                                                                     \
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }  \
    uint32 PLUGIN_API addRef () override { return 1; }                                        \
    uint32 PLUGIN_API release () override { return 1; }

class Host : public Vst::IHostApplication
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

class ValueQueue : public Vst::IParamValueQueue
{
public:
    TESTS_NO_REFCOUNT

    ValueQueue (Vst::ParamID id, double value) : _id (id), _value (value) {}

    Vst::ParamID PLUGIN_API getParameterId () override { return _id; }
    int32 PLUGIN_API getPointCount () override { return 1; }

    tresult PLUGIN_API getPoint (int32, int32& offset, Vst::ParamValue& value) override
    {
        offset = 0;
        value = _value;
        return kResultTrue;
    }

    tresult PLUGIN_API addPoint (int32, Vst::ParamValue, int32&) override { return kResultFalse; }

private:
    Vst::ParamID _id;
    double _value;
};

Host gHost;
HMODULE gModule = nullptr;
IPluginFactory* gFactory = nullptr;
bool (*gExitDll) () = nullptr;
TUID gComponentId;
TUID gControllerId;
int gFailures = 0;

} // namespace

class ParameterChanges : public Vst::IParameterChanges
{
public:
    TESTS_NO_REFCOUNT

    int32 PLUGIN_API getParameterCount () override { return static_cast<int32> (queues.size ()); }
    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 index) override { return &queues[static_cast<size_t> (index)]; }
    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID&, int32&) override { return nullptr; }

    std::vector<ValueQueue> queues;
};

class EventList : public Vst::IEventList
{
public:
    TESTS_NO_REFCOUNT

    int32 PLUGIN_API getEventCount () override { return static_cast<int32> (blockEvents.size ()); }

    tresult PLUGIN_API getEvent (int32 index, Vst::Event& event) override
    {
        event = blockEvents[static_cast<size_t> (index)];
        return kResultOk;
    }

    tresult PLUGIN_API addEvent (Vst::Event&) override { return kResultFalse; }

    std::vector<Vst::Event> blockEvents;

    // Waiting for their block, with sampleOffset counted from the start of the next render.
    std::vector<Vst::Event> pending;
};

bool loadPlugin (const wchar_t* path)
{
    gModule = LoadLibraryW (path);
    if (!gModule)
        return false;

    auto initDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "InitDll"));
    gExitDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "ExitDll"));
    auto getPluginFactory = reinterpret_cast<GetFactoryProc> (GetProcAddress (gModule, "GetPluginFactory"));
    if (!initDll || !gExitDll || !getPluginFactory || !initDll ())
        return false;

    gFactory = getPluginFactory ();
    for (int32 i = 0; i < gFactory->countClasses (); ++i)
    {
        PClassInfo info;
        gFactory->getClassInfo (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            std::memcpy (gComponentId, info.cid, sizeof (TUID));
        else if (std::strcmp (info.category, kVstComponentControllerClass) == 0)
            std::memcpy (gControllerId, info.cid, sizeof (TUID));
    }
    return true;
}

void unloadPlugin ()
{
    gFactory->release ();
    gExitDll ();
    FreeLibrary (gModule);
}

IPluginFactory* getFactory ()
{
    return gFactory;
}

void check (const char* what, bool ok)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++gFailures;
}

int getFailureCount ()
{
    return gFailures;
}

Plug::Plug (double sampleRate) : _changes (new ParameterChanges), _events (new EventList), _sampleRate (sampleRate)
{
    gFactory->createInstance (gComponentId, Vst::IComponent::iid, reinterpret_cast<void**> (&component));
    component->initialize (&gHost);
    component->queryInterface (Vst::IAudioProcessor::iid, reinterpret_cast<void**> (&processor));

    gFactory->createInstance (gControllerId, Vst::IEditController::iid, reinterpret_cast<void**> (&controller));
    controller->initialize (&gHost);

    component->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_componentConnection));
    controller->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_controllerConnection));
    _componentConnection->connect (_controllerConnection);
    _controllerConnection->connect (_componentConnection);

    Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, kBlockSize, sampleRate };
    processor->setupProcessing (setup);
    component->activateBus (Vst::kAudio, Vst::kOutput, 0, true);
    component->activateBus (Vst::kEvent, Vst::kInput, 0, true);
    component->setActive (true);
    processor->setProcessing (true);
}

Plug::~Plug ()
{
    processor->setProcessing (false);
    component->setActive (false);
    _componentConnection->disconnect (_controllerConnection);
    _controllerConnection->disconnect (_componentConnection);
    _componentConnection->release ();
    _controllerConnection->release ();
    controller->terminate ();
    controller->release ();
    processor->release ();
    component->terminate ();
    component->release ();
    delete _changes;
    delete _events;
}

void Plug::set (ParamId id, double plain)
{
    const double normalized = controller->plainParamToNormalized (id, plain);
    controller->setParamNormalized (id, normalized);
    _changes->queues.emplace_back (id, normalized);
}

void Plug::noteOn (int pitch, int offset, float velocity)
{
    Vst::Event event {};
    event.type = Vst::Event::kNoteOnEvent;
    event.sampleOffset = offset;
    event.noteOn.pitch = static_cast<int16> (pitch);
    event.noteOn.velocity = velocity;
    event.noteOn.noteId = -1;
    _events->pending.push_back (event);
}

void Plug::noteOff (int pitch, int offset)
{
    Vst::Event event {};
    event.type = Vst::Event::kNoteOffEvent;
    event.sampleOffset = offset;
    event.noteOff.pitch = static_cast<int16> (pitch);
    event.noteOff.noteId = -1;
    _events->pending.push_back (event);
}

Stereo Plug::render (double seconds)
{
    return renderSamples (static_cast<size_t> (seconds * _sampleRate));
}

Stereo Plug::renderSamples (size_t total)
{
    Stereo result;
    result.left.resize (total);
    result.right.resize (total);

    for (size_t position = 0; position < total; position += kBlockSize)
    {
        const int count = static_cast<int> (std::min<size_t> (kBlockSize, total - position));
        float* outputs[2] = { &result.left[position], &result.right[position] };

        Vst::AudioBusBuffers outputBus {};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outputs;

        _events->blockEvents.clear ();
        auto& pending = _events->pending;
        for (auto it = pending.begin (); it != pending.end ();)
        {
            if (it->sampleOffset < static_cast<int32> (position) + count)
            {
                Vst::Event event = *it;
                event.sampleOffset -= static_cast<int32> (position);
                _events->blockEvents.push_back (event);
                it = pending.erase (it);
            }
            else
            {
                ++it;
            }
        }

        Vst::ProcessData data {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = count;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = _changes->queues.empty () ? nullptr : _changes;
        data.inputEvents = _events;
        data.processContext = context;
        processor->process (data);
        _changes->queues.clear ();

        if (context && (context->state & Vst::ProcessContext::kPlaying) != 0)
            context->projectTimeMusic += count * context->tempo / (60.0 * _sampleRate);

        result.blockSilent.push_back (outputBus.silenceFlags == 3);
    }
    return result;
}

void Plug::bare ()
{
    set (kAmpAttack, 1);
    set (kAmpSustain, 100);
    set (kFilterCutoff, 20000);
    set (kFilterResonance, 0);
    set (kFilterEnvAmount, 0);
    set (kVolume, 0);
}

std::vector<char> Plug::getStateBytes ()
{
    MemoryStream stream;
    component->getState (&stream);
    return std::vector<char> (stream.getData (), stream.getData () + stream.getSize ());
}

Samples slice (const Samples& samples, size_t from, size_t count)
{
    return Samples (samples.begin () + static_cast<std::ptrdiff_t> (from),
                    samples.begin () + static_cast<std::ptrdiff_t> (std::min (samples.size (), from + count)));
}

double rms (const Samples& samples, size_t from)
{
    double energy = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        energy += static_cast<double> (samples[i]) * samples[i];
    return std::sqrt (energy / static_cast<double> (std::max<size_t> (1, samples.size () - from)));
}

double peak (const Samples& samples, size_t from)
{
    double highest = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        highest = std::max (highest, static_cast<double> (std::abs (samples[i])));
    return highest;
}

double mean (const Samples& samples, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        sum += samples[i];
    return sum / static_cast<double> (std::max<size_t> (1, samples.size () - from));
}

double db (double value)
{
    return 20.0 * std::log10 (std::max (value, 1e-12));
}

double toneLevel (const Samples& samples, double hz, size_t from, double sampleRate)
{
    const size_t count = samples.size () - from;
    double real = 0.0;
    double imaginary = 0.0;
    double windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (count - 1));
        const double phase = 2.0 * kPi * hz * static_cast<double> (i) / sampleRate;
        real += window * samples[from + i] * std::cos (phase);
        imaginary += window * samples[from + i] * std::sin (phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

// By autocorrelation, because many saws with different phases cross zero many times per cycle. It finds
// the first strong repeat, which is the period rather than a multiple of it.
double measureFrequency (const Samples& samples, double sampleRate)
{
    const int count = static_cast<int> (std::min<size_t> (samples.size (), 9600));
    const int maxLag = std::min (static_cast<int> (sampleRate / 25.0), count / 2);
    if (maxLag < 4)
        return 0.0;

    std::vector<double> similarity (static_cast<size_t> (maxLag) + 2, 0.0);
    for (int lag = 1; lag <= maxLag + 1 && lag < count; ++lag)
    {
        double both = 0.0;
        double first = 0.0;
        double second = 0.0;
        for (int i = 0; i + lag < count; ++i)
        {
            const double a = samples[static_cast<size_t> (i)];
            const double b = samples[static_cast<size_t> (i + lag)];
            both += a * b;
            first += a * a;
            second += b * b;
        }
        similarity[static_cast<size_t> (lag)] = both / std::max (std::sqrt (first * second), 1e-20);
    }

    // Lags below this just see the same part of the wave, which says nothing about its period.
    int start = 1;
    while (start <= maxLag && similarity[static_cast<size_t> (start)] > 0.5)
        ++start;

    double strongest = 0.0;
    for (int lag = start; lag <= maxLag; ++lag)
        strongest = std::max (strongest, similarity[static_cast<size_t> (lag)]);

    for (int lag = std::max (start, 2); lag <= maxLag; ++lag)
    {
        const double before = similarity[static_cast<size_t> (lag - 1)];
        const double here = similarity[static_cast<size_t> (lag)];
        const double after = similarity[static_cast<size_t> (lag + 1)];
        if (here >= 0.9 * strongest && here >= before && here >= after)
        {
            const double curve = before - 2.0 * here + after;
            const double offset = curve != 0.0 ? 0.5 * (before - after) / curve : 0.0;
            return sampleRate / (lag + offset);
        }
    }
    return 0.0;
}

double frequencyOfNote (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

bool isFinite (const Samples& samples)
{
    return std::all_of (samples.begin (), samples.end (), [] (float sample) { return std::isfinite (sample); });
}

size_t firstNonZero (const Samples& samples)
{
    const auto found = std::find_if (samples.begin (), samples.end (), [] (float sample) { return sample != 0.0f; });
    return static_cast<size_t> (found - samples.begin ());
}

} // namespace Tests
} // namespace RabidPhatt
