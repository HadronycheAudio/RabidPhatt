// What the LV2 wrapper and its Turtle need to know about this plug-in. The URI and the symbols are saved in hosts'
// projects, so never change them, and a new parameter adds a symbol at the end.

#include "Parameters.h"

#include "hadronyche/lv2/PluginInfo.h"

namespace {

const char* const kSymbols[] = {
    "motion",
    "glide",
    "amp_attack",
    "amp_decay",
    "amp_sustain",
    "amp_release",
    "filter_cutoff",
    "filter_resonance",
    "filter_env_amount",
    "filter_attack",
    "filter_decay",
    "filter_sustain",
    "filter_release",
    "volume",
    "lfo_rate",
    "lfo_amount",
    "lfo_delay",
    "lfo_sync",
    "lfo_division",
};

static_assert (sizeof (kSymbols) / sizeof (kSymbols[0]) == RabidPhatt::kParamCount, "One symbol for each parameter");

} // namespace

const Hadronyche::Lv2::PluginInfo& Hadronyche::Lv2::getPluginInfo ()
{
    static const PluginInfo info {
        "https://github.com/HadronycheAudio/RabidPhatt",
        "RabidPhatt",
        "InstrumentPlugin",
        RabidPhatt::getParamSpec,
        RabidPhatt::kParamCount,
        kSymbols,
        2, // LV2 treats an odd minor version as a development build.
        0,
        false, // Audio input
        true, // MIDI
        true, // Transport
        false, // Latency
    };
    return info;
}
