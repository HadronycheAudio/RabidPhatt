// Usage: RabidPhattTests <path to the plug-in's DLL> [test name]
//        RabidPhattTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Runner.h"

#include <iterator>

using namespace RabidPhatt::Tests;

namespace {

const Test kTests[] = {
    { L"class-ids", testClassIds },
    { L"parameter-ids", testParameterIds },
    { L"parameter-ranges", testParameterRanges },
    { L"saved-format", testSavedFormat },
    { L"buses", testBuses },

    { L"base-pitch", testBasePitch },
    { L"motion-minimum", testMotionMinimum },
    { L"banks-rows", testBanksRows },
    { L"motion-spread", testMotionSpread },
    { L"motion-smooth", testMotionSmooth },
    { L"lfo-modulates", testLfoModulates },
    { L"lfo-rate", testLfoRate },
    { L"lfo-delay", testLfoDelay },
    { L"lfo-smooth", testLfoSmooth },
    { L"lfo-sync-tempo", testLfoSyncTempo },
    { L"lfo-sync-divisions", testLfoSyncDivisions },
    { L"lfo-sync-grid", testLfoSyncGrid },
    { L"lfo-sync-free", testLfoSyncFree },
    { L"level-flat", testLevelFlat },
    { L"stereo-width", testStereoWidth },
    { L"dc-offset", testDcOffset },
    { L"aliasing", testAliasing },
    { L"top-of-keyboard", testTopOfKeyboard },

    { L"last-note-priority", testLastNotePriority },
    { L"legato", testLegato },
    { L"glide", testGlide },
    { L"amp-envelope", testAmpEnvelope },
    { L"silence-flags", testSilenceFlags },
    { L"sample-accurate", testSampleAccurate },
    { L"filter-slope", testFilterSlope },
    { L"filter-envelope", testFilterEnvelope },
    { L"filter-tail", testFilterTail },
    { L"volume", testVolume },
    { L"levels", testLevels },
    { L"determinism", testDeterminism },
    { L"extreme-settings", testExtremeSettings },
    { L"parameter-state", testParameterState },
    { L"parameter-text", testParameterText },

#if defined(_WIN32)
    { L"panel", testPanel },
#endif
};

} // namespace

HADRONYCHE_TEST_MAIN
{
    return runTests (argc, argv, { "RabidPhattTests", kTests, std::size (kTests), measureCpu });
}
