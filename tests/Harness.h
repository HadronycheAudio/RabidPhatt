#pragma once

// A minimal VST3 host. It loads the built plug-in and drives it through the same interfaces a real host
// uses, so the tests exercise the real binary rather than individual classes.

#include "Parameters.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstddef>
#include <vector>

namespace RabidPhatt {
namespace Tests {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

using Samples = std::vector<float>;

struct Stereo
{
    Samples left;
    Samples right;
    std::vector<bool> blockSilent; // The silence flags the plug-in set, per block.
};

// Loading the plug-in.
bool loadPlugin (const wchar_t* path);
void unloadPlugin ();
Steinberg::IPluginFactory* getFactory ();

// Results. Measurements print as they go, so that a failure comes with its numbers.
void check (const char* what, bool ok);
int getFailureCount ();

class ParameterChanges;
class EventList;

// One instance of the plug-in, connected and active, as a host would set it up.
class Plug
{
public:
    explicit Plug (double sampleRate = kSampleRate);
    ~Plug ();

    Plug (const Plug&) = delete;
    Plug& operator= (const Plug&) = delete;

    // Applied to both sides, as a host would forward it, and takes effect in the next block.
    void set (ParamId id, double plain);

    // Sent with the next call to render, offset samples from its start.
    void noteOn (int pitch, int offset = 0, float velocity = 0.8f);
    void noteOff (int pitch, int offset = 0);

    Stereo render (double seconds);
    Stereo renderSamples (size_t count);

    // The envelopes and filter set so that a measurement only sees the oscillators: instant attack,
    // full sustain, the filter wide open.
    void bare ();

    std::vector<char> getStateBytes ();

    Steinberg::Vst::IComponent* component { nullptr };
    Steinberg::Vst::IAudioProcessor* processor { nullptr };
    Steinberg::Vst::IEditController* controller { nullptr };
    Steinberg::Vst::ProcessContext* context { nullptr }; // Null means the host gives no transport or tempo.

private:
    ParameterChanges* _changes;
    EventList* _events;
    Steinberg::Vst::IConnectionPoint* _componentConnection { nullptr };
    Steinberg::Vst::IConnectionPoint* _controllerConnection { nullptr };
    double _sampleRate;
};

// Measurements.
Samples slice (const Samples& samples, size_t from, size_t count);
double rms (const Samples& samples, size_t from = 0);
double peak (const Samples& samples, size_t from = 0);
double mean (const Samples& samples, size_t from = 0);
double db (double value);
double toneLevel (const Samples& samples, double hz, size_t from = 0, double sampleRate = kSampleRate); // Hann windowed.
double measureFrequency (const Samples& samples, double sampleRate = kSampleRate);
double frequencyOfNote (double note);
bool isFinite (const Samples& samples);
size_t firstNonZero (const Samples& samples);

} // namespace Tests
} // namespace RabidPhatt
