#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace RabidPhatt {

// "Left Oscillator Bank" and "Right Oscillator Bank": 33 sawtooth oscillators and a signal add.
//
// The first row always plays the incoming frequency. Every other row plays
// frequency x (1 + Motion x multiplier), so that they all converge on the note as Motion falls, and
// diverge from it, above and below, as Motion rises. Those rows alternate between inverted and not,
// starting with inverted.
class OscillatorBank
{
public:
    static constexpr size_t kMovingRows = 32;

    enum class Side
    {
        Left,
        Right
    };

    void prepare (Side side)
    {
        _multipliers = side == Side::Left ? kLeftMultipliers : kRightMultipliers;

        // A fixed seed, so that a project renders the same way every time. The banks start differently
        // from each other, for stereo width. Nothing else is done about cancellation, which comes and
        // goes as the rows drift, and is part of the sound.
        uint32_t random = side == Side::Left ? 0x9E3779B9u : 0x85EBCA6Bu;
        const auto nextRandom = [&random] {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            return random / 4294967296.0;
        };

        _basePhase = nextRandom ();
        for (auto& phase : _phases)
            phase = nextRandom ();
    }

    // frequency is in cycles per sample.
    float process (double frequency, double motion)
    {
        double sum = saw (_basePhase, frequency);

        const double spread = frequency * motion;
        for (size_t i = 0; i < kMovingRows; ++i)
        {
            const double value = saw (_phases[i], frequency + spread * _multipliers[i]);
            sum += i % 2 == 0 ? -value : value;
        }

        return static_cast<float> (sum * kBankGain);
    }

private:
    // Keeps the peaks of all the rows playing together under full scale.
    static constexpr double kBankGain = 0.07;

    // Above this, wraps come too close together for the anti-aliasing to deal with them. A sawtooth this
    // high is nearly a sine, so fading it out only loses a little at the very top of the keyboard.
    static constexpr double kFadeStart = 0.2;
    static constexpr double kFadeEnd = 0.25;

    // These are the diagrams' multipliers divided by 10, which the owner chose after trying them in Cubase.
    static constexpr std::array<double, kMovingRows> kLeftMultipliers {
        0.11, 0.13, 0.15, 0.17, 0.19, 0.21, 0.23, 0.25, 0.27, 0.29, 0.31, 0.33, 0.35, 0.37, 0.39, 0.41,
        -0.11, -0.13, -0.15, -0.17, -0.19, -0.21, -0.23, -0.25, -0.27, -0.29, -0.31, -0.33, -0.35, -0.37, -0.39, -0.41
    };

    // The last row is -0.42: the -4.1 in the diagram is a known typo for -4.2.
    static constexpr std::array<double, kMovingRows> kRightMultipliers {
        0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.42,
        -0.12, -0.14, -0.16, -0.18, -0.20, -0.22, -0.24, -0.26, -0.28, -0.30, -0.32, -0.34, -0.36, -0.38, -0.40, -0.42
    };

    // A band-limited step: the integral of a cubic B-spline, minus the ideal step it replaces. x is the
    // distance from the wrap in samples, and the correction reaches zero 2 samples away.
    static double stepCorrection (double x)
    {
        if (x < -1.0)
        {
            const double a = x + 2.0;
            return a * a * a * a / 24.0;
        }
        if (x < 0.0)
            return 0.5 + x * (2.0 / 3.0 - x * x * (1.0 / 3.0 + x / 8.0));
        if (x < 1.0)
            return -0.5 + x * (2.0 / 3.0 - x * x * (1.0 / 3.0 - x / 8.0));

        const double a = 2.0 - x;
        return -a * a * a * a / 24.0;
    }

    static double saw (double& phase, double increment)
    {
        phase += increment;
        if (phase >= 1.0)
            phase -= std::floor (phase);

        if (increment >= kFadeEnd)
            return 0.0;

        double value = 2.0 * phase - 1.0;

        // Each wrap steps the output down by 2. Comparing first avoids dividing for most samples.
        const double reach = 2.0 * increment;
        if (phase < reach)
            value -= 2.0 * stepCorrection (phase / increment);
        if (1.0 - phase < reach)
            value -= 2.0 * stepCorrection ((phase - 1.0) / increment);

        if (increment > kFadeStart)
            value *= (kFadeEnd - increment) / (kFadeEnd - kFadeStart);
        return value;
    }

    std::array<double, kMovingRows> _multipliers {};
    std::array<double, kMovingRows> _phases {};
    double _basePhase { 0.0 };
};

} // namespace RabidPhatt
