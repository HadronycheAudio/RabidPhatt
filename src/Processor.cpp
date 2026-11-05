#include "Processor.h"

#include "Controller.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

using namespace Steinberg;

namespace RabidPhatt {

namespace {

constexpr double kSmoothingMs = 20.0;

// Motion goes from 0.001 to 1, which is 3 decades. 100% of the LFO's amount moves it by that much each way.
constexpr double kMotionRangeLog10 = 3.0;

// A filter tail below this is treated as finished. It's about -120 dB.
constexpr float kSilentPeak = 1e-6f;

// The glide's time is how long it takes to cover 95% of the interval, which is 3 time constants.
constexpr double kGlideTimeConstants = 3.0;

// Stops very small values in the filters from slowing the CPU down.
class FlushDenormals
{
public:
#if defined(_M_X64) || defined(__x86_64__)
    FlushDenormals () : _previous (_mm_getcsr ()) { _mm_setcsr (_previous | 0x8040); }
    ~FlushDenormals () { _mm_setcsr (_previous); }

private:
    unsigned int _previous;
#endif
};

double volumeToGain (double db)
{
    return db <= kVolumeSilenceDb ? 0.0 : std::pow (10.0, db / 20.0);
}

} // namespace

Processor::Processor ()
{
    setControllerClass (Controller::getClassFUID ());
    processContextRequirements.needTransportState ().needProjectTimeMusic ().needTempo ();

    for (Vst::ParamID id = 0; id < kParamCount; ++id)
    {
        const auto& spec = getParamSpec (id);
        _params[id] = plainToNormalized (spec, spec.defaultValue);
    }
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    addAudioOutput (STR16 ("Stereo Out"), Vst::SpeakerArr::kStereo);
    addEventInput (STR16 ("MIDI In"), 16); // Notes are taken from every channel.
    return kResultOk;
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
        prepareDsp ();

    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
                                                  Vst::SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 && outputs[0] == Vst::SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);

    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

// Releases can be long and there's no input to tell the host when they end, so the host shouldn't cut
// the plug-in off. The silence flags tell it when there's nothing to hear.
uint32 PLUGIN_API Processor::getTailSamples ()
{
    return Vst::kInfiniteTail;
}

double Processor::plain (ParamId id) const
{
    return normalizedToPlain (getParamSpec (id), _params[id]);
}

void Processor::readParameterChanges (Vst::IParameterChanges* changes)
{
    if (!changes)
        return;

    const int32 count = changes->getParameterCount ();
    for (int32 i = 0; i < count; ++i)
    {
        auto* queue = changes->getParameterData (i);
        if (!queue || queue->getParameterId () >= kParamCount || queue->getPointCount () == 0)
            continue;

        // Smoothing covers the rest, so only the last value in each block is needed.
        int32 offset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint (queue->getPointCount () - 1, offset, value) == kResultTrue)
            _params[queue->getParameterId ()] = value;
    }
}

void Processor::applyParameters ()
{
    _pitch.prepare (processSetup.sampleRate, plain (kGlide) / kGlideTimeConstants);

    _motion.setTarget (std::log10 (plain (kMotion)));
    _lfoAmount.setTarget (plain (kLfoAmount) / 100.0 * kMotionRangeLog10);
    _lfo.setDelay (plain (kLfoDelay));
    _cutoffOctaves.setTarget (std::log2 (plain (kFilterCutoff)));
    _resonance.setTarget (plain (kFilterResonance) / 100.0);
    _envAmount.setTarget (plain (kFilterEnvAmount));
    _volume.setTarget (volumeToGain (plain (kVolume)));

    _ampEnv.setParameters (plain (kAmpAttack), plain (kAmpDecay), plain (kAmpSustain) / 100.0, plain (kAmpRelease));
    _filterEnv.setParameters (plain (kFilterAttack), plain (kFilterDecay), plain (kFilterSustain) / 100.0, plain (kFilterRelease));
}

void Processor::updateLfoTiming (const Vst::ProcessContext* context)
{
    _isLfoOnGrid = false;

    if (plain (kLfoSync) < 0.5)
    {
        _lfo.setRate (plain (kLfoRate));
        return;
    }

    if (context && (context->state & Vst::ProcessContext::kTempoValid) != 0 && context->tempo > 0.0)
        _tempo = context->tempo;

    const double quarters = getLfoDivisionQuarters (static_cast<int> (plain (kLfoDivision)));
    const double hz = _tempo / 60.0 / quarters;
    _lfo.setRate (hz);

    // Calculated from the position rather than accumulated, so that it can't drift off the grid. Without
    // a playing transport there's no position to follow, and the sine just runs at the tempo.
    if (context && (context->state & Vst::ProcessContext::kPlaying) != 0 && (context->state & Vst::ProcessContext::kProjectTimeMusicValid) != 0)
    {
        _isLfoOnGrid = true;
        _lfoGridCycles = context->projectTimeMusic / quarters;
        _lfoGridCyclesPerSample = hz / processSetup.sampleRate;
    }
}

void Processor::followGrid (int32 position)
{
    if (_isLfoOnGrid)
        _lfo.setPhase (_lfoGridCycles + position * _lfoGridCyclesPerSample);
}

void Processor::prepareDsp ()
{
    const double sampleRate = processSetup.sampleRate;

    for (auto* smoother : { &_motion, &_lfoAmount, &_lfoFade, &_cutoffOctaves, &_resonance, &_envAmount, &_volume })
        smoother->prepare (sampleRate, kSmoothingMs);

    _motion.snapTo (std::log10 (plain (kMotion)));
    _lfoAmount.snapTo (plain (kLfoAmount) / 100.0 * kMotionRangeLog10);
    _lfoFade.snapTo (0.0);
    _cutoffOctaves.snapTo (std::log2 (plain (kFilterCutoff)));
    _resonance.snapTo (plain (kFilterResonance) / 100.0);
    _envAmount.snapTo (plain (kFilterEnvAmount));
    _volume.snapTo (volumeToGain (plain (kVolume)));

    _leftBank.prepare (OscillatorBank::Side::Left);
    _rightBank.prepare (OscillatorBank::Side::Right);
    _lfo.prepare (sampleRate);
    _ampEnv.prepare (sampleRate);
    _filterEnv.prepare (sampleRate);
    _filter.prepare (sampleRate);

    _notes.clear ();
    _hasPlayed = false;
    _renderedPitch = -1.0;
    _isAsleep = true;

    applyParameters ();
}

void Processor::onNoteOn (int pitch, float tuningCents)
{
    const bool wasGateOpen = !_notes.isEmpty ();
    _notes.noteOn (pitch, tuningCents);
    if (_notes.isEmpty ())
        return;

    const double target = _notes.getCurrentPitch ();
    if (_hasPlayed)
        _pitch.setTarget (target);
    else
        _pitch.snapTo (target);
    _hasPlayed = true;

    // There's one gate: playing legato moves the pitch and leaves the envelopes and LFO delay running.
    if (!wasGateOpen)
    {
        _lfo.restartDelay ();

        // A note out of silence starts the same way every time. One that catches the previous note's
        // release can't, because the LFO jumping would move every oscillator's pitch at once.
        if (_isAsleep)
        {
            _lfo.resetPhase ();
            _lfoFade.snapTo (_lfo.hasDelay () ? 0.0 : 1.0);
        }

        _ampEnv.gateOn ();
        _filterEnv.gateOn ();
        _isAsleep = false;
    }
}

void Processor::onNoteOff (int pitch)
{
    const bool wasGateOpen = !_notes.isEmpty ();
    _notes.noteOff (pitch);

    if (!_notes.isEmpty ())
    {
        _pitch.setTarget (_notes.getCurrentPitch ());
    }
    else if (wasGateOpen)
    {
        _ampEnv.gateOff ();
        _filterEnv.gateOff ();
    }
}

void Processor::handleEvent (const Vst::Event& event)
{
    if (event.type == Vst::Event::kNoteOnEvent)
    {
        // A note-on with no velocity is the MIDI way of saying note-off.
        if (event.noteOn.velocity > 0.0f)
            onNoteOn (event.noteOn.pitch, event.noteOn.tuning);
        else
            onNoteOff (event.noteOn.pitch);
    }
    else if (event.type == Vst::Event::kNoteOffEvent)
    {
        onNoteOff (event.noteOff.pitch);
    }
}

void Processor::render (float* left, float* right, int32 count)
{
    if (_isAsleep)
    {
        std::fill_n (left, count, 0.0f);
        std::fill_n (right, count, 0.0f);
        return;
    }

    const double sampleRate = processSetup.sampleRate;

    for (int32 i = 0; i < count; ++i)
    {
        const double pitch = _pitch.next ();
        if (pitch != _renderedPitch)
        {
            _renderedPitch = pitch;
            _frequency = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0) / sampleRate;
        }

        // In log10, where the LFO moves Motion like turning its knob, and clamped to the knob's range.
        _lfoFade.setTarget (_lfo.nextFade ());
        const double modulation = _lfoAmount.next () * _lfoFade.next () * _lfo.nextSine ();
        const double motion = std::pow (10.0, std::clamp (_motion.next () + modulation, -kMotionRangeLog10, 0.0));
        const bool isAmpOpen = !_ampEnv.isIdle ();
        const float amp = static_cast<float> (_ampEnv.next ());

        // A closed amplifier means nothing to hear from the oscillators, so they pause.
        float l = 0.0f;
        float r = 0.0f;
        if (isAmpOpen)
        {
            l = _leftBank.process (_frequency, motion) * amp;
            r = _rightBank.process (_frequency, motion) * amp;
        }

        const double cutoffOctaves = _cutoffOctaves.next () + _envAmount.next () * _filterEnv.next ();
        _filter.setCoefficients (std::pow (2.0, cutoffOctaves), _resonance.next ());
        l = _filter.process (l, 0);
        r = _filter.process (r, 1);

        _blockPeak = std::max ({ _blockPeak, std::abs (l), std::abs (r) });

        const float volume = static_cast<float> (_volume.next ());
        left[i] = l * volume;
        right[i] = r * volume;
    }
}

void Processor::updateSleep ()
{
    if (!_isAsleep && _ampEnv.isIdle () && _filterEnv.isIdle () && _blockPeak < kSilentPeak)
    {
        _isAsleep = true;
        _filter.reset ();
    }
    _blockPeak = 0.0f;
}

tresult PLUGIN_API Processor::process (Vst::ProcessData& data)
{
    readParameterChanges (data.inputParameterChanges);

    if (data.numOutputs == 0 || data.numSamples == 0)
        return kResultOk;

    auto& output = data.outputs[0];
    if (output.numChannels != 2)
    {
        for (int32 c = 0; c < output.numChannels; ++c)
            std::memset (output.channelBuffers32[c], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
        return kResultOk;
    }

    const FlushDenormals flushDenormals;
    applyParameters ();
    updateLfoTiming (data.processContext);

    const bool startedAsleep = _isAsleep;

    // Nobody can hear parameters move while asleep, and the next note shouldn't ramp from their old
    // values either, even if the change arrives in the same block as the note.
    if (_isAsleep)
    {
        _pitch.snapToTarget ();
        for (auto* smoother : { &_motion, &_lfoAmount, &_cutoffOctaves, &_resonance, &_envAmount, &_volume })
            smoother->snapToTarget ();
    }

    // Renders up to each event, so that notes start on the exact sample.
    float* left = output.channelBuffers32[0];
    float* right = output.channelBuffers32[1];
    const int32 eventCount = data.inputEvents ? data.inputEvents->getEventCount () : 0;
    int32 position = 0;
    for (int32 e = 0; e < eventCount; ++e)
    {
        Vst::Event event {};
        if (data.inputEvents->getEvent (e, event) != kResultOk)
            continue;

        const int32 offset = std::clamp (event.sampleOffset, position, data.numSamples);
        followGrid (position);
        render (left + position, right + position, offset - position);
        position = offset;
        handleEvent (event);
    }
    followGrid (position);
    render (left + position, right + position, data.numSamples - position);

    updateSleep ();

    output.silenceFlags = startedAsleep && _isAsleep ? 3 : 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    const bool ok = readParamState (state, [this] (Vst::ParamID id, double value) { _params[id] = value; });
    return ok ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    ParamValues values {};
    for (size_t id = 0; id < kParamCount; ++id)
        values[id] = _params[id];

    return writeParamState (state, values) ? kResultOk : kResultFalse;
}

} // namespace RabidPhatt
