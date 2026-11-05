#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace RabidPhatt {

// The "Filter" block: a stereo 24 dB/octave low-pass, made of two topology-preserving state-variable
// stages, which stay stable however fast the envelope sweeps them. Only the second stage resonates.
class LowPassFilter
{
public:
    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        reset ();
    }

    void reset () { _channels = {}; }

    // Light enough to call for every sample. Resonance is 0..1.
    void setCoefficients (double frequency, double resonance)
    {
        const double limited = std::clamp (frequency, 10.0, _sampleRate * 0.49);
        const double g = std::tan (3.141592653589793 * limited / _sampleRate);

        // At zero resonance these two Qs make a 4-pole Butterworth response.
        _first = makeStage (g, kFirstQ);
        _second = makeStage (g, kSecondQ * std::pow (kMaxQ / kSecondQ, resonance));
    }

    float process (float input, size_t channel)
    {
        auto& stages = _channels[channel];
        return processStage (processStage (input, _first, stages[0]), _second, stages[1]);
    }

private:
    static constexpr double kFirstQ = 0.5412;
    static constexpr double kSecondQ = 1.3066;
    static constexpr double kMaxQ = 20.0;

    struct Coefficients
    {
        float a1 { 0.0f };
        float a2 { 0.0f };
        float a3 { 0.0f };
    };

    struct StageState
    {
        float ic1 { 0.0f };
        float ic2 { 0.0f };
    };

    static Coefficients makeStage (double g, double q)
    {
        const double a1 = 1.0 / (1.0 + g * (g + 1.0 / q));
        const double a2 = g * a1;
        return { static_cast<float> (a1), static_cast<float> (a2), static_cast<float> (g * a2) };
    }

    static float processStage (float input, const Coefficients& c, StageState& state)
    {
        const float v3 = input - state.ic2;
        const float v1 = c.a1 * state.ic1 + c.a2 * v3;
        const float v2 = state.ic2 + c.a2 * state.ic1 + c.a3 * v3;
        state.ic1 = 2.0f * v1 - state.ic1;
        state.ic2 = 2.0f * v2 - state.ic2;
        return v2;
    }

    double _sampleRate { 44100.0 };
    Coefficients _first;
    Coefficients _second;
    std::array<std::array<StageState, 2>, 2> _channels {};
};

} // namespace RabidPhatt
