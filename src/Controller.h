#pragma once

#include "PluginConfig.h"

#include "hadronyche/vst/SpecController.h"

namespace RabidPhatt {

// The VST3 edit controller: its parameters and state come from Parameters.h, and it creates the editor.
class Controller : public Hadronyche::SpecController
{
public:
    Controller ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (RABIDPHATT_CONTROLLER_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller);
    }

private:
    Steinberg::IPlugView* createEditor () override;
};

} // namespace RabidPhatt
