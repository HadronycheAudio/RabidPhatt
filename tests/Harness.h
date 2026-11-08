#pragma once

// The test host from HadronycheCore, and what this plug-in's tests add to it.

#include "ParamIds.h"

#include "hadronyche/testhost/Host.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace RabidPhatt {
namespace Tests {

using namespace Hadronyche::TestHost;

class EventList;

class Plug : public PlugBase
{
public:
    explicit Plug (double sampleRate = kSampleRate);
    ~Plug ();

    // Sent with the next call to render, offset samples from its start.
    void noteOn (int pitch, int offset = 0, float velocity = 0.8f);
    void noteOff (int pitch, int offset = 0);

    Stereo render (double seconds);
    Stereo renderSamples (size_t count);

    // The envelopes and filter set so that a measurement only sees the oscillators: instant attack,
    // full sustain, the filter wide open.
    void bare ();

private:
    std::unique_ptr<EventList> _events;
};

// Measurements.
double mean (const Samples& samples, size_t from = 0);
double measureFrequency (const Samples& samples, double sampleRate = kSampleRate);
double frequencyOfNote (double note);
size_t firstNonZero (const Samples& samples);

} // namespace Tests
} // namespace RabidPhatt
