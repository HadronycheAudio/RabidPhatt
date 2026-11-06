#pragma once

#include "hadronyche/dsp/Svf.h"

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
        const double g = Hadronyche::svfWarp (frequency, _sampleRate);

        // At zero resonance these two Qs make a 4-pole Butterworth response.
        _first = Hadronyche::svfCoefficients (g, kFirstQ);
        _second = Hadronyche::svfCoefficients (g, kSecondQ * std::pow (kMaxQ / kSecondQ, resonance));
    }

    float process (float input, size_t channel)
    {
        auto& stages = _channels[channel];
        return Hadronyche::svfTick (Hadronyche::svfTick (input, _first, stages[0]).v2, _second, stages[1]).v2;
    }

private:
    static constexpr double kFirstQ = 0.5412;
    static constexpr double kSecondQ = 1.3066;
    static constexpr double kMaxQ = 20.0;

    double _sampleRate { 44100.0 };
    Hadronyche::SvfCoefficients _first;
    Hadronyche::SvfCoefficients _second;
    std::array<std::array<Hadronyche::SvfState, 2>, 2> _channels {};
};

} // namespace RabidPhatt
