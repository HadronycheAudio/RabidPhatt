#include "Parameters.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace RabidPhatt {

namespace {

constexpr int32 kStateVersion = 1;

// Saved in projects by position, so only ever add to the end of these two, which go together.
const char* const kLfoDivisions[] = {
    "4/1", "2/1", "1/1", "1/2 dotted", "1/2", "1/2 triplet", "1/4 dotted", "1/4",
    "1/4 triplet", "1/8 dotted", "1/8", "1/8 triplet", "1/16 dotted", "1/16", "1/16 triplet", "1/32"
};
constexpr double kLfoDivisionQuarters[] = {
    16.0, 8.0, 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0,
    2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125
};
constexpr int kLfoDivisionCount = 16;
constexpr int kLfoDivisionDefault = 7; // 1/4

// Listed in ParamId order.
const std::array<ParamSpec, kParamCount> kSpecs { {
    { "Motion", "", 0.001, 1.0, 0.25, Curve::Log, 4 },
    { "Glide", "ms", 1.0, 5000.0, 1.0, Curve::Log, 0 },

    { "Amp attack", "ms", 1.0, 10000.0, 5.0, Curve::Log, 0 },
    { "Amp decay", "ms", 1.0, 10000.0, 300.0, Curve::Log, 0 },
    { "Amp sustain", "%", 0.0, 100.0, 80.0, Curve::Linear, 0 },
    { "Amp release", "ms", 1.0, 10000.0, 200.0, Curve::Log, 0 },

    { "Filter cutoff", "Hz", 20.0, 20000.0, 1000.0, Curve::Log, 0 },
    { "Filter resonance", "%", 0.0, 100.0, 20.0, Curve::Linear, 0 },
    { "Filter env amount", "oct", -8.0, 8.0, 3.0, Curve::Linear, 2 },

    { "Filter attack", "ms", 1.0, 10000.0, 5.0, Curve::Log, 0 },
    { "Filter decay", "ms", 1.0, 10000.0, 400.0, Curve::Log, 0 },
    { "Filter sustain", "%", 0.0, 100.0, 30.0, Curve::Linear, 0 },
    { "Filter release", "ms", 1.0, 10000.0, 300.0, Curve::Log, 0 },

    { "Volume", "dB", kVolumeSilenceDb, 6.0, 0.0, Curve::Linear, 1 },

    { "LFO rate", "Hz", 0.05, 20.0, 1.0, Curve::Log, 2 },
    { "LFO amount", "%", 0.0, 100.0, 0.0, Curve::Linear, 0 },
    { "LFO delay", "s", 0.0, 10.0, 0.0, Curve::Squared, 2 },

    { "LFO sync", "", 0.0, 1.0, 0.0, Curve::Toggle, 0 },
    { "LFO division", "", 0.0, kLfoDivisionCount - 1, kLfoDivisionDefault, Curve::List, 0, kLfoDivisions, kLfoDivisionCount },
} };

} // namespace

const ParamSpec& getParamSpec (Vst::ParamID id)
{
    return kSpecs[id];
}

double getLfoDivisionQuarters (int division)
{
    return kLfoDivisionQuarters[std::clamp (division, 0, kLfoDivisionCount - 1)];
}

double normalizedToPlain (const ParamSpec& spec, double normalized)
{
    const double n = std::clamp (normalized, 0.0, 1.0);
    if (spec.curve == Curve::List)
        return std::round (n * (spec.labelCount - 1));
    if (spec.curve == Curve::Toggle)
        return n >= 0.5 ? 1.0 : 0.0;
    if (spec.curve == Curve::Log)
        return spec.min * std::pow (spec.max / spec.min, n);
    if (spec.curve == Curve::Squared)
        return spec.min + (spec.max - spec.min) * n * n;
    return spec.min + (spec.max - spec.min) * n;
}

double plainToNormalized (const ParamSpec& spec, double plain)
{
    const double p = std::clamp (plain, spec.min, spec.max);
    if (spec.curve == Curve::Log)
        return std::log (p / spec.min) / std::log (spec.max / spec.min);
    if (spec.curve == Curve::Squared)
        return std::sqrt ((p - spec.min) / (spec.max - spec.min));
    return (p - spec.min) / (spec.max - spec.min);
}

bool writeParamState (IBStream* stream, const ParamValues& normalizedValues)
{
    IBStreamer streamer (stream, kLittleEndian);
    if (!streamer.writeInt32 (kStateVersion) || !streamer.writeInt32 (kParamCount))
        return false;

    for (int32 id = 0; id < kParamCount; ++id)
    {
        if (!streamer.writeInt32 (id) || !streamer.writeDouble (normalizedValues[static_cast<size_t> (id)]))
            return false;
    }
    return true;
}

bool readParamState (IBStream* stream, const std::function<void (Vst::ParamID, double)>& onValue)
{
    IBStreamer streamer (stream, kLittleEndian);

    int32 version = 0;
    int32 count = 0;
    if (!streamer.readInt32 (version) || version < 1 || version > kStateVersion || !streamer.readInt32 (count))
        return false;

    for (int32 i = 0; i < count; ++i)
    {
        int32 id = 0;
        double value = 0.0;
        if (!streamer.readInt32 (id) || !streamer.readDouble (value))
            return false;

        if (id >= 0 && id < kParamCount && std::isfinite (value))
            onValue (static_cast<Vst::ParamID> (id), std::clamp (value, 0.0, 1.0));
    }
    return true;
}

} // namespace RabidPhatt
