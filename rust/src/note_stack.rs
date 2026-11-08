//! Which keys are held.

/// The "Note To Freq" block's memory: which keys are held, most recent last. The synth is monophonic with last-note
/// priority, so releasing the newest key falls back to the one held before it. The gate is high for as long as any
/// key is held.
#[derive(Clone, Debug)]
pub struct NoteStack {
    notes: [Note; PITCHES],
    count: usize,
}

const PITCHES: usize = 128;

#[derive(Clone, Copy, Debug, Default)]
struct Note {
    pitch: i32,
    tuning_cents: f32,
}

impl Default for NoteStack {
    fn default() -> Self {
        Self {
            notes: [Note::default(); PITCHES],
            count: 0,
        }
    }
}

impl NoteStack {
    pub fn clear(&mut self) {
        self.count = 0;
    }

    pub fn note_on(&mut self, pitch: i32, tuning_cents: f32) {
        if pitch < 0 || pitch >= PITCHES as i32 {
            return;
        }

        self.note_off(pitch);
        self.notes[self.count] = Note { pitch, tuning_cents };
        self.count += 1;
    }

    pub fn note_off(&mut self, pitch: i32) {
        let held = &mut self.notes[..self.count];
        if let Some(found) = held.iter().position(|note| note.pitch == pitch) {
            held.copy_within(found + 1.., found);
            self.count -= 1;
        }
    }

    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Fractional MIDI pitch of the note that should sound. Only valid when not empty.
    pub fn current_pitch(&self) -> f64 {
        let note = &self.notes[self.count - 1];
        f64::from(note.pitch) + f64::from(note.tuning_cents) / 100.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_newest_key_sounds_and_releasing_it_falls_back() {
        let mut stack = NoteStack::default();
        assert!(stack.is_empty());
        stack.note_on(60, 0.0);
        stack.note_on(64, 50.0);
        assert_eq!(stack.current_pitch(), 64.5);
        stack.note_off(64);
        assert_eq!(stack.current_pitch(), 60.0);
        stack.note_off(60);
        assert!(stack.is_empty());
    }

    #[test]
    fn a_key_pressed_again_moves_to_the_top() {
        let mut stack = NoteStack::default();
        stack.note_on(60, 0.0);
        stack.note_on(62, 0.0);
        stack.note_on(60, 0.0);
        assert_eq!(stack.current_pitch(), 60.0);
        stack.note_off(60);
        assert_eq!(stack.current_pitch(), 62.0);
        stack.note_off(62);
        assert!(stack.is_empty());
    }

    #[test]
    fn pitches_outside_midi_are_ignored() {
        let mut stack = NoteStack::default();
        stack.note_on(-1, 0.0);
        stack.note_on(128, 0.0);
        assert!(stack.is_empty());
    }
}
