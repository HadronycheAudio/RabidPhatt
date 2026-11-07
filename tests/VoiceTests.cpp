// Everything around the oscillator banks: notes, glide, envelopes, filter, volume and state.

#include "Harness.h"
#include "Tests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;

namespace RabidPhatt {
namespace Tests {

namespace {

constexpr int kA2 = 45; // 110 Hz.
constexpr int kA3 = 57; // 220 Hz.
constexpr int kA4 = 69; // 440 Hz.

// All the oscillators converged on the note, with nothing shaping them. That's periodic at the note's
void converged (Plug& plug)
{
    plug.bare ();
    plug.set (kMotion, 0.0);
    plug.set (kAmpRelease, 1);
}

double pitchOf (const Samples& samples)
{
    return measureFrequency (samples);
}

} // namespace

void testLastNotePriority ()
{
    Plug plug;
    converged (plug);

    plug.noteOn (kA2);
    plug.render (0.2);
    const double first = pitchOf (plug.render (0.2).left);

    plug.noteOn (kA4);
    plug.render (0.05);
    const double second = pitchOf (plug.render (0.2).left);

    plug.noteOff (kA4);
    plug.render (0.05);
    const double back = pitchOf (plug.render (0.2).left);

    std::printf ("  %.1f, then %.1f, then back to %.1f Hz\n", first, second, back);
    check ("the first note plays", std::abs (first - 110.0) < 0.5);
    check ("a new note takes over", std::abs (second - 440.0) < 1.0);
    check ("releasing it falls back to the held note", std::abs (back - 110.0) < 0.5);

    plug.noteOff (kA3); // Was never pressed.
    check ("releasing an unheld note changes nothing", rms (plug.render (0.2).left) > 0.01);

    // Just the filter's tail is left after this, so it isn't quite zero yet.
    plug.noteOff (kA2);
    plug.render (0.05);
    check ("releasing the last note closes the gate", db (rms (plug.render (0.1).left)) < -80.0);
}

void testLegato ()
{
    Plug plug;
    plug.bare ();
    plug.set (kMotion, 0.0);
    plug.set (kAmpAttack, 500);
    plug.set (kAmpSustain, 100);

    plug.noteOn (kA3);
    const auto before = plug.render (0.1);
    plug.noteOn (kA3 + 2); // Would restart the attack if it retriggered.
    const auto after = plug.render (0.1);

    const double levelBefore = peak (slice (before.left, before.left.size () - 2400, 2400));
    const double levelAfter = peak (slice (after.left, 0, 2400));
    std::printf ("  envelope level around the second note: %.4f then %.4f\n", levelBefore, levelAfter);
    check ("an overlapping note doesn't drop the level", levelAfter >= levelBefore * 0.9);

    // Velocity 0 is a note-off.
    Plug other;
    converged (other);
    other.noteOn (kA3);
    other.render (0.1);
    other.noteOn (kA3, 0, 0.0f);
    other.render (0.05);
    check ("a note-on with no velocity releases the note", db (rms (other.render (0.1).left)) < -80.0);
}

void testGlide ()
{
    constexpr double kGlideMs = 500.0;

    Plug plug;
    converged (plug);
    plug.set (kGlide, kGlideMs);

    plug.noteOn (kA3);
    const auto first = plug.render (0.2);
    check ("the first note doesn't glide from anywhere", std::abs (pitchOf (slice (first.left, 2400, 7200)) - 220.0) < 1.0);

    plug.noteOn (kA3 + 12);
    plug.noteOff (kA3);
    const auto gliding = plug.render (1.5);

    const auto pitchAt = [&] (double seconds) {
        return pitchOf (slice (gliding.left, static_cast<size_t> (seconds * kSampleRate), 2400));
    };
    const double atStart = pitchAt (0.0);
    const double atHalf = pitchAt (0.25);
    const double atEnd = pitchAt (kGlideMs / 1000.0);
    const double settled = pitchOf (slice (gliding.left, static_cast<size_t> (1.2 * kSampleRate), 14400));
    std::printf ("  220 to 440 Hz in %.0f ms: %.1f at the start, %.1f at 250 ms, %.1f at 500 ms, %.2f settled\n", kGlideMs, atStart, atHalf, atEnd, settled);
    check ("it starts near the old pitch", atStart < 260.0);
    check ("it's on the way at half the time", atHalf > 300.0 && atHalf < 420.0);
    check ("it's nearly there after the glide time", atEnd > 410.0);
    check ("it settles exactly in tune", std::abs (settled - 440.0) < 0.5);

    // With the shortest setting, the glide is effectively off.
    Plug quick;
    converged (quick);
    quick.noteOn (kA3);
    quick.render (0.1);
    quick.noteOn (kA3 + 12);
    const auto jump = quick.render (0.1);
    const double jumped = pitchOf (slice (jump.left, 240, 2400));
    std::printf ("  shortest glide: %.1f Hz after 5 ms\n", jumped);
    check ("the shortest glide is effectively a jump", std::abs (jumped - 440.0) < 8.0);
}

void testAmpEnvelope ()
{
    Plug plug;
    plug.bare ();
    plug.set (kMotion, 0.0);
    plug.set (kAmpAttack, 100);
    plug.set (kAmpDecay, 100);
    plug.set (kAmpSustain, 50);
    plug.set (kAmpRelease, 100);
    plug.noteOn (kA3);
    const auto held = plug.render (0.5);

    // The same notes with full sustain, so that the wave is the same and only the envelope differs.
    Plug full;
    full.bare ();
    full.set (kMotion, 0.0);
    full.set (kAmpAttack, 100);
    full.set (kAmpDecay, 100);
    full.set (kAmpSustain, 100);
    full.noteOn (kA3);
    const auto reference = full.render (0.5);

    plug.noteOff (kA3);
    const auto released = plug.render (1.5); // Long enough for the filter envelope's release too.

    const auto levelAt = [] (const Samples& samples, double seconds) {
        return rms (slice (samples, static_cast<size_t> (seconds * kSampleRate), 480));
    };
    const double attackStart = levelAt (held.left, 0.002);
    const double atPeak = levelAt (reference.left, 0.4);
    const double sustained = levelAt (held.left, 0.4);
    const double endOfRelease = levelAt (released.left, 0.11);
    std::printf ("  rms: %.4f early, %.4f with full sustain, %.4f with 50%% sustain, %.5f after the release\n", attackStart, atPeak, sustained, endOfRelease);
    check ("the attack starts low", attackStart < atPeak * 0.2);
    check ("sustain sits at 50% of full level", std::abs (sustained / atPeak - 0.5) < 0.05);
    check ("the release gets to 1% in its time", endOfRelease < sustained * 0.02);
    check ("after the release there's silence", rms (slice (released.left, released.left.size () - 2400, 2400)) == 0.0);
}

void testSilenceFlags ()
{
    Plug plug;
    converged (plug);
    const auto idle = plug.render (0.05);
    check ("no notes: silent, and flagged", rms (idle.left) == 0.0 && std::all_of (idle.blockSilent.begin (), idle.blockSilent.end (), [] (bool flag) { return flag; }));

    plug.noteOn (kA3);
    const auto playing = plug.render (0.05);
    check ("a note isn't flagged as silence", std::none_of (playing.blockSilent.begin (), playing.blockSilent.end (), [] (bool flag) { return flag; }));

    plug.noteOff (kA3);
    plug.render (2.0);
    const auto after = plug.render (0.05);
    check ("after the note ends, silence is flagged again", std::all_of (after.blockSilent.begin (), after.blockSilent.end (), [] (bool flag) { return flag; }));
}

void testSampleAccurate ()
{
    for (const int offset : { 0, 100, 255, 256, 1000 })
    {
        Plug plug;
        converged (plug);
        plug.render (0.01);
        plug.noteOn (kA3, offset);
        const auto out = plug.renderSamples (2000);
        const auto first = firstNonZero (out.left);
        std::printf ("  note at sample %d: sound from %zu\n", offset, first);
        check ("the note starts on its sample", first >= static_cast<size_t> (offset) && first <= static_cast<size_t> (offset) + 1);
    }
}

void testFilterSlope ()
{
    const auto measure = [] (double cutoff) {
        Plug plug;
        converged (plug);
        plug.set (kFilterCutoff, cutoff);
        plug.noteOn (kA2);
        const auto out = plug.render (1.0);
        return out.left;
    };

    constexpr size_t kStart = 9600;
    const auto open = measure (20000.0);
    const auto closed = measure (880.0);

    // 880 Hz is the 8th harmonic of 110 Hz. Two and three octaves above, a 4-pole filter takes 24 dB per
    // octave.
    const auto drop = [&] (double hz) { return db (toneLevel (closed, hz, kStart) / toneLevel (open, hz, kStart)); };
    const double below = drop (110.0);
    const double oneUp = drop (1760.0);
    const double twoUp = drop (3520.0);
    std::printf ("  relative to an open filter: at 110 Hz %.1f dB, at 1760 Hz %.1f dB, at 3520 Hz %.1f dB\n", below, oneUp, twoUp);
    check ("the passband is left alone", std::abs (below) < 0.5);
    check ("one octave above the cutoff is about 24 dB down", oneUp < -20.0 && oneUp > -28.0);
    check ("each further octave drops about 24 dB", twoUp - oneUp < -20.0 && twoUp - oneUp > -28.0);

    // Resonance lifts what's at the cutoff.
    Plug plug;
    converged (plug);
    plug.set (kFilterCutoff, 880.0);
    plug.set (kFilterResonance, 100.0);
    plug.noteOn (kA2);
    const auto resonant = plug.render (1.0);
    const double lift = db (toneLevel (resonant.left, 880.0, kStart) / toneLevel (closed, 880.0, kStart));
    std::printf ("  full resonance lifts the cutoff frequency by %.1f dB\n", lift);
    check ("resonance lifts the cutoff by over 18 dB", lift > 18.0);
    check ("and stays stable", isFinite (resonant.left) && peak (resonant.left) < 4.0);
}

void testFilterEnvelope ()
{
    // Several harmonics of the note, because with many saws at different phases, any one of them can
    // happen to be in a null.
    const auto highs = [] (const Samples& samples) {
        double sum = 0.0;
        for (int harmonic = 30; harmonic < 36; ++harmonic)
            sum += toneLevel (samples, harmonic * 110.0, 0);
        return sum;
    };

    Plug up;
    converged (up);
    up.set (kFilterCutoff, 200.0);
    up.set (kFilterEnvAmount, 5.0);
    up.set (kFilterAttack, 1);
    up.set (kFilterDecay, 200);
    up.set (kFilterSustain, 0);
    up.noteOn (kA2);
    const auto swept = up.render (1.0);
    const double early = highs (slice (swept.left, 480, 4800));
    const double late = highs (slice (swept.left, 38400, 4800));
    std::printf ("  3520 Hz with a positive amount: %.1f dB early, %.1f dB once decayed\n", db (early), db (late));
    check ("the envelope opens the filter, and it closes again", db (early / late) > 40.0);

    Plug down;
    converged (down);
    down.set (kFilterCutoff, 12000.0);
    down.set (kFilterEnvAmount, -6.0);
    down.set (kFilterAttack, 1);
    down.set (kFilterDecay, 2000);
    down.set (kFilterSustain, 0);
    down.noteOn (kA2);
    const auto inverted = down.render (1.5);
    const double earlyDown = highs (slice (inverted.left, 480, 4800));
    const double lateDown = highs (slice (inverted.left, 62400, 4800));
    std::printf ("  3520 Hz with a negative amount: %.1f dB early, %.1f dB once decayed\n", db (earlyDown), db (lateDown));
    check ("a negative amount closes it at the envelope's peak, and it opens as the envelope falls", db (lateDown / earlyDown) > 20.0);

    // The filter envelope has its own gate timing, separate from the amplifier's.
    Plug both;
    converged (both);
    both.set (kAmpRelease, 1000);
    both.set (kFilterCutoff, 200.0);
    both.set (kFilterEnvAmount, 5.0);
    both.set (kFilterAttack, 1);
    both.set (kFilterSustain, 100);
    both.set (kFilterRelease, 50);
    both.noteOn (kA2);
    both.render (0.1);
    both.noteOff (kA2);
    const auto tail = both.render (0.3);
    const double earlyTail = highs (slice (tail.left, 0, 960));
    const double lateTail = highs (slice (tail.left, 9600, 4800));
    std::printf ("  3520 Hz in the release: %.1f dB, then %.1f dB\n", db (earlyTail), db (lateTail));
    check ("the two envelopes release separately", db (earlyTail / lateTail) > 30.0);
}

void testFilterTail ()
{
    // A closed amplifier leaves a resonant filter ringing, which must not be cut short by going to sleep.
    Plug plug;
    converged (plug);
    plug.set (kFilterCutoff, 500.0);
    plug.set (kFilterResonance, 100.0);
    plug.noteOn (kA2);
    plug.render (0.2);
    plug.noteOff (kA2);
    const auto tail = plug.render (1.0);
    const double level = rms (slice (tail.left, 1200, 240));
    std::printf ("  25 ms after the amplifier closes: %.1f dB\n", db (level));
    check ("the filter rings on after the amplifier closes", level > 1e-4);
    check ("and then goes silent", rms (slice (tail.left, tail.left.size () - 2400, 2400)) == 0.0);
}

void testVolume ()
{
    const auto levelAt = [] (double volume) {
        Plug plug;
        plug.bare ();
        plug.set (kMotion, 0.0);
        plug.set (kVolume, volume);
        plug.noteOn (kA3);
        const auto out = plug.render (0.5);
        return rms (out.left, 12000);
    };

    const double unity = levelAt (0.0);
    const double down = levelAt (-12.0);
    const double up = levelAt (6.0);
    std::printf ("  0 dB: %.4f, -12 dB: %.1f dB, +6 dB: %.1f dB\n", unity, db (down / unity), db (up / unity));
    check ("-12 dB is 12 dB down", std::abs (db (down / unity) + 12.0) < 0.1);
    check ("+6 dB is 6 dB up", std::abs (db (up / unity) - 6.0) < 0.1);
    check ("the bottom of the range is silence", levelAt (-60.0) == 0.0);
}

// Not a pass or fail on any one number so much as a record of the gain staging.
void testLevels ()
{
    for (const double motion : { 0.0, 0.05, 0.25, 0.5, 1.0 })
    {
        double highestPeak = 0.0;
        double level = 0.0;
        for (const int note : { 33, 45, 57, 69 })
        {
            Plug plug;
            plug.bare ();
            plug.set (kMotion, motion);
            plug.noteOn (note);
            const auto out = plug.render (3.0);
            highestPeak = std::max ({ highestPeak, peak (out.left, 9600), peak (out.right, 9600) });
            level = std::max (level, rms (out.left, 9600));
        }
        std::printf ("  Motion %.2f, filter open: rms %.1f dB, peak %.1f dB\n", motion, db (level), db (highestPeak));
        check ("peaks stay under full scale", db (highestPeak) < 0.0);
    }
}

void testDeterminism ()
{
    const auto play = [] {
        Plug plug;
        plug.noteOn (kA3);
        auto first = plug.render (0.3);
        plug.noteOff (kA3);
        plug.noteOn (kA3 + 5, 4800);
        auto second = plug.render (0.5);
        first.left.insert (first.left.end (), second.left.begin (), second.left.end ());
        return first.left;
    };
    check ("two renders of the same notes are identical", play () == play ());
}

void testExtremeSettings ()
{
    // Every parameter at each end, at both ends of the keyboard.
    bool allFinite = true;
    double loudest = 0.0;
    for (const double normalized : { 0.0, 1.0 })
    {
        for (const int note : { 0, 127 })
        {
            Plug plug;
            for (Vst::ParamID id = 0; id < kParamCount; ++id)
                plug.set (static_cast<ParamId> (id), plug.controller->normalizedParamToPlain (id, normalized));
            plug.noteOn (note);
            const auto held = plug.render (1.0);
            plug.noteOff (note);
            const auto released = plug.render (0.5);
            allFinite = allFinite && isFinite (held.left) && isFinite (held.right) && isFinite (released.left);
            loudest = std::max ({ loudest, peak (held.left), peak (held.right) });
        }
    }
    std::printf ("  loudest %.1f dB\n", db (loudest));
    check ("nothing blows up", allFinite);

    // Fast changes of everything while playing.
    Plug plug;
    plug.noteOn (kA3);
    bool stayedSane = true;
    for (int i = 0; i < 200; ++i)
    {
        const double x = 0.5 + 0.5 * std::sin (i * 1.7);
        plug.set (kMotion, x);
        plug.set (kFilterCutoff, 20.0 * std::pow (1000.0, 0.5 + 0.5 * std::cos (i * 2.3)));
        plug.set (kFilterResonance, 100.0 * x);
        plug.set (kFilterEnvAmount, 8.0 * std::sin (i * 0.9));
        const auto out = plug.renderSamples (64);
        stayedSane = stayedSane && isFinite (out.left) && peak (out.left) < 20.0;
    }
    check ("the filter stays stable under fast modulation", stayedSane);

    // Other sample rates.
    for (const double rate : { 44100.0, 96000.0, 192000.0 })
    {
        Plug other (rate);
        other.bare ();
        other.set (kMotion, 0.0);
        other.noteOn (kA3);
        const auto out = other.render (1.0);
        const double hz = measureFrequency (slice (out.left, static_cast<size_t> (rate * 0.2), out.left.size ()), rate);
        std::printf ("  at %.0f Hz: %.2f Hz\n", rate, hz);
        check ("the pitch is right at this sample rate", std::abs (hz - 220.0) < 0.5);
    }
}

void testParameterState ()
{
    Plug first;
    first.set (kMotion, 0.7);
    first.set (kGlide, 123.0);
    first.set (kFilterEnvAmount, -2.0);
    first.renderSamples (kBlockSize);
    const auto saved = first.getStateBytes ();

    Plug second;
    check ("the processor and the controller accept it", second.setStateBytes (saved));

    check ("Motion came back", std::abs (second.controller->getParamNormalized (kMotion) - first.controller->getParamNormalized (kMotion)) < 1e-9);
    check ("Glide came back", std::abs (second.controller->getParamNormalized (kGlide) - first.controller->getParamNormalized (kGlide)) < 1e-9);
    check ("the saved bytes are identical", second.getStateBytes () == saved);
}

void testParameterText ()
{
    Plug plug;
    const auto text = [&] (ParamId id, double normalized) { return paramText (plug.controller, id, normalized); };

    check ("Motion reads in its own units", text (kMotion, 0.0) == "0.0010" && text (kMotion, 1.0) == "1.0000");
    check ("the bottom of Volume reads -inf", text (kVolume, 0.0) == "-inf");
    check ("LFO sync reads Off and On", text (kLfoSync, 0.0) == "Off" && text (kLfoSync, 1.0) == "On");

    // These are saved by position, so their order can't change.
    const char* const kDivisions[] = { "4/1", "2/1", "1/1", "1/2 dotted", "1/2", "1/2 triplet", "1/4 dotted", "1/4",
                                       "1/4 triplet", "1/8 dotted", "1/8", "1/8 triplet", "1/16 dotted", "1/16", "1/16 triplet", "1/32" };
    bool divisionsMatch = true;
    for (int i = 0; i < 16; ++i)
        divisionsMatch = divisionsMatch && text (kLfoDivision, i / 15.0) == kDivisions[i];
    check ("the LFO divisions are in their saved order", divisionsMatch);

    bool roundTrips = true;
    for (Vst::ParamID id = 0; id < kParamCount; ++id)
    {
        const double normalized = 0.37;
        double parsed = -1.0;
        roundTrips = roundTrips && paramFromText (plug.controller, id, text (static_cast<ParamId> (id), normalized).c_str (), parsed)
                     && text (static_cast<ParamId> (id), parsed) == text (static_cast<ParamId> (id), normalized);
    }
    check ("every parameter's text converts back", roundTrips);
}

void measureCpu ()
{
    Plug plug;
    plug.bare ();
    plug.set (kMotion, 1.0);
    plug.set (kFilterCutoff, 2000.0);
    plug.noteOn (kA3);
    plug.render (0.1);

    constexpr double kSeconds = 30.0;
    const auto start = std::chrono::steady_clock::now ();
    const auto out = plug.render (kSeconds);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now () - start;
    std::printf ("%.1f s of audio in %.3f s: %.2f%% of one core at 48 kHz (peak %.2f)\n", kSeconds, elapsed.count (), 100.0 * elapsed.count () / kSeconds, peak (out.left));
}

} // namespace Tests
} // namespace RabidPhatt
