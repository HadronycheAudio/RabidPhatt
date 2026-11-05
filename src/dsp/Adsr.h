#pragma once

#include <cmath>

namespace RabidPhatt {

// An analogue-style envelope generator, used for both "Amp Env Generator" and "Filter Env Generator".
// Each stage is a capacitor charging towards a target, so a gate arriving during any stage continues
// from the current level rather than jumping, which keeps retriggering free of clicks.
//
// Sustain has no stage of its own: decay settles on the sustain level, and keeps following it if the
// knob moves while a key is held.
class Adsr
{
public:
    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        reset ();
    }

    void reset ()
    {
        _stage = Stage::Idle;
        _value = 0.0;
    }

    // Times are how long a stage takes to get there: attack from silence to full level, decay and
    // release to within 1% of their target. Sustain is 0..1.
    void setParameters (double attackMs, double decayMs, double sustain, double releaseMs)
    {
        _attackCoefficient = coefficient (attackMs, std::log (kAttackTarget / (kAttackTarget - 1.0)));
        _decayCoefficient = coefficient (decayMs, std::log (100.0));
        _releaseCoefficient = coefficient (releaseMs, std::log (100.0));
        _sustain = sustain;
    }

    void gateOn () { _stage = Stage::Attack; }

    void gateOff ()
    {
        if (_stage != Stage::Idle)
            _stage = Stage::Release;
    }

    bool isIdle () const { return _stage == Stage::Idle; }

    double next ()
    {
        switch (_stage)
        {
            case Stage::Idle:
                break;

            case Stage::Attack:
                _value = kAttackTarget + (_value - kAttackTarget) * _attackCoefficient;
                if (_value >= 1.0)
                {
                    _value = 1.0;
                    _stage = Stage::Decay;
                }
                break;

            case Stage::Decay:
                _value = _sustain + (_value - _sustain) * _decayCoefficient;
                break;

            case Stage::Release:
                _value *= _releaseCoefficient;
                if (_value < kSilence)
                {
                    _value = 0.0;
                    _stage = Stage::Idle;
                }
                break;
        }
        return _value;
    }

private:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Release
    };

    // Charging towards a level above full gives the attack its fast-then-slowing analogue shape, and
    // means that it reaches full level in a finite time.
    static constexpr double kAttackTarget = 1.3;
    static constexpr double kSilence = 1e-4; // -80 dB, where a release can stop without being heard.

    double coefficient (double timeMs, double timeConstants) const
    {
        return std::exp (-timeConstants * 1000.0 / (_sampleRate * timeMs));
    }

    double _sampleRate { 44100.0 };
    double _attackCoefficient { 0.0 };
    double _decayCoefficient { 0.0 };
    double _releaseCoefficient { 0.0 };
    double _sustain { 1.0 };
    double _value { 0.0 };
    Stage _stage { Stage::Idle };
};

} // namespace RabidPhatt
