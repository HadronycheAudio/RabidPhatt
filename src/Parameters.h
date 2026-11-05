#pragma once

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

enum class Curve
{
    Linear,
    Log,
    Squared, // Can reach zero, unlike Log, and still gives short times more of the knob's travel.
    List,
    Toggle
};

struct ParamSpec
{
    const char* title;
    const char* units;
    double min;
    double max;
    double defaultValue; // In plain units, like min and max.
    Curve curve;
    int precision;
    const char* const* labels; // Curve::List only.
    int labelCount;
};

const ParamSpec& getParamSpec (Steinberg::Vst::ParamID id);

// Conversions between the host's 0..1 values and the units in ParamSpec.
double normalizedToPlain (const ParamSpec& spec, double normalized);
double plainToNormalized (const ParamSpec& spec, double plain);

// How long one LFO cycle lasts for an entry of kLfoDivision, in quarter notes.
double getLfoDivisionQuarters (int division);

// At or below this, kVolume is silent.
constexpr double kVolumeSilenceDb = -60.0;

using ParamValues = std::array<double, kParamCount>;

// Parameters are saved as ID and value pairs, so that newer versions can add parameters and still
// read older projects.
bool writeParamState (Steinberg::IBStream* stream, const ParamValues& normalizedValues);

// Calls onValue (id, normalized) for every known parameter in the stream.
bool readParamState (Steinberg::IBStream* stream, const std::function<void (Steinberg::Vst::ParamID, double)>& onValue);

} // namespace RabidPhatt
