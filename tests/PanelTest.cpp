// The real editor in an off-screen window, driven with mouse messages on its real controls, while audio
// runs. It doesn't touch the real mouse. Set the environment variable RABIDPHATT_SCREENSHOTS to a folder
// to also save the panel as a BMP file.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Panel.h"

#include <cmath>
#include <cstdio>

namespace RabidPhatt {
namespace Tests {

namespace {

// The centre of the Motion knob, which follows the layout in src/Editor.cpp.
constexpr int kMotionKnobX = 47;
constexpr int kMotionKnobY = 64;

// The Sync switch, at the right of the Motion LFO section's title.
constexpr int kSyncToggleX = 345;
constexpr int kSyncToggleY = 20;

} // namespace

void testPanel ()
{
    Plug plug;
    PanelWindow panel (plug.controller, L"RabidPhattPanelTest");
    check ("the controller creates an editor", panel.view != nullptr);
    if (!panel.view)
        return;

    check ("the editor opens", panel.open ());
    check ("it has a window", panel.editorWindow != nullptr);

    const auto runWithUi = [&] (double seconds) { panel.runFor (seconds, [&] { plug.renderSamples (kBlockSize); }); };

    plug.noteOn (57);
    runWithUi (0.3);
    panel.saveScreenshot ("RABIDPHATT_SCREENSHOTS", "panel-default");

    // A knob drag changes its parameter. The drag ends 1/6 of the way in, and a little more because the mouse is
    // slightly above the knob's centre.
    const double before = plug.controller->getParamNormalized (kMotion);
    panel.dragLeft (kMotionKnobX, kMotionKnobY);
    const double after = plug.controller->getParamNormalized (kMotion);
    std::printf ("  Motion went from %.3f to %.3f\n", before, after);
    check ("dragging the Motion knob moves Motion to where the mouse is", std::abs (after - 0.184) < 0.02);

    // Automation reaches the panel: the knob isn't the source of this change.
    plug.set (kVolume, -12.0);
    runWithUi (0.3);
    panel.saveScreenshot ("RABIDPHATT_SCREENSHOTS", "panel-edited");

    // The Sync switch. This also brings up the division menu in place of the rate knob.
    panel.click (kSyncToggleX, kSyncToggleY);
    check ("clicking Sync turns it on", plug.controller->getParamNormalized (kLfoSync) > 0.5);
    plug.set (kLfoDivision, 12);
    runWithUi (0.3);
    panel.saveScreenshot ("RABIDPHATT_SCREENSHOTS", "panel-synced");

    check ("the editor closes", panel.close ());
}

} // namespace Tests
} // namespace RabidPhatt
