// VST3 module entry: registers the processor and controller classes with the host.

#include "Controller.h"
#include "PluginConfig.h"
#include "Processor.h"

#include "hadronyche/vst/PluginFactory.h"

HADRONYCHE_PLUGIN_FACTORY (RabidPhatt, RABIDPHATT, Vst::PlugType::kInstrumentSynth, Vst::kDistributable)
