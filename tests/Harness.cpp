#include "Harness.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace RabidPhatt {
namespace Tests {

class EventList : public Vst::IEventList
{
public:
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef () override { return 1; }
    uint32 PLUGIN_API release () override { return 1; }

    int32 PLUGIN_API getEventCount () override { return static_cast<int32> (blockEvents.size ()); }

    tresult PLUGIN_API getEvent (int32 index, Vst::Event& event) override
    {
        event = blockEvents[static_cast<size_t> (index)];
        return kResultOk;
    }

    tresult PLUGIN_API addEvent (Vst::Event&) override { return kResultFalse; }

    std::vector<Vst::Event> blockEvents;

    // Waiting for their block, with sampleOffset counted from the start of the next render.
    std::vector<Vst::Event> pending;
};

Plug::Plug (double sampleRate) : PlugBase (sampleRate), _events (std::make_unique<EventList> ())
{
}

Plug::~Plug () = default;

void Plug::noteOn (int pitch, int offset, float velocity)
{
    Vst::Event event {};
    event.type = Vst::Event::kNoteOnEvent;
    event.sampleOffset = offset;
    event.noteOn.pitch = static_cast<int16> (pitch);
    event.noteOn.velocity = velocity;
    event.noteOn.noteId = -1;
    _events->pending.push_back (event);
}

void Plug::noteOff (int pitch, int offset)
{
    Vst::Event event {};
    event.type = Vst::Event::kNoteOffEvent;
    event.sampleOffset = offset;
    event.noteOff.pitch = static_cast<int16> (pitch);
    event.noteOff.noteId = -1;
    _events->pending.push_back (event);
}

Stereo Plug::render (double seconds)
{
    return renderSamples (static_cast<size_t> (seconds * _sampleRate));
}

Stereo Plug::renderSamples (size_t total)
{
    Stereo result;
    result.left.resize (total);
    result.right.resize (total);

    for (size_t position = 0; position < total; position += kBlockSize)
    {
        const int count = static_cast<int> (std::min<size_t> (kBlockSize, total - position));
        float* outputs[2] = { &result.left[position], &result.right[position] };

        _events->blockEvents.clear ();
        auto& pending = _events->pending;
        for (auto it = pending.begin (); it != pending.end ();)
        {
            if (it->sampleOffset < static_cast<int32> (position) + count)
            {
                Vst::Event event = *it;
                event.sampleOffset -= static_cast<int32> (position);
                _events->blockEvents.push_back (event);
                it = pending.erase (it);
            }
            else
            {
                ++it;
            }
        }

        const uint64 silenceFlags = runBlock (nullptr, outputs, count, _events.get ());

        if (context && (context->state & Vst::ProcessContext::kPlaying) != 0)
            context->projectTimeMusic += count * context->tempo / (60.0 * _sampleRate);

        result.blockSilent.push_back (silenceFlags == 3);
    }
    return result;
}

void Plug::bare ()
{
    set (kAmpAttack, 1);
    set (kAmpSustain, 100);
    set (kFilterCutoff, 20000);
    set (kFilterResonance, 0);
    set (kFilterEnvAmount, 0);
    set (kVolume, 0);
}

double mean (const Samples& samples, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        sum += samples[i];
    return sum / static_cast<double> (std::max<size_t> (1, samples.size () - from));
}

double measureFrequency (const Samples& samples, double sampleRate)
{
    const int count = static_cast<int> (std::min<size_t> (samples.size (), 9600));
    const int maxLag = std::min (static_cast<int> (sampleRate / 25.0), count / 2);
    if (maxLag < 4)
        return 0.0;

    std::vector<double> similarity (static_cast<size_t> (maxLag) + 2, 0.0);
    for (int lag = 1; lag <= maxLag + 1 && lag < count; ++lag)
    {
        double both = 0.0;
        double first = 0.0;
        double second = 0.0;
        for (int i = 0; i + lag < count; ++i)
        {
            const double a = samples[static_cast<size_t> (i)];
            const double b = samples[static_cast<size_t> (i + lag)];
            both += a * b;
            first += a * a;
            second += b * b;
        }
        similarity[static_cast<size_t> (lag)] = both / std::max (std::sqrt (first * second), 1e-20);
    }

    // Lags below this just see the same part of the wave, which says nothing about its period.
    int start = 1;
    while (start <= maxLag && similarity[static_cast<size_t> (start)] > 0.5)
        ++start;

    double strongest = 0.0;
    for (int lag = start; lag <= maxLag; ++lag)
        strongest = std::max (strongest, similarity[static_cast<size_t> (lag)]);

    for (int lag = std::max (start, 2); lag <= maxLag; ++lag)
    {
        const double before = similarity[static_cast<size_t> (lag - 1)];
        const double here = similarity[static_cast<size_t> (lag)];
        const double after = similarity[static_cast<size_t> (lag + 1)];
        if (here >= 0.9 * strongest && here >= before && here >= after)
        {
            const double curve = before - 2.0 * here + after;
            const double offset = curve != 0.0 ? 0.5 * (before - after) / curve : 0.0;
            return sampleRate / (lag + offset);
        }
    }
    return 0.0;
}

double frequencyOfNote (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

size_t firstNonZero (const Samples& samples)
{
    const auto found = std::find_if (samples.begin (), samples.end (), [] (float sample) { return sample != 0.0f; });
    return static_cast<size_t> (found - samples.begin ());
}

} // namespace Tests
} // namespace RabidPhatt
