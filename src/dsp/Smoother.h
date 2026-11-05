#pragma once

#include <cmath>

namespace RabidPhatt {

// One-pole smoothing, which removes clicks and zipper noise from parameter changes, and is also what
// the glide runs on. Double precision, because in single precision it stalls just short of its target,
// which would leave a held note slightly out of tune after a glide.
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs)
    {
        _coefficient = std::exp (-1000.0 / (sampleRate * timeMs));
    }

    void setTarget (double target) { _target = target; }

    void snapToTarget () { _current = _target; }

    void snapTo (double value)
    {
        _target = value;
        _current = value;
    }

    double next ()
    {
        _current = _target + (_current - _target) * _coefficient;
        return _current;
    }

private:
    double _coefficient { 0.0 };
    double _target { 0.0 };
    double _current { 0.0 };
};

} // namespace RabidPhatt
