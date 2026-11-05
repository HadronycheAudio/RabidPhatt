// The oscillator banks, as they come out of the plug-in, against the design.

#include "Harness.h"
#include "Tests.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace RabidPhatt {
namespace Tests {

namespace {

constexpr int kA2 = 45; // 110 Hz.
constexpr int kA5 = 81; // 880 Hz, which spreads the rows far enough apart to tell them apart.
constexpr double kMinMotion = 0.001;

// Every multiplier, written out here so that changing the plug-in's own table fails a test.
const double kLeftMultipliers[] = { 0.11, 0.13, 0.15, 0.17, 0.19, 0.21, 0.23, 0.25, 0.27, 0.29, 0.31, 0.33, 0.35, 0.37, 0.39, 0.41 };
const double kRightMultipliers[] = { 0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.42 };

Stereo holdNote (Plug& plug, int note, double motion, double seconds)
{
    plug.bare ();
    plug.set (kMotion, motion);
    plug.noteOn (note);
    return plug.render (seconds);
}

// How far from the note the outermost row is, in Hz, scanning inwards from well beyond where it should
// be. The direction says which side. The step is finer than a 2 s window can resolve, so that a row
// can't fall between two steps.
double findEdge (const Samples& samples, double note, double expectedDistance, double direction, size_t start, double thresholdDb)
{
    for (double distance = expectedDistance * 1.5; distance > 0.0; distance -= 0.25)
    {
        if (db (toneLevel (samples, note + direction * distance, start)) > thresholdDb)
            return distance;
    }
    return 0.0;
}

} // namespace

void testBasePitch ()
{
    for (const int note : { 33, 45, 57, 69, 81 })
    {
        Plug plug;
        const auto out = holdNote (plug, note, kMinMotion, 1.0);
        const auto start = static_cast<size_t> (0.2 * kSampleRate);
        const double left = measureFrequency (slice (out.left, start, out.left.size ()));
        const double right = measureFrequency (slice (out.right, start, out.right.size ()));
        const double expected = frequencyOfNote (note);
        std::printf ("  note %d: %.3f / %.3f Hz, expected %.3f\n", note, left, right, expected);
        check ("converged, both banks play the note", std::abs (left - expected) < expected * 0.002 && std::abs (right - expected) < expected * 0.002);
    }
}

// The bottom of the range: everything is on the note, and cancellation doesn't take the sound away.
void testMotionMinimum ()
{
    Plug plug;
    plug.set (kMotion, 0.0);
    plug.renderSamples (kBlockSize);
    const double normalized = plug.controller->getParamNormalized (kMotion);
    check ("Motion can't go below 0.001", std::abs (plug.controller->normalizedParamToPlain (kMotion, normalized) - kMinMotion) < 1e-9);

    const auto start = static_cast<size_t> (0.2 * kSampleRate);
    const double f = frequencyOfNote (kA5);

    Plug still;
    const auto converged = holdNote (still, kA5, kMinMotion, 2.0);
    const double onNote = toneLevel (converged.left, f, start);
    const double offNote = toneLevel (converged.left, 1.11 * f, start);
    std::printf ("  at 0.001: on the note %.1f dB, where a row would be at Motion 1 %.1f dB\n", db (onNote), db (offNote));
    check ("the rows are all on the note", db (offNote / onNote) < -40.0);

    Plug moving;
    const auto diverged = holdNote (moving, kA5, 1.0, 2.0);
    const double convergedLevel = rms (converged.left, start);
    const double divergedLevel = rms (diverged.left, start);
    std::printf ("  rms at 0.001 %.1f dB, at 1 %.1f dB\n", db (convergedLevel), db (divergedLevel));
    check ("it isn't cancelled away when converged", db (convergedLevel / divergedLevel) > -6.0);
}

// At Motion 1, every row on both sides of the note, and only the rows of the bank in question.
void testBanksRows ()
{
    Plug plug;
    const auto out = holdNote (plug, kA5, 1.0, 2.0);
    const auto start = static_cast<size_t> (0.2 * kSampleRate);
    const double f = frequencyOfNote (kA5);

    constexpr double kFloorDb = -40.0; // A saw's fundamental is about -27 dB.
    constexpr double kAbsentDb = -60.0;

    double quietestLeft = 0.0;
    double loudestRightInLeft = -200.0;
    double quietestRight = 0.0;
    double loudestLeftInRight = -200.0;
    for (size_t i = 0; i < 16; ++i)
    {
        for (const double sign : { 1.0, -1.0 })
        {
            const double leftRatio = 1.0 + sign * kLeftMultipliers[i];
            const double rightRatio = 1.0 + sign * kRightMultipliers[i];
            quietestLeft = std::min (quietestLeft, db (toneLevel (out.left, leftRatio * f, start)));
            quietestRight = std::min (quietestRight, db (toneLevel (out.right, rightRatio * f, start)));

            // Harmonics start at twice the lowest row, so the comparison stays below that.
            if (rightRatio < 1.15)
                loudestRightInLeft = std::max (loudestRightInLeft, db (toneLevel (out.left, rightRatio * f, start)));
            if (leftRatio < 1.15)
                loudestLeftInRight = std::max (loudestLeftInRight, db (toneLevel (out.right, leftRatio * f, start)));
        }
    }
    std::printf ("  left: quietest own row %.1f dB, loudest right-bank frequency %.1f dB\n", quietestLeft, loudestRightInLeft);
    std::printf ("  right: quietest own row %.1f dB, loudest left-bank frequency %.1f dB\n", quietestRight, loudestLeftInRight);
    check ("every left row sounds, above and below the note", quietestLeft > kFloorDb);
    check ("every right row sounds, above and below the note", quietestRight > kFloorDb);
    check ("the left bank doesn't have the right bank's frequencies", loudestRightInLeft < kAbsentDb);
    check ("the right bank doesn't have the left bank's frequencies", loudestLeftInRight < kAbsentDb);
}

// What the control is for: the rows spread from the note in proportion to Motion, both ways.
void testMotionSpread ()
{
    constexpr double kThresholdDb = -45.0;
    const double f = frequencyOfNote (kA5);

    for (const double motion : { 0.02, 0.05, 0.1, 0.25 })
    {
        Plug plug;
        const auto out = holdNote (plug, kA5, motion, 2.2);
        const auto start = static_cast<size_t> (0.2 * kSampleRate);

        const double expected = f * motion * 0.41;
        const double upper = findEdge (out.left, f, expected, 1.0, start, kThresholdDb);
        const double lower = findEdge (out.left, f, expected, -1.0, start, kThresholdDb);
        std::printf ("  Motion %.2f: rows from %.1f to %.1f Hz, expected %.1f to %.1f\n", motion, f - lower, f + upper, f - expected, f + expected);
        check ("the rows reach as far above the note as Motion says", std::abs (upper - expected) < expected * 0.25);
        check ("and as far below it", std::abs (lower - expected) < expected * 0.25);
    }

    // And back again on one note, with nothing else changing.
    Plug plug;
    holdNote (plug, kA5, 1.0, 0.5);
    plug.set (kMotion, kMinMotion);
    plug.render (0.5);
    const auto back = plug.render (2.0);
    const double wide = db (toneLevel (back.left, 1.11 * f, 0) / toneLevel (back.left, f, 0));
    std::printf ("  after bringing Motion back down: where a row was at Motion 1, %.1f dB\n", wide);
    check ("the rows converge again when Motion comes back down", wide < -40.0);
}

void testMotionSmooth ()
{
    const auto largestStep = [] (const Samples& samples) {
        double largest = 0.0;
        for (size_t i = 1; i < samples.size (); ++i)
            largest = std::max (largest, static_cast<double> (std::abs (samples[i] - samples[i - 1])));
        return largest;
    };

    // Worst-case automation: jumping across the whole range, every 100 ms. The yardstick is the larger
    // step at either end when held still.
    Plug stillAtMinimum;
    Plug stillAtOne;
    const double steadyStep = std::max (largestStep (holdNote (stillAtMinimum, 57, kMinMotion, 3.0).left),
                                        largestStep (holdNote (stillAtOne, 57, 1.0, 3.0).left));

    Plug plug;
    holdNote (plug, 57, 1.0, 0.1);
    double jumpingStep = 0.0;
    bool isFiniteThroughout = true;
    for (int i = 0; i < 30; ++i)
    {
        plug.set (kMotion, i % 2 == 0 ? kMinMotion : 1.0);
        const auto out = plug.render (0.1);
        jumpingStep = std::max (jumpingStep, largestStep (out.left));
        isFiniteThroughout = isFiniteThroughout && isFinite (out.left);
    }
    std::printf ("  largest sample-to-sample step: %.3f steady, %.3f with Motion jumping across its range\n", steadyStep, jumpingStep);
    check ("jumping Motion doesn't click", isFiniteThroughout && jumpingStep <= steadyStep * 1.25);

    // A slow sweep, with no knob position where the level suddenly changes.
    for (const int note : { 33, 57, 81 })
    {
        Plug sweep;
        holdNote (sweep, note, kMinMotion, 0.5);

        constexpr int kWindows = 200;
        constexpr double kWindowSeconds = 0.25;

        // The rows beat, so the level moves by itself at any fixed Motion, by quite a lot when they're
        // close together. That's part of the sound, and it's the yardstick: sweeping shouldn't move it
        // noticeably more than that does.
        double steadyChange = 0.0;
        for (const double motion : { 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.5 })
        {
            Plug steady;
            holdNote (steady, note, motion, 0.5);
            double previousLevel = 0.0;
            for (int window = 0; window < 40; ++window)
            {
                const double level = db (rms (steady.render (kWindowSeconds).left));
                if (window > 0)
                    steadyChange = std::max (steadyChange, std::abs (level - previousLevel));
                previousLevel = level;
            }
        }

        double previous = 0.0;
        double largestChange = 0.0;
        double motionOfLargestChange = 0.0;
        double lowest = 200.0;
        double highest = -200.0;
        for (int window = 0; window < kWindows; ++window)
        {
            // Spends as long in the first 1% of the range as the rest.
            const double motion = kMinMotion * std::pow (1.0 / kMinMotion, static_cast<double> (window) / (kWindows - 1));
            sweep.set (kMotion, motion);
            const auto out = sweep.render (kWindowSeconds);
            const double level = db (rms (out.left));
            if (window > 0 && std::abs (level - previous) > largestChange)
            {
                largestChange = std::abs (level - previous);
                motionOfLargestChange = motion;
            }
            previous = level;
            lowest = std::min (lowest, level);
            highest = std::max (highest, level);
        }
        std::printf ("  note %d, sweeping Motion: rms from %.1f to %.1f dB, at most %.1f dB between 250 ms windows (at Motion %.3f), against %.1f dB at fixed Motion\n",
                     note, lowest, highest, largestChange, motionOfLargestChange, steadyChange);
        check ("the sweep moves the level no more than beating does, give or take 1 dB", largestChange < steadyChange + 1.0);
    }
}

namespace {

// How much is sounding between 8% and 12% above the note, for 100 ms from a given time. With Motion at
// the middle of its knob, the rows only reach there when the LFO has pushed Motion well up.
double outerRows (const Samples& samples, double seconds)
{
    const double f = frequencyOfNote (kA5);
    const auto window = slice (samples, static_cast<size_t> (seconds * kSampleRate), static_cast<size_t> (0.1 * kSampleRate));
    double sum = 0.0;
    for (double ratio = 1.08; ratio <= 1.12; ratio += 0.005)
        sum += toneLevel (window, f * ratio);
    return sum;
}

// Motion at the middle of its knob, and an LFO that moves it by a decade each way, between 0.0032 and
// 0.32. Its peak is a quarter of a cycle in, because the first note starts the LFO at zero.
Stereo playWithLfo (Plug& plug, double rate, double amount, double delay, double seconds)
{
    plug.bare ();
    plug.set (kLfoRate, rate);
    plug.set (kLfoAmount, amount);
    plug.set (kLfoDelay, delay);
    plug.set (kMotion, 0.0316);
    plug.noteOn (kA5);
    return plug.render (seconds);
}

constexpr double kOneDecade = 100.0 / 3.0;

} // namespace

void testLfoModulates ()
{
    Plug plug;
    const auto out = playWithLfo (plug, 1.0, kOneDecade, 0.0, 2.5);

    // The window is 100 ms long, so these centre on 0.25 s and 0.75 s of a 1 s cycle.
    const double top = outerRows (out.left, 0.2);
    const double bottom = outerRows (out.left, 0.7);
    const double nextTop = outerRows (out.left, 1.2);
    std::printf ("  rows far from the note: %.1f dB at the LFO's peak, %.1f dB at its trough, %.1f dB at the next peak\n", db (top), db (bottom), db (nextTop));
    check ("with no delay, the LFO spreads the rows straight away", db (top / bottom) > 20.0);
    check ("and again a cycle later", db (nextTop / bottom) > 20.0);

    Plug off;
    const auto still = playWithLfo (off, 1.0, 0.0, 0.0, 1.0);
    std::printf ("  with no amount, at the same moment: %.1f dB\n", db (outerRows (still.left, 0.2)));
    check ("with no amount, there's no modulation", db (top / outerRows (still.left, 0.2)) > 20.0);

    Plug left;
    Plug right;
    const auto stereo = playWithLfo (left, 1.0, kOneDecade, 0.0, 1.0);
    check ("both banks are modulated", db (outerRows (stereo.right, 0.2) / outerRows (stereo.right, 0.7)) > 20.0);
}

void testLfoRate ()
{
    Plug plug;
    const auto out = playWithLfo (plug, 2.0, kOneDecade, 0.0, 1.5);

    // At 2 Hz, the peaks are at 0.125 s and 0.625 s, and the trough between them at 0.375 s.
    const double top = outerRows (out.left, 0.075);
    const double bottom = outerRows (out.left, 0.325);
    const double nextTop = outerRows (out.left, 0.575);
    std::printf ("  at 2 Hz: %.1f dB, %.1f dB, %.1f dB\n", db (top), db (bottom), db (nextTop));
    check ("the rate sets the speed", db (top / bottom) > 20.0 && db (nextTop / bottom) > 20.0);
}

void testLfoDelay ()
{
    // Nothing for 0.75 s, and a fade in over the next 0.75 s. The peaks at 1 Hz are at 0.25, 1.25 and
    // 2.25 s, so the first is during the delay, the second near the end of the fade and the third after.
    Plug plug;
    const auto out = playWithLfo (plug, 1.0, kOneDecade, 0.75, 3.0);
    const double during = outerRows (out.left, 0.2);
    const double late = outerRows (out.left, 2.2);
    std::printf ("  rows far from the note: %.1f dB during the delay, %.1f dB once faded in\n", db (during), db (late));
    check ("there's no modulation during the delay", db (late / during) > 20.0);

    // The depth grows gradually: at 1.25 s, it's 67% of the way through the fade, so Motion is lower than
    // at the full depth, which shows as the rows reaching less far.
    Plug full;
    const auto noDelay = playWithLfo (full, 1.0, kOneDecade, 0.0, 2.0);
    const double partway = outerRows (out.left, 1.2);
    const double atFullDepth = outerRows (noDelay.left, 1.2);
    std::printf ("  a peak partway through the fade %.1f dB, and with no delay at all %.1f dB\n", db (partway), db (atFullDepth));
    check ("it fades in instead of switching on", partway < atFullDepth * 0.8);

    // A new note after silence starts the delay again.
    plug.noteOff (kA5);
    plug.render (3.0);
    plug.noteOn (kA5);
    const auto again = plug.render (3.0);
    const double duringAgain = outerRows (again.left, 0.2);
    const double lateAgain = outerRows (again.left, 2.2);
    std::printf ("  next note: %.1f dB during the delay, %.1f dB once faded in\n", db (duringAgain), db (lateAgain));
    check ("a new note starts the delay again", db (lateAgain / duringAgain) > 20.0);

    // Playing legato doesn't.
    Plug legato;
    legato.bare ();
    legato.set (kLfoRate, 1.0);
    legato.set (kLfoAmount, kOneDecade);
    legato.set (kLfoDelay, 0.75);
    legato.set (kMotion, 0.0316);
    legato.noteOn (kA5 - 5);
    legato.render (2.0);
    legato.noteOn (kA5);
    legato.noteOff (kA5 - 5);
    // The LFO is 2 s in, so its next peak is at 0.25 s from here.
    const auto joined = legato.render (0.5);
    const double legatoLevel = outerRows (joined.left, 0.2);
    std::printf ("  after a legato note, at the next peak: %.1f dB\n", db (legatoLevel));
    check ("a legato note doesn't restart the delay", db (legatoLevel / during) > 20.0);
}

void testLfoSmooth ()
{
    const auto largestStep = [] (const Samples& samples) {
        double largest = 0.0;
        for (size_t i = 1; i < samples.size (); ++i)
            largest = std::max (largest, static_cast<double> (std::abs (samples[i] - samples[i - 1])));
        return largest;
    };

    Plug stillAtMinimum;
    Plug stillAtOne;
    const double steadyStep = std::max (largestStep (holdNote (stillAtMinimum, 57, kMinMotion, 3.0).left),
                                        largestStep (holdNote (stillAtOne, 57, 1.0, 3.0).left));

    // As hard as it goes: the fastest rate, the full amount, and pushing against both ends of the range.
    Plug plug;
    plug.bare ();
    plug.set (kLfoRate, 20.0);
    plug.set (kLfoAmount, 100.0);
    plug.set (kMotion, 0.0316);
    plug.noteOn (57);
    const auto out = plug.render (3.0);
    std::printf ("  largest sample-to-sample step: %.3f still, %.3f with the LFO flat out\n", steadyStep, largestStep (out.left));
    check ("the LFO doesn't click, even at its limits", isFinite (out.left) && largestStep (out.left) <= steadyStep * 1.25);

    // Retriggering during a release, where the delay restarts while the modulation is at full depth.
    Plug retrigger;
    retrigger.bare ();
    retrigger.set (kAmpRelease, 2000);
    retrigger.set (kLfoAmount, 100.0);
    retrigger.set (kLfoDelay, 0.1);
    retrigger.set (kMotion, 0.0316);
    retrigger.noteOn (57);
    retrigger.render (1.0);
    retrigger.noteOff (57);
    retrigger.render (0.1);
    retrigger.noteOn (57);
    const auto restarted = retrigger.render (1.0);
    std::printf ("  retriggered: %.3f\n", largestStep (restarted.left));
    check ("restarting the delay during a release doesn't click", isFinite (restarted.left) && largestStep (restarted.left) <= steadyStep * 1.25);
}

namespace {

// A host's transport, stopped at the start of the song unless a test moves it.
Steinberg::Vst::ProcessContext makeTransport (double tempo)
{
    using Context = Steinberg::Vst::ProcessContext;
    Context context {};
    context.state = Context::kProjectTimeMusicValid | Context::kTempoValid;
    context.sampleRate = kSampleRate;
    context.tempo = tempo;
    return context;
}

// An LFO synced to a division, set up like playWithLfo, but with the note left for the test to play.
void setUpSyncedLfo (Plug& plug, int division)
{
    plug.bare ();
    plug.set (kLfoSync, 1);
    plug.set (kLfoDivision, division);
    plug.set (kLfoAmount, kOneDecade);
    plug.set (kMotion, 0.0316);
}

constexpr int kDivision1_2 = 4;
constexpr int kDivision1_4Triplet = 8;
constexpr int kDivision1_4 = 7;
constexpr int kDivision1_8 = 10;
constexpr int kDivision1_4Dotted = 6;

// A short window, centred on a moment, for faster cycles.
double outerRowsAt (const Samples& samples, double seconds)
{
    const double f = frequencyOfNote (kA5);
    const auto window = slice (samples, static_cast<size_t> ((seconds - 0.02) * kSampleRate), static_cast<size_t> (0.04 * kSampleRate));
    double sum = 0.0;
    for (double ratio = 1.08; ratio <= 1.12; ratio += 0.005)
        sum += toneLevel (window, f * ratio);
    return sum;
}

} // namespace

// With a stopped transport, a note out of silence starts the sine at zero, so a cycle of length T peaks at
// T / 4 and bottoms out at 3T / 4.
void testLfoSyncTempo ()
{
    for (const double tempo : { 60.0, 120.0, 90.0 })
    {
        auto transport = makeTransport (tempo);
        Plug plug;
        plug.context = &transport;
        setUpSyncedLfo (plug, kDivision1_4);
        plug.noteOn (kA5);
        const auto out = plug.render (2.0);

        const double cycle = 60.0 / tempo;
        const double top = outerRowsAt (out.left, cycle * 0.25);
        const double bottom = outerRowsAt (out.left, cycle * 0.75);
        const double nextTop = outerRowsAt (out.left, cycle * 1.25);
        std::printf ("  1/4 at %.0f BPM: %.1f dB, %.1f dB, %.1f dB\n", tempo, db (top), db (bottom), db (nextTop));
        check ("the cycle follows the tempo", db (top / bottom) > 20.0 && db (nextTop / bottom) > 20.0);
    }

    // A tempo change while the note is held. 120 BPM for 1 s is 2 cycles, then 60 BPM makes it 1 s a cycle.
    auto transport = makeTransport (120.0);
    Plug plug;
    plug.context = &transport;
    setUpSyncedLfo (plug, kDivision1_4);
    plug.noteOn (kA5);
    plug.render (1.0);
    transport.tempo = 60.0;
    const auto slower = plug.render (1.0);
    const double top = outerRowsAt (slower.left, 0.25);
    const double bottom = outerRowsAt (slower.left, 0.75);
    std::printf ("  after dropping to 60 BPM: %.1f dB, %.1f dB\n", db (top), db (bottom));
    check ("a tempo change takes effect on a held note", db (top / bottom) > 20.0);

    // No tempo from the host: it plays at 120 BPM.
    Plug noHost;
    setUpSyncedLfo (noHost, kDivision1_4);
    noHost.noteOn (kA5);
    const auto out = noHost.render (1.0);
    const double hostlessTop = outerRowsAt (out.left, 0.125);
    const double hostlessBottom = outerRowsAt (out.left, 0.375);
    std::printf ("  with no tempo from the host: %.1f dB, %.1f dB\n", db (hostlessTop), db (hostlessBottom));
    check ("without a tempo, it assumes 120 BPM", db (hostlessTop / hostlessBottom) > 20.0);
}

void testLfoSyncDivisions ()
{
    struct Case
    {
        int division;
        const char* name;
        double quarters;
    };
    const Case kCases[] = {
        { kDivision1_2, "1/2", 2.0 },
        { kDivision1_4Dotted, "1/4 dotted", 1.5 },
        { kDivision1_4Triplet, "1/4 triplet", 2.0 / 3.0 },
        { kDivision1_8, "1/8", 0.5 },
    };

    for (const auto& item : kCases)
    {
        auto transport = makeTransport (60.0);
        Plug plug;
        plug.context = &transport;
        setUpSyncedLfo (plug, item.division);
        plug.noteOn (kA5);
        const auto out = plug.render (item.quarters * 2.5);

        const double top = outerRowsAt (out.left, item.quarters * 0.25);
        const double bottom = outerRowsAt (out.left, item.quarters * 0.75);
        const double nextTop = outerRowsAt (out.left, item.quarters * 1.25);
        std::printf ("  %s at 60 BPM, a cycle of %.3f s: %.1f dB, %.1f dB, %.1f dB\n", item.name, item.quarters, db (top), db (bottom), db (nextTop));
        check ("the division sets the length of the cycle", db (top / bottom) > 15.0 && db (nextTop / bottom) > 15.0);
    }
}

void testLfoSyncGrid ()
{
    // With a playing transport, the sine is set by the song's position, whenever the note starts. At
    // 120 BPM with 1/4, a cycle is a beat: a quarter of the way through a beat is the peak.
    const auto playFrom = [] (double quarters) {
        auto transport = makeTransport (120.0);
        transport.state |= Steinberg::Vst::ProcessContext::kPlaying;
        transport.projectTimeMusic = quarters;
        Plug plug;
        plug.context = &transport;
        setUpSyncedLfo (plug, kDivision1_4);
        plug.noteOn (kA5);
        const auto out = plug.render (1.0);
        plug.context = nullptr;
        return out;
    };

    const auto onPeak = playFrom (16.25);
    const auto onTrough = playFrom (16.75);

    // 0.25 s is half a cycle on.
    const double peakFromPeak = outerRowsAt (onPeak.left, 0.5);
    const double troughFromPeak = outerRowsAt (onPeak.left, 0.25);
    const double peakFromTrough = outerRowsAt (onTrough.left, 0.25);
    const double troughFromTrough = outerRowsAt (onTrough.left, 0.5);
    std::printf ("  from 16.25 quarters, peak %.1f dB, trough %.1f dB; from 16.75, peak %.1f dB, trough %.1f dB\n", db (peakFromPeak), db (troughFromPeak), db (peakFromTrough), db (troughFromTrough));
    check ("the sine follows the song's position", db (peakFromPeak / troughFromPeak) > 20.0);
    check ("and starting elsewhere in the bar lands in the right place", db (peakFromTrough / troughFromTrough) > 20.0);

    // A locate lands on the new position's phase straight away.
    auto transport = makeTransport (120.0);
    transport.state |= Steinberg::Vst::ProcessContext::kPlaying;
    Plug plug;
    plug.context = &transport;
    setUpSyncedLfo (plug, kDivision1_4);
    plug.noteOn (kA5);
    plug.render (0.5);
    transport.projectTimeMusic = 100.25;
    const auto located = plug.render (0.5);
    const double peak = outerRowsAt (located.left, 0.48); // As close to a whole cycle on as the window fits.
    const double trough = outerRowsAt (located.left, 0.25);
    std::printf ("  after locating: peak %.1f dB, trough %.1f dB\n", db (peak), db (trough));
    check ("a locate moves the sine to the new position", db (peak / trough) > 15.0);

    // A long time later it hasn't drifted. 600 quarters is 5 minutes, which is 14.4 million samples.
    auto longPlay = makeTransport (120.0);
    longPlay.state |= Steinberg::Vst::ProcessContext::kPlaying;
    Plug later;
    later.context = &longPlay;
    setUpSyncedLfo (later, kDivision1_4);
    later.noteOn (kA5);
    later.render (300.0);
    const double position = longPlay.projectTimeMusic;
    const double toNextPeak = (std::ceil (position - 0.25) + 0.25 - position) * 0.5;
    const auto end = later.render (toNextPeak + 1.0);
    const double laterPeak = outerRowsAt (end.left, toNextPeak + 0.5);
    const double laterTrough = outerRowsAt (end.left, toNextPeak + 0.25);
    std::printf ("  after 5 minutes: peak %.1f dB, trough %.1f dB\n", db (laterPeak), db (laterTrough));
    check ("it's still on the grid after 5 minutes", db (laterPeak / laterTrough) > 20.0);
}

void testLfoSyncFree ()
{
    // With Sync off, the tempo and the song's position make no difference.
    auto transport = makeTransport (200.0);
    transport.state |= Steinberg::Vst::ProcessContext::kPlaying;
    transport.projectTimeMusic = 16.75;
    Plug plug;
    plug.context = &transport;
    const auto out = playWithLfo (plug, 1.0, kOneDecade, 0.0, 1.5);
    const double top = outerRowsAt (out.left, 0.25);
    const double bottom = outerRowsAt (out.left, 0.75);
    std::printf ("  free running at 1 Hz, in a song at 200 BPM: %.1f dB, %.1f dB\n", db (top), db (bottom));
    check ("the free running LFO ignores the song", db (top / bottom) > 20.0);

    // A division with a transport, then back off: it returns to the rate knob.
    auto running = makeTransport (120.0);
    Plug toggled;
    toggled.context = &running;
    setUpSyncedLfo (toggled, kDivision1_8);
    toggled.noteOn (kA5);
    toggled.render (0.2);
    toggled.set (kLfoSync, 0);
    toggled.set (kLfoRate, 1.0);
    const auto back = toggled.render (2.0);
    bool isFiniteThroughout = isFinite (back.left);
    double highest = 0.0;
    double lowest = 1e9;
    for (double t = 0.1; t < 1.8; t += 0.05)
    {
        const double level = outerRowsAt (back.left, t);
        highest = std::max (highest, level);
        lowest = std::min (lowest, level);
    }
    std::printf ("  after turning Sync off: from %.1f to %.1f dB\n", db (lowest), db (highest));
    check ("switching back to free running keeps it going", isFiniteThroughout && db (highest / lowest) > 20.0);
}

void testLevelFlat ()
{
    for (const int note : { 33, 57, 81 })
    {
        double lowest = 200.0;
        double highest = -200.0;
        double highestPeak = -200.0;
        for (const double motion : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1.0 })
        {
            Plug plug;
            const auto out = holdNote (plug, note, motion, 20.0); // Long enough for the rows to drift a fair way.
            const auto start = static_cast<size_t> (1.0 * kSampleRate);
            const double level = db (rms (out.left, start));
            std::printf ("    note %d, Motion %.3f: rms %.1f dB, peak %.1f dB\n", note, motion, level, db (peak (out.left, start)));
            lowest = std::min (lowest, level);
            highest = std::max (highest, level);
            highestPeak = std::max (highestPeak, db (peak (out.left, start)));
        }
        std::printf ("  note %d: rms from %.1f to %.1f dB, highest peak %.1f dB\n", note, lowest, highest, highestPeak);
        check ("the level stays within 6 dB across Motion", highest - lowest < 6.0);
        check ("peaks stay under full scale", highestPeak < 0.0);
    }
}

void testStereoWidth ()
{
    for (const double motion : { kMinMotion, 0.5 })
    {
        Plug plug;
        const auto out = holdNote (plug, kA2, motion, 1.0);
        const auto start = static_cast<size_t> (0.2 * kSampleRate);

        double both = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (size_t i = start; i < out.left.size (); ++i)
        {
            both += static_cast<double> (out.left[i]) * out.right[i];
            leftEnergy += static_cast<double> (out.left[i]) * out.left[i];
            rightEnergy += static_cast<double> (out.right[i]) * out.right[i];
        }
        const double correlation = both / std::sqrt (leftEnergy * rightEnergy);
        std::printf ("  Motion %.3f: correlation %.3f\n", motion, correlation);
        check ("left and right are largely independent", std::abs (correlation) < 0.5);
    }
}

void testDcOffset ()
{
    for (const double motion : { kMinMotion, 0.25, 1.0 })
    {
        Plug plug;
        const auto out = holdNote (plug, kA2, motion, 10.0);
        const auto start = static_cast<size_t> (0.4 * kSampleRate);
        std::printf ("  Motion %.3f: DC %.5f / %.5f\n", motion, mean (out.left, start), mean (out.right, start));
        check ("there's no DC at the output", std::abs (mean (out.left, start)) < 0.002 && std::abs (mean (out.right, start)) < 0.002);
    }
}

void testAliasing ()
{
    // A 3 kHz note has harmonics at multiples of 3 kHz, and an alias falls between them. These are where
    // the harmonics above Nyquist land at 48 kHz.
    Plug plug;
    plug.bare ();
    plug.set (kMotion, kMinMotion);
    const double note = 69.0 + 12.0 * std::log2 (3000.0 / 440.0);
    const int nearest = static_cast<int> (std::lround (note));
    const double actual = frequencyOfNote (nearest);
    plug.noteOn (nearest);
    const auto out = plug.render (1.0);
    const auto start = static_cast<size_t> (0.2 * kSampleRate);

    const double fundamental = toneLevel (out.left, actual, start);
    double worst = -200.0;
    double worstBelow10k = -200.0;
    for (int harmonic = 8; harmonic <= 40; ++harmonic)
    {
        if (actual * harmonic < kSampleRate / 2.0)
            continue;

        double folded = std::fmod (actual * harmonic, kSampleRate);
        if (folded > kSampleRate / 2.0)
            folded = kSampleRate - folded;

        const double level = db (toneLevel (out.left, folded, start) / fundamental);
        worst = std::max (worst, level);
        if (folded < 10000.0)
            worstBelow10k = std::max (worstBelow10k, level);
    }
    std::printf ("  %.0f Hz: worst alias %.1f dB, worst below 10 kHz %.1f dB, relative to the fundamental\n", actual, worst, worstBelow10k);
    check ("aliases are at least 35 dB down", worst < -35.0);
    check ("aliases below 10 kHz are at least 50 dB down", worstBelow10k < -50.0);
}

void testTopOfKeyboard ()
{
    for (const int note : { 96, 108, 120, 127 })
    {
        Plug plug;
        const auto out = holdNote (plug, note, 1.0, 0.5);
        const auto start = static_cast<size_t> (0.1 * kSampleRate);
        std::printf ("  note %d: peak %.3f\n", note, peak (out.left, start));
        check ("the output stays sane", isFinite (out.left) && isFinite (out.right) && peak (out.left, start) < 2.0);
    }

    // Oscillators fade out as they approach a quarter of the sample rate, instead of aliasing.
    Plug plug;
    const auto out = holdNote (plug, 127, kMinMotion, 0.5);
    std::printf ("  note 127 at the minimum Motion: peak %.5f\n", peak (out.left, static_cast<size_t> (0.1 * kSampleRate)));
    check ("oscillators above a quarter of the sample rate are silent", peak (out.left, static_cast<size_t> (0.1 * kSampleRate)) < 1e-3);
}

} // namespace Tests
} // namespace RabidPhatt
