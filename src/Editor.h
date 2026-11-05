#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/vstguibase.h"

#include <vector>

namespace VSTGUI {
class CControl;
class CTextLabel;
class CView;
class CVSTGUITimer;
} // namespace VSTGUI

namespace RabidPhatt {

class Controller;

// The plug-in window: one row of knobs, in signal path order.
class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    explicit Editor (Controller* controller);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close () SMTG_OVERRIDE;

    void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

private:
    struct Binding
    {
        VSTGUI::CControl* control;
        VSTGUI::CTextLabel* valueLabel; // Null for controls that show their value themselves.
    };

    using ViewGroup = std::vector<VSTGUI::CView*>;

    ViewGroup addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar = false);
    ViewGroup addMenu (int x, int y, Steinberg::Vst::ParamID id, const char* title);
    void addToggle (const VSTGUI::CRect& area, Steinberg::Vst::ParamID id, const char* title);
    void syncControls ();

    Controller* _controller;
    std::vector<Binding> _bindings;

    // These share a column, because only one of them applies at a time. They belong to the frame.
    ViewGroup _freeRunningViews;
    ViewGroup _syncedViews;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> _syncTimer;
};

} // namespace RabidPhatt
