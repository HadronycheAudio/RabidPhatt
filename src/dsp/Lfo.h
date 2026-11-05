#pragma once

#include <algorithm>
#include <cmath>

namespace RabidPhatt {

// A sine LFO, with a delay before its modulation arrives: nothing for the delay's time, and then a fade
// in that takes the same time again. With no delay, it's at full depth straight away.
class Lfo
{
public:
    void prepare (double sampleRate)
    {
        _secondsPerSample = 1.0 / sampleRate;
        _phase = 0.0;
        restartDelay ();
    }

    void setRate (double hz) { _increment = hz * _secondsPerSample; }
    void setDelay (double seconds) { _delay = seconds; }

    // Starting at zero means the note starts with Motion where its knob is.
    void resetPhase () { _phase = 0.0; }
    void restartDelay () { _elapsed = 0.0; }

    // For following the host's musical position. Whole cycles don't matter.
    void setPhase (double cycles) { _phase = cycles - std::floor (cycles); }

    bool hasDelay () const { return _delay > 0.0; }

    double nextSine ()
    {
        const double value = std::sin (2.0 * 3.141592653589793 * _phase);
        _phase += _increment;
        if (_phase >= 1.0)
            _phase -= 1.0;
        return value;
    }

    double nextFade ()
    {
        _elapsed += _secondsPerSample;
        return hasDelay () ? std::clamp (_elapsed / _delay - 1.0, 0.0, 1.0) : 1.0;
    }

private:
    double _secondsPerSample { 0.0 };
    double _increment { 0.0 };
    double _phase { 0.0 };
    double _delay { 0.0 };
    double _elapsed { 0.0 };
};

} // namespace RabidPhatt
