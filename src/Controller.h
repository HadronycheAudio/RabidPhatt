#pragma once

#include "PluginConfig.h"

#include "hadronyche/vst/RustController.h"

namespace RabidPhatt {

// The VST3 edit controller: its parameters and state come from the Rust side, and it creates the editor.
class Controller : public Hadronyche::RustController
{
public:
    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (RABIDPHATT_CONTROLLER_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller);
    }

private:
    Steinberg::IPlugView* createEditor () override;
};

} // namespace RabidPhatt
