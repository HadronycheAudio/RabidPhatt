#pragma once

#include <algorithm>
#include <array>

namespace RabidPhatt {

// The "Note To Freq" block's memory: which keys are held, most recent last. The synth is monophonic
// with last-note priority, so releasing the newest key falls back to the one held before it. The gate
// is high for as long as any key is held.
class NoteStack
{
public:
    void clear () { _count = 0; }

    void noteOn (int pitch, float tuningCents)
    {
        if (pitch < 0 || pitch >= kPitches)
            return;

        noteOff (pitch);
        _notes[static_cast<size_t> (_count++)] = { pitch, tuningCents };
    }

    void noteOff (int pitch)
    {
        const auto begin = _notes.begin ();
        const auto end = begin + _count;
        const auto found = std::find_if (begin, end, [pitch] (const Note& note) { return note.pitch == pitch; });
        if (found != end)
        {
            std::copy (found + 1, end, found);
            --_count;
        }
    }

    bool isEmpty () const { return _count == 0; }

    // Fractional MIDI pitch of the note that should sound. Only valid when not empty.
    double getCurrentPitch () const
    {
        const auto& note = _notes[static_cast<size_t> (_count - 1)];
        return note.pitch + note.tuningCents / 100.0;
    }

private:
    static constexpr int kPitches = 128;

    struct Note
    {
        int pitch;
        float tuningCents;
    };

    std::array<Note, kPitches> _notes {};
    int _count { 0 };
};

} // namespace RabidPhatt
