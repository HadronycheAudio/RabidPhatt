#pragma once

#include "hadronyche/gui/PanelEditor.h"

namespace RabidPhatt {

class Controller;

// The plug-in window: one row of knobs, in signal path order.
class Editor : public Hadronyche::PanelEditor
{
public:
    explicit Editor (Controller* controller);

private:
    void buildPanel () override;
    void onSync () override;
    void onClose () override;

    Controller* _controller;

    // These share a column, because only one of them applies at a time. They belong to the frame.
    ViewGroup _freeRunningViews;
    ViewGroup _syncedViews;
};

} // namespace RabidPhatt
