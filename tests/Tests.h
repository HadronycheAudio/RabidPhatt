#pragma once

namespace RabidPhatt {
namespace Tests {

// CompatibilityTests.cpp
void testClassIds ();
void testParameterIds ();
void testParameterRanges ();
void testSavedFormat ();
void testBuses ();

// OscillatorTests.cpp
void testBasePitch ();
void testMotionMinimum ();
void testBanksRows ();
void testMotionSpread ();
void testMotionSmooth ();
void testLfoModulates ();
void testLfoRate ();
void testLfoDelay ();
void testLfoSmooth ();
void testLfoSyncTempo ();
void testLfoSyncDivisions ();
void testLfoSyncGrid ();
void testLfoSyncFree ();
void testLevelFlat ();
void testStereoWidth ();
void testDcOffset ();
void testAliasing ();
void testTopOfKeyboard ();

// VoiceTests.cpp
void testLastNotePriority ();
void testLegato ();
void testGlide ();
void testAmpEnvelope ();
void testSilenceFlags ();
void testSampleAccurate ();
void testFilterSlope ();
void testFilterEnvelope ();
void testFilterTail ();
void testVolume ();
void testLevels ();
void testDeterminism ();
void testExtremeSettings ();
void testParameterState ();
void testParameterText ();
void measureCpu ();

// PanelTest.cpp
void testPanel ();

} // namespace Tests
} // namespace RabidPhatt
