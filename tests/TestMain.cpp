// Usage: RabidPhattTests <path to the plug-in's DLL> [test name]
//        RabidPhattTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include <cstdio>
#include <cwchar>

using namespace RabidPhatt::Tests;

namespace {

struct Test
{
    const wchar_t* name;
    void (*function) ();
};

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

    { L"panel", testPanel },
};

} // namespace

int wmain (int argc, wchar_t** argv)
{
    if (argc >= 2 && std::wcscmp (argv[1], L"--list") == 0)
    {
        for (const auto& test : kTests)
            std::wprintf (L"%ls\n", test.name);
        std::wprintf (L"cpu\n");
        return 0;
    }

    if (argc < 2)
    {
        std::printf ("Usage: RabidPhattTests <path to the plug-in's DLL> [test name]\n");
        return 2;
    }

    if (!loadPlugin (argv[1]))
    {
        std::wprintf (L"Could not load the plug-in: %ls\n", argv[1]);
        return 2;
    }

    const wchar_t* filter = argc >= 3 ? argv[2] : nullptr;
    bool foundTest = false;

    if (filter && std::wcscmp (filter, L"cpu") == 0)
    {
        foundTest = true;
        measureCpu ();
    }

    for (const auto& test : kTests)
    {
        if (filter && std::wcscmp (filter, test.name) != 0)
            continue;

        foundTest = true;
        std::wprintf (L"[%ls]\n", test.name);
        std::fflush (stdout);
        test.function ();
    }

    unloadPlugin ();

    if (!foundTest)
    {
        std::wprintf (L"No test named %ls. Use --list to see them.\n", filter);
        return 2;
    }

    std::printf ("\n%d failure(s)\n", getFailureCount ());
    return getFailureCount () == 0 ? 0 : 1;
}
