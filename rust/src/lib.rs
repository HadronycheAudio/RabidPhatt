//! RabidPhatt's voice: the parameters, and the processor that runs the signal path described in docs/signal-path.md.
//! The C++ side, in src/, is the VST3 classes around it, and reads the parameter table from here through
//! `hadronyche_plugin`.

mod adsr;
mod lfo;
mod low_pass_filter;
mod note_stack;
mod oscillator_bank;

use core::ffi::CStr;

use hadronyche_dsp::{Smoother, VOLUME_SILENCE_DB, volume_to_gain};
use hadronyche_plugin::{
    Block, Curve, NoteEvent, NoteKind, Output, ParamSpec, Params, Processor, TRANSPORT_TEMPO_VALID, Tail, Transport,
    export_plugin, params,
};

use adsr::Adsr;
use lfo::Lfo;
use low_pass_filter::LowPassFilter;
use note_stack::NoteStack;
use oscillator_bank::{OscillatorBank, Side};

// Saved in projects by position, so only ever add to the end of these two, which go together.
const LFO_DIVISIONS: &[&CStr] = &[
    c"4/1",
    c"2/1",
    c"1/1",
    c"1/2 dotted",
    c"1/2",
    c"1/2 triplet",
    c"1/4 dotted",
    c"1/4",
    c"1/4 triplet",
    c"1/8 dotted",
    c"1/8",
    c"1/8 triplet",
    c"1/16 dotted",
    c"1/16",
    c"1/16 triplet",
    c"1/32",
];
const LFO_DIVISION_QUARTERS: [f64; 16] = [
    16.0,
    8.0,
    4.0,
    3.0,
    2.0,
    4.0 / 3.0,
    1.5,
    1.0,
    2.0 / 3.0,
    0.75,
    0.5,
    1.0 / 3.0,
    0.375,
    0.25,
    1.0 / 6.0,
    0.125,
];
/// 1/4.
const LFO_DIVISION_DEFAULT: usize = 7;

params! {
    /// Parameter IDs are saved in projects and automation: never reorder or remove one, only add at the end.
    pub enum Param => PARAMS {
        Motion => ParamSpec::new(c"Motion", c"", 0.001, 1.0, 0.25, Curve::Log, 4),
        Glide => ParamSpec::new(c"Glide", c"ms", 1.0, 5000.0, 1.0, Curve::Log, 0),

        AmpAttack => ParamSpec::new(c"Amp attack", c"ms", 1.0, 10000.0, 5.0, Curve::Log, 0),
        AmpDecay => ParamSpec::new(c"Amp decay", c"ms", 1.0, 10000.0, 300.0, Curve::Log, 0),
        AmpSustain => ParamSpec::new(c"Amp sustain", c"%", 0.0, 100.0, 80.0, Curve::Linear, 0),
        AmpRelease => ParamSpec::new(c"Amp release", c"ms", 1.0, 10000.0, 200.0, Curve::Log, 0),

        FilterCutoff => ParamSpec::new(c"Filter cutoff", c"Hz", 20.0, 20000.0, 1000.0, Curve::Log, 0),
        FilterResonance => ParamSpec::new(c"Filter resonance", c"%", 0.0, 100.0, 20.0, Curve::Linear, 0),
        FilterEnvAmount => ParamSpec::new(c"Filter env amount", c"oct", -8.0, 8.0, 3.0, Curve::Linear, 2),

        FilterAttack => ParamSpec::new(c"Filter attack", c"ms", 1.0, 10000.0, 5.0, Curve::Log, 0),
        FilterDecay => ParamSpec::new(c"Filter decay", c"ms", 1.0, 10000.0, 400.0, Curve::Log, 0),
        FilterSustain => ParamSpec::new(c"Filter sustain", c"%", 0.0, 100.0, 30.0, Curve::Linear, 0),
        FilterRelease => ParamSpec::new(c"Filter release", c"ms", 1.0, 10000.0, 300.0, Curve::Log, 0),

        Volume => ParamSpec::new(c"Volume", c"dB", VOLUME_SILENCE_DB, 6.0, 0.0, Curve::Linear, 1).silent_at_min(),

        LfoRate => ParamSpec::new(c"LFO rate", c"Hz", 0.05, 20.0, 1.0, Curve::Log, 2),
        LfoAmount => ParamSpec::new(c"LFO amount", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        LfoDelay => ParamSpec::new(c"LFO delay", c"s", 0.0, 10.0, 0.0, Curve::Squared, 2),

        LfoSync => ParamSpec::toggle(c"LFO sync", false),
        LfoDivision => ParamSpec::list(c"LFO division", LFO_DIVISIONS, LFO_DIVISION_DEFAULT),
    }
}

/// How long one LFO cycle lasts for an entry of LFO division, in quarter notes.
pub fn lfo_division_quarters(division: i32) -> f64 {
    LFO_DIVISION_QUARTERS[division.clamp(0, LFO_DIVISION_QUARTERS.len() as i32 - 1) as usize]
}

const SMOOTHING_MS: f64 = 20.0;

/// Motion goes from 0.001 to 1, which is 3 decades. 100% of the LFO's amount moves it by that much each way.
const MOTION_RANGE_LOG10: f64 = 3.0;

/// A filter tail below this is treated as finished. It's about -120 dB.
const SILENT_PEAK: f32 = 1e-6;

/// The glide's time is how long it takes to cover 95% of the interval, which is 3 time constants.
const GLIDE_TIME_CONSTANTS: f64 = 3.0;

pub struct RabidPhatt {
    sample_rate: f64,

    notes: NoteStack,
    /// The first note has nothing to glide from.
    has_played: bool,

    /// In MIDI notes, so that a glide moves evenly through the octaves.
    pitch: Smoother,
    rendered_pitch: f64,
    /// `pitch` as cycles per sample, for `rendered_pitch`.
    frequency: f64,

    /// log10 of it, the same space as its knob and the LFO's modulation.
    motion: Smoother,
    /// How far the LFO can move Motion, in the same units.
    lfo_amount: Smoother,
    lfo_fade: Smoother,
    /// log2 of the frequency, so that sweeps are even across the range.
    cutoff_octaves: Smoother,
    resonance: Smoother,
    env_amount: Smoother,
    volume: Smoother,

    left_bank: OscillatorBank,
    right_bank: OscillatorBank,
    lfo: Lfo,
    /// The last one the host gave, for hosts that don't.
    tempo: f64,
    is_lfo_on_grid: bool,
    /// Where the block starts on the grid, in LFO cycles.
    lfo_grid_cycles: f64,
    lfo_grid_cycles_per_sample: f64,
    amp_env: Adsr,
    filter_env: Adsr,
    filter: LowPassFilter,

    /// With nothing left to hear, the voice stops running, which is also why the oscillators keep their phases
    /// between notes, and why a project renders the same way every time.
    is_asleep: bool,
    block_peak: f32,
}

impl RabidPhatt {
    fn apply_parameters(&mut self, params: &Params<'_>) {
        self.pitch
            .prepare(self.sample_rate, params.plain(Param::Glide) / GLIDE_TIME_CONSTANTS);

        self.motion.set_target(params.plain(Param::Motion).log10());
        self.lfo_amount
            .set_target(params.plain(Param::LfoAmount) / 100.0 * MOTION_RANGE_LOG10);
        self.lfo.set_delay(params.plain(Param::LfoDelay));
        self.cutoff_octaves.set_target(params.plain(Param::FilterCutoff).log2());
        self.resonance.set_target(params.plain(Param::FilterResonance) / 100.0);
        self.env_amount.set_target(params.plain(Param::FilterEnvAmount));
        self.volume.set_target(volume_to_gain(params.plain(Param::Volume)));

        self.amp_env.set_parameters(
            params.plain(Param::AmpAttack),
            params.plain(Param::AmpDecay),
            params.plain(Param::AmpSustain) / 100.0,
            params.plain(Param::AmpRelease),
        );
        self.filter_env.set_parameters(
            params.plain(Param::FilterAttack),
            params.plain(Param::FilterDecay),
            params.plain(Param::FilterSustain) / 100.0,
            params.plain(Param::FilterRelease),
        );
    }

    fn update_lfo_timing(&mut self, params: &Params<'_>, transport: &Transport) {
        self.is_lfo_on_grid = false;

        if params.plain(Param::LfoSync) < 0.5 {
            self.lfo.set_rate(params.plain(Param::LfoRate));
            return;
        }

        if transport.flags & TRANSPORT_TEMPO_VALID != 0 && transport.tempo > 0.0 {
            self.tempo = transport.tempo;
        }

        let quarters = lfo_division_quarters(params.plain(Param::LfoDivision) as i32);
        let hz = self.tempo / 60.0 / quarters;
        self.lfo.set_rate(hz);

        // Calculated from the position rather than accumulated, so that it can't drift off the grid. Without a
        // playing transport there's no position to follow, and the sine just runs at the tempo.
        if let (true, Some(position)) = (transport.is_playing(), transport.project_time_music()) {
            self.is_lfo_on_grid = true;
            self.lfo_grid_cycles = position / quarters;
            self.lfo_grid_cycles_per_sample = hz / self.sample_rate;
        }
    }

    fn follow_grid(&mut self, position: usize) {
        if self.is_lfo_on_grid {
            self.lfo
                .set_phase(self.lfo_grid_cycles + position as f64 * self.lfo_grid_cycles_per_sample);
        }
    }

    fn on_note_on(&mut self, pitch: i32, tuning_cents: f32) {
        let was_gate_open = !self.notes.is_empty();
        self.notes.note_on(pitch, tuning_cents);
        if self.notes.is_empty() {
            return;
        }

        let target = self.notes.current_pitch();
        if self.has_played {
            self.pitch.set_target(target);
        } else {
            self.pitch.snap_to(target);
        }
        self.has_played = true;

        // There's one gate: playing legato moves the pitch and leaves the envelopes and LFO delay running.
        if !was_gate_open {
            self.lfo.restart_delay();

            // A note out of silence starts the same way every time. One that catches the previous note's release
            // can't, because the LFO jumping would move every oscillator's pitch at once.
            if self.is_asleep {
                self.lfo.reset_phase();
                self.lfo_fade.snap_to(if self.lfo.has_delay() { 0.0 } else { 1.0 });
            }

            self.amp_env.gate_on();
            self.filter_env.gate_on();
            self.is_asleep = false;
        }
    }

    fn on_note_off(&mut self, pitch: i32) {
        let was_gate_open = !self.notes.is_empty();
        self.notes.note_off(pitch);

        if !self.notes.is_empty() {
            self.pitch.set_target(self.notes.current_pitch());
        } else if was_gate_open {
            self.amp_env.gate_off();
            self.filter_env.gate_off();
        }
    }

    fn handle_note(&mut self, note: &NoteEvent) {
        match note.kind {
            // A note-on with no velocity is the MIDI way of saying note-off.
            NoteKind::On if note.velocity > 0.0 => self.on_note_on(i32::from(note.pitch), note.tuning),
            NoteKind::On | NoteKind::Off => self.on_note_off(i32::from(note.pitch)),
        }
    }

    fn render(&mut self, left: &mut [f32], right: &mut [f32]) {
        if self.is_asleep {
            left.fill(0.0);
            right.fill(0.0);
            return;
        }

        for (out_left, out_right) in left.iter_mut().zip(right.iter_mut()) {
            let pitch = self.pitch.step();
            if pitch != self.rendered_pitch {
                self.rendered_pitch = pitch;
                self.frequency = 440.0 * 2f64.powf((pitch - 69.0) / 12.0) / self.sample_rate;
            }

            // In log10, where the LFO moves Motion like turning its knob, and clamped to the knob's range.
            let fade = self.lfo.step_fade();
            self.lfo_fade.set_target(fade);
            let modulation = self.lfo_amount.step() * self.lfo_fade.step() * self.lfo.step_sine();
            let motion = 10f64.powf((self.motion.step() + modulation).clamp(-MOTION_RANGE_LOG10, 0.0));
            let is_amp_open = !self.amp_env.is_idle();
            let amp = self.amp_env.step() as f32;

            // A closed amplifier means nothing to hear from the oscillators, so they pause.
            let mut l = 0f32;
            let mut r = 0f32;
            if is_amp_open {
                l = self.left_bank.process(self.frequency, motion) * amp;
                r = self.right_bank.process(self.frequency, motion) * amp;
            }

            let cutoff_octaves = self.cutoff_octaves.step() + self.env_amount.step() * self.filter_env.step();
            let resonance = self.resonance.step();
            self.filter.set_coefficients(2f64.powf(cutoff_octaves), resonance);
            l = self.filter.process(l, 0);
            r = self.filter.process(r, 1);

            self.block_peak = max(max(self.block_peak, l.abs()), r.abs());

            let volume = self.volume.step_f32();
            *out_left = l * volume;
            *out_right = r * volume;
        }
    }

    fn update_sleep(&mut self) {
        if !self.is_asleep && self.amp_env.is_idle() && self.filter_env.is_idle() && self.block_peak < SILENT_PEAK {
            self.is_asleep = true;
            self.filter.reset();
        }
        self.block_peak = 0.0;
    }
}

/// `std::max` on floats: the second wins only when it's greater.
fn max(a: f32, b: f32) -> f32 {
    if a < b { b } else { a }
}

impl Processor for RabidPhatt {
    const PARAMS: &'static [ParamSpec] = &PARAMS;
    const STATE_VERSION: i32 = 1;
    const HAS_AUDIO_INPUT: bool = false;
    const HAS_NOTE_INPUT: bool = true;

    fn new() -> Self {
        Self {
            sample_rate: 0.0,
            notes: NoteStack::default(),
            has_played: false,
            pitch: Smoother::default(),
            rendered_pitch: -1.0,
            frequency: 0.0,
            motion: Smoother::default(),
            lfo_amount: Smoother::default(),
            lfo_fade: Smoother::default(),
            cutoff_octaves: Smoother::default(),
            resonance: Smoother::default(),
            env_amount: Smoother::default(),
            volume: Smoother::default(),
            left_bank: OscillatorBank::default(),
            right_bank: OscillatorBank::default(),
            lfo: Lfo::default(),
            tempo: 120.0,
            is_lfo_on_grid: false,
            lfo_grid_cycles: 0.0,
            lfo_grid_cycles_per_sample: 0.0,
            amp_env: Adsr::default(),
            filter_env: Adsr::default(),
            filter: LowPassFilter::default(),
            is_asleep: true,
            block_peak: 0.0,
        }
    }

    fn prepare(&mut self, sample_rate: f64, _max_block_size: usize) {
        self.sample_rate = sample_rate;

        // The parameters' defaults, until the first block brings the host's values, which the voice, asleep, then
        // snaps to.
        let defaults: Vec<f64> = PARAMS
            .iter()
            .map(|spec| spec.plain_to_normalized(spec.default))
            .collect();
        let params = Params::new(&PARAMS, &defaults);

        for smoother in [
            &mut self.motion,
            &mut self.lfo_amount,
            &mut self.lfo_fade,
            &mut self.cutoff_octaves,
            &mut self.resonance,
            &mut self.env_amount,
            &mut self.volume,
        ] {
            smoother.prepare(sample_rate, SMOOTHING_MS);
        }

        self.motion.snap_to(params.plain(Param::Motion).log10());
        self.lfo_amount
            .snap_to(params.plain(Param::LfoAmount) / 100.0 * MOTION_RANGE_LOG10);
        self.lfo_fade.snap_to(0.0);
        self.cutoff_octaves.snap_to(params.plain(Param::FilterCutoff).log2());
        self.resonance.snap_to(params.plain(Param::FilterResonance) / 100.0);
        self.env_amount.snap_to(params.plain(Param::FilterEnvAmount));
        self.volume.snap_to(volume_to_gain(params.plain(Param::Volume)));

        self.left_bank.prepare(Side::Left);
        self.right_bank.prepare(Side::Right);
        self.lfo.prepare(sample_rate);
        self.amp_env.prepare(sample_rate);
        self.filter_env.prepare(sample_rate);
        self.filter.prepare(sample_rate);

        self.notes.clear();
        self.has_played = false;
        self.rendered_pitch = -1.0;
        self.is_asleep = true;

        self.apply_parameters(&params);
    }

    fn process(&mut self, block: Block<'_>) -> Output {
        self.apply_parameters(&block.params);
        self.update_lfo_timing(&block.params, block.transport);

        let started_asleep = self.is_asleep;

        // Nobody can hear parameters move while asleep, and the next note shouldn't ramp from their old values
        // either, even if the change arrives in the same block as the note.
        if self.is_asleep {
            self.pitch.snap_to_target();
            for smoother in [
                &mut self.motion,
                &mut self.lfo_amount,
                &mut self.cutoff_octaves,
                &mut self.resonance,
                &mut self.env_amount,
                &mut self.volume,
            ] {
                smoother.snap_to_target();
            }
        }

        // Renders up to each event, so that notes start on the exact sample.
        let count = block.len();
        let [left, right] = block.output;
        let mut position = 0;
        for note in block.notes {
            let offset = (note.sample_offset.max(0) as usize).clamp(position, count);
            self.follow_grid(position);
            self.render(&mut left[position..offset], &mut right[position..offset]);
            position = offset;
            self.handle_note(note);
        }
        self.follow_grid(position);
        self.render(&mut left[position..], &mut right[position..]);

        self.update_sleep();

        if started_asleep && self.is_asleep {
            Output::Silence
        } else {
            Output::Sound
        }
    }

    /// A note can come at any time, and the release after it ends with the silence flags.
    fn tail(&self) -> Tail {
        Tail::Infinite
    }
}

export_plugin!(RabidPhatt);

#[cfg(test)]
mod tests {
    use super::*;
    use hadronyche_plugin::header;
    use std::path::Path;

    #[test]
    fn param_ids_header_is_current() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../src/ParamIds.h");
        if let Err(problem) = header::check(&path, &header::param_ids("RabidPhatt", &Param::NAMES)) {
            panic!("{problem}");
        }
    }

    #[test]
    fn every_division_has_its_length() {
        assert_eq!(LFO_DIVISIONS.len(), LFO_DIVISION_QUARTERS.len());
        assert_eq!(
            lfo_division_quarters(LFO_DIVISION_DEFAULT as i32),
            1.0,
            "a quarter note to start with"
        );
        assert_eq!(lfo_division_quarters(-1), 16.0);
        assert_eq!(lfo_division_quarters(99), 0.125);
    }

    #[test]
    fn the_defaults_are_what_projects_have_always_started_from() {
        let plain: Vec<f64> = PARAMS.iter().map(|spec| spec.default).collect();
        assert_eq!(
            plain,
            [
                0.25, 1.0, 5.0, 300.0, 80.0, 200.0, 1000.0, 20.0, 3.0, 5.0, 400.0, 30.0, 300.0, 0.0, 1.0, 0.0, 0.0,
                0.0, 7.0
            ]
        );
    }
}
