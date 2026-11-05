// These protect projects people have saved. The expected values are written out here on purpose, rather
// than taken from the plug-in's own definitions, so that changing those definitions fails a test.

#include "Harness.h"
#include "Tests.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Steinberg;

namespace RabidPhatt {
namespace Tests {

namespace {

// Hosts save these numbers, so they can never change. New parameters get new numbers.
static_assert (kMotion == 0 && kGlide == 1, "Parameter IDs are saved in projects");
static_assert (kAmpAttack == 2 && kAmpDecay == 3 && kAmpSustain == 4 && kAmpRelease == 5, "Parameter IDs are saved in projects");
static_assert (kFilterCutoff == 6 && kFilterResonance == 7 && kFilterEnvAmount == 8, "Parameter IDs are saved in projects");
static_assert (kFilterAttack == 9 && kFilterDecay == 10 && kFilterSustain == 11 && kFilterRelease == 12, "Parameter IDs are saved in projects");
static_assert (kVolume == 13 && kLfoRate == 14 && kLfoAmount == 15 && kLfoDelay == 16 && kLfoSync == 17 && kLfoDivision == 18, "Parameter IDs are saved in projects");

template <typename T>
T readAt (const std::vector<char>& bytes, size_t offset)
{
    T value {};
    std::memcpy (&value, bytes.data () + offset, sizeof (value));
    return value;
}

bool nearly (double a, double b)
{
    return std::abs (a - b) < 1e-6 * std::max (1.0, std::abs (b));
}

} // namespace

void testClassIds ()
{
    bool foundProcessor = false;
    bool foundController = false;
    const FUID processor (0x20183AE4, 0x631D4CAB, 0xB004BC97, 0x089F42FE);
    const FUID controller (0x035E7E78, 0xF5134DE5, 0xADDAB440, 0x895A9716);

    auto* factory = getFactory ();
    for (int32 i = 0; i < factory->countClasses (); ++i)
    {
        PClassInfo info;
        factory->getClassInfo (i, &info);
        foundProcessor = foundProcessor || FUID::fromTUID (info.cid) == processor;
        foundController = foundController || FUID::fromTUID (info.cid) == controller;
    }
    check ("the processor's class ID hasn't changed", foundProcessor);
    check ("the controller's class ID hasn't changed", foundController);
}

void testParameterIds ()
{
    Plug plug;
    check ("there are 19 parameters", plug.controller->getParameterCount () == 19);

    bool allMatch = true;
    for (int32 i = 0; i < plug.controller->getParameterCount (); ++i)
    {
        Vst::ParameterInfo info {};
        plug.controller->getParameterInfo (i, info);
        allMatch = allMatch && info.id == static_cast<Vst::ParamID> (i)
                   && (info.flags & Vst::ParameterInfo::kCanAutomate) != 0;
    }
    check ("IDs follow the enum, and all are automatable", allMatch);
}

void testParameterRanges ()
{
    struct Range
    {
        ParamId id;
        double min;
        double max;
    };
    const Range kRanges[] = {
        { kMotion, 0.001, 1 },          { kGlide, 1, 5000 },
        { kAmpAttack, 1, 10000 },   { kAmpDecay, 1, 10000 },     { kAmpSustain, 0, 100 },    { kAmpRelease, 1, 10000 },
        { kFilterCutoff, 20, 20000 }, { kFilterResonance, 0, 100 }, { kFilterEnvAmount, -8, 8 },
        { kFilterAttack, 1, 10000 }, { kFilterDecay, 1, 10000 }, { kFilterSustain, 0, 100 }, { kFilterRelease, 1, 10000 },
        { kVolume, -60, 6 },
        { kLfoRate, 0.05, 20 },     { kLfoAmount, 0, 100 },      { kLfoDelay, 0, 10 },
        { kLfoSync, 0, 1 },         { kLfoDivision, 0, 15 },
    };

    Plug plug;
    bool allMatch = true;
    for (const auto& range : kRanges)
    {
        allMatch = allMatch && nearly (plug.controller->normalizedParamToPlain (range.id, 0.0), range.min)
                   && nearly (plug.controller->normalizedParamToPlain (range.id, 1.0), range.max);
    }
    check ("every range is as it was", allMatch);

    check ("glide and the times are logarithmic", nearly (plug.controller->normalizedParamToPlain (kAmpAttack, 0.5), 100.0));
    check ("cutoff is logarithmic", std::abs (plug.controller->normalizedParamToPlain (kFilterCutoff, 0.5) - 632.46) < 0.01);
    check ("motion is logarithmic", std::abs (plug.controller->normalizedParamToPlain (kMotion, 0.5) - 0.0316228) < 1e-5);
    check ("LFO rate is logarithmic", nearly (plug.controller->normalizedParamToPlain (kLfoRate, 0.5), 1.0));
    check ("LFO delay is squared, and reaches zero", nearly (plug.controller->normalizedParamToPlain (kLfoDelay, 0.5), 2.5) && plug.controller->normalizedParamToPlain (kLfoDelay, 0.0) == 0.0);
}

void testSavedFormat ()
{
    Plug plug;
    const auto bytes = plug.getStateBytes ();
    check ("version, count, then 19 ID and value pairs", bytes.size () == 8 + 19 * 12);
    check ("version 1", readAt<int32_t> (bytes, 0) == 1);
    check ("the count is 19", readAt<int32_t> (bytes, 4) == 19);
    check ("the first pair is Motion", readAt<int32_t> (bytes, 8) == 0 && std::abs (readAt<double> (bytes, 12) - 0.799313) < 1e-5);

    MemoryStream empty;
    check ("an empty state is rejected without crashing", plug.component->setState (&empty) != kResultOk);

    // A state from a future version with parameters unknown to this one still loads what it knows.
    MemoryStream stream;
    const int32_t header[2] = { 1, 2 };
    int32 written = 0;
    stream.write (const_cast<int32_t*> (header), sizeof (header), &written);
    const int32_t unknownId = 999;
    const double value = 0.5;
    const int32_t motionId = 0;
    stream.write (const_cast<int32_t*> (&unknownId), 4, &written);
    stream.write (const_cast<double*> (&value), 8, &written);
    stream.write (const_cast<int32_t*> (&motionId), 4, &written);
    stream.write (const_cast<double*> (&value), 8, &written);
    int64 position = 0;
    stream.seek (0, IBStream::kIBSeekSet, &position);
    check ("an unknown parameter is skipped", plug.component->setState (&stream) == kResultOk);
    check ("and the known one still arrives", nearly (readAt<double> (plug.getStateBytes (), 12), 0.5));
}

void testBuses ()
{
    Plug plug;
    check ("no audio inputs", plug.component->getBusCount (Vst::kAudio, Vst::kInput) == 0);
    check ("one audio output", plug.component->getBusCount (Vst::kAudio, Vst::kOutput) == 1);
    check ("one event input", plug.component->getBusCount (Vst::kEvent, Vst::kInput) == 1);

    Vst::SpeakerArrangement arrangement = 0;
    plug.processor->getBusArrangement (Vst::kOutput, 0, arrangement);
    check ("the output is stereo", arrangement == Vst::SpeakerArr::kStereo);
    check ("no latency", plug.processor->getLatencySamples () == 0);

    FUnknownPtr<IPluginFactory2> factory (getFactory ());
    bool isInstrument = false;
    for (int32 i = 0; factory && i < factory->countClasses (); ++i)
    {
        PClassInfo2 info;
        factory->getClassInfo2 (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            isInstrument = std::strcmp (info.subCategories, "Instrument|Synth") == 0;
    }
    check ("it's registered as a synth", isInstrument);
}

} // namespace Tests
} // namespace RabidPhatt
