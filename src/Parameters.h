#pragma once

#include "hadronyche/dsp/Gain.h"
#include "hadronyche/vst/ParamSpec.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <functional>

namespace RabidPhatt {

// Parameter IDs are saved in projects and automation: never renumber them, only append.
enum ParamId : Steinberg::Vst::ParamID
{
    kMotion,
    kGlide,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kFilterAttack,
    kFilterDecay,
    kFilterSustain,
    kFilterRelease,
    kVolume,
    kLfoRate,
    kLfoAmount,
    kLfoDelay,
    kLfoSync,
    kLfoDivision,
    kParamCount
};

using Hadronyche::Curve;
using Hadronyche::ParamSpec;
using Hadronyche::kVolumeSilenceDb;
using Hadronyche::normalizedToPlain;
using Hadronyche::plainToNormalized;

const ParamSpec& getParamSpec (Steinberg::Vst::ParamID id);

// How long one LFO cycle lasts for an entry of kLfoDivision, in quarter notes.
double getLfoDivisionQuarters (int division);

using ParamValues = std::array<double, kParamCount>;

bool writeParamState (Steinberg::IBStream* stream, const ParamValues& normalizedValues);

// Calls onValue (id, normalized) for every known parameter in the stream.
bool readParamState (Steinberg::IBStream* stream, const std::function<void (Steinberg::Vst::ParamID, double)>& onValue);

} // namespace RabidPhatt
