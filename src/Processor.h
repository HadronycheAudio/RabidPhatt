#pragma once

#include "Parameters.h"
#include "PluginConfig.h"
#include "dsp/Adsr.h"
#include "dsp/Lfo.h"
#include "dsp/LowPassFilter.h"
#include "dsp/NoteStack.h"
#include "dsp/OscillatorBank.h"

#include "hadronyche/dsp/Smoother.h"
#include "hadronyche/vst/ParamStore.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace RabidPhatt {

// The VST3 audio component, which runs the voice described in docs/signal-path.md.
class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (RABIDPHATT_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    double plain (ParamId id) const;
    void applyParameters ();
    void updateLfoTiming (const Steinberg::Vst::ProcessContext* context);
    void followGrid (Steinberg::int32 position);
    void prepareDsp ();

    void handleEvent (const Steinberg::Vst::Event& event);
    void onNoteOn (int pitch, float tuningCents);
    void onNoteOff (int pitch);

    void render (float* left, float* right, Steinberg::int32 count);
    void updateSleep ();

    using Smoother = Hadronyche::Smoother;

    Hadronyche::ParamStore<kParamCount> _params;

    NoteStack _notes;
    bool _hasPlayed { false }; // The first note has nothing to glide from.

    Smoother _pitch; // In MIDI notes, so that a glide moves evenly through the octaves.
    double _renderedPitch { -1.0 };
    double _frequency { 0.0 }; // _pitch as cycles per sample, for _renderedPitch.

    Smoother _motion; // log10 of it, the same space as its knob and the LFO's modulation.
    Smoother _lfoAmount; // How far the LFO can move Motion, in the same units.
    Smoother _lfoFade;
    Smoother _cutoffOctaves; // log2 of the frequency, so that sweeps are even across the range.
    Smoother _resonance;
    Smoother _envAmount;
    Smoother _volume;

    OscillatorBank _leftBank;
    OscillatorBank _rightBank;
    Lfo _lfo;
    double _tempo { 120.0 }; // The last one the host gave, for hosts that don't.
    bool _isLfoOnGrid { false };
    double _lfoGridCycles { 0.0 }; // Where the block starts on the grid, in LFO cycles.
    double _lfoGridCyclesPerSample { 0.0 };
    Adsr _ampEnv;
    Adsr _filterEnv;
    LowPassFilter _filter;

    // With nothing left to hear, the voice stops running, which is also why the oscillators keep their
    // phases between notes, and why a project renders the same way every time.
    bool _isAsleep { true };
    float _blockPeak { 0.0f };
};

} // namespace RabidPhatt
