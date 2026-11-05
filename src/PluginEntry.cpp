// VST3 module entry: registers the processor and controller classes with the host.

#include "Controller.h"
#include "PluginConfig.h"
#include "Processor.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY_DEF (RABIDPHATT_VENDOR, RABIDPHATT_URL, RABIDPHATT_EMAIL)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (RabidPhatt::Processor::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                RABIDPHATT_NAME,
                Vst::kDistributable,
                Vst::PlugType::kInstrumentSynth,
                RABIDPHATT_VERSION,
                kVstVersionString,
                RabidPhatt::Processor::createInstance)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (RabidPhatt::Controller::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                RABIDPHATT_NAME,
                0,
                "",
                RABIDPHATT_VERSION,
                kVstVersionString,
                RabidPhatt::Controller::createInstance)

END_FACTORY
