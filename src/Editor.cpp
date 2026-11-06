#include "Editor.h"

#include "Controller.h"
#include "Parameters.h"

#include "vstgui/vstgui.h"

#include <initializer_list>
#include <utility>

using namespace VSTGUI;

namespace RabidPhatt {

namespace {

constexpr Hadronyche::PanelLayout kLayout {};
constexpr int kToggleWidth = 52;

const Hadronyche::PanelTheme kTheme {
    CColor (28, 31, 36),
    CColor (42, 47, 55),
    CColor (70, 78, 90),
    CColor (232, 160, 70),
    CColor (225, 228, 232),
    CColor (160, 166, 175),
};

} // namespace

Editor::Editor (Controller* controller)
: PanelEditor (controller, kLayout.rowWidth ({ 2, 3, 4, 3, 4, 1 }), kLayout.rowHeight (), getParamSpec, kParamCount, kTheme, kLayout), _controller (controller)
{
}

void Editor::buildPanel ()
{
    const int knobY = kLayout.knobRow ();

    const auto addEnvelope = [&] (const char* title, ParamId attack, ParamId decay, ParamId sustain, ParamId release) {
        int knobX = addColumnSection (4, title);
        for (const auto& knob : { std::make_pair (attack, "Attack"), std::make_pair (decay, "Decay"),
                                  std::make_pair (sustain, "Sustain"), std::make_pair (release, "Release") })
        {
            addKnob (knobX, knobY, knob.first, knob.second);
            knobX += kLayout.columnWidth;
        }
    };

    {
        const int left = addColumnSection (2, "Oscillators");
        addKnob (left, knobY, kMotion, "Motion");
        addKnob (left + kLayout.columnWidth, knobY, kGlide, "Glide");
    }
    {
        const int left = addColumnSection (3, "Motion LFO");
        const int right = left + 3 * kLayout.columnWidth;
        const int toggleY = kLayout.margin + kLayout.titleRowOffset;
        addToggle (CRect (right - kToggleWidth, toggleY, right, toggleY + 18), kLfoSync, "Sync");
        _freeRunningViews = addKnob (left, knobY, kLfoRate, "Rate");
        _syncedViews = addMenu (left, knobY, kLfoDivision, "Division");
        addKnob (left + kLayout.columnWidth, knobY, kLfoAmount, "Amount");
        addKnob (left + 2 * kLayout.columnWidth, knobY, kLfoDelay, "Delay");
    }
    addEnvelope ("Amp envelope", kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease);
    {
        const int left = addColumnSection (3, "Filter");
        addKnob (left, knobY, kFilterCutoff, "Cutoff");
        addKnob (left + kLayout.columnWidth, knobY, kFilterResonance, "Resonance");
        addKnob (left + 2 * kLayout.columnWidth, knobY, kFilterEnvAmount, "Env amount", true);
    }
    addEnvelope ("Filter envelope", kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease);
    {
        const int left = addColumnSection (1, "Main");
        addKnob (left, knobY, kVolume, "Volume");
    }
}

void Editor::onSync ()
{
    const bool isSynced = _controller->getParamNormalized (kLfoSync) >= 0.5;
    for (auto* view : _freeRunningViews)
        view->setVisible (!isSynced);
    for (auto* view : _syncedViews)
        view->setVisible (isSynced);
}

void Editor::onClose ()
{
    _freeRunningViews.clear ();
    _syncedViews.clear ();
}

} // namespace RabidPhatt
