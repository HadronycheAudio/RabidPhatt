//! "Left Oscillator Bank" and "Right Oscillator Bank": 33 sawtooth oscillators and a signal add.

use hadronyche_dsp::step_correction;

pub const MOVING_ROWS: usize = 32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Left,
    Right,
}

/// Keeps the peaks of all the rows playing together under full scale.
const BANK_GAIN: f64 = 0.07;

/// Above this, wraps come too close together for the anti-aliasing to deal with them. A sawtooth this high is nearly
/// a sine, so fading it out only loses a little at the very top of the keyboard.
const FADE_START: f64 = 0.2;
const FADE_END: f64 = 0.25;

/// These are the diagrams' multipliers divided by 10, which the owner chose after trying them in Cubase.
const LEFT_MULTIPLIERS: [f64; MOVING_ROWS] = [
    0.11, 0.13, 0.15, 0.17, 0.19, 0.21, 0.23, 0.25, 0.27, 0.29, 0.31, 0.33, 0.35, 0.37, 0.39, 0.41, -0.11, -0.13,
    -0.15, -0.17, -0.19, -0.21, -0.23, -0.25, -0.27, -0.29, -0.31, -0.33, -0.35, -0.37, -0.39, -0.41,
];

/// The last row is -0.42: the -4.1 in the diagram is a known typo for -4.2.
const RIGHT_MULTIPLIERS: [f64; MOVING_ROWS] = [
    0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.42, -0.12, -0.14,
    -0.16, -0.18, -0.20, -0.22, -0.24, -0.26, -0.28, -0.30, -0.32, -0.34, -0.36, -0.38, -0.40, -0.42,
];

/// The first row always plays the incoming frequency. Every other row plays frequency x (1 + Motion x multiplier), so
/// that they all converge on the note as Motion falls, and diverge from it, above and below, as Motion rises. Those
/// rows alternate between inverted and not, starting with inverted.
#[derive(Clone, Debug)]
pub struct OscillatorBank {
    multipliers: [f64; MOVING_ROWS],
    phases: [f64; MOVING_ROWS],
    base_phase: f64,
}

impl Default for OscillatorBank {
    fn default() -> Self {
        Self {
            multipliers: [0.0; MOVING_ROWS],
            phases: [0.0; MOVING_ROWS],
            base_phase: 0.0,
        }
    }
}

impl OscillatorBank {
    pub fn prepare(&mut self, side: Side) {
        self.multipliers = match side {
            Side::Left => LEFT_MULTIPLIERS,
            Side::Right => RIGHT_MULTIPLIERS,
        };

        // A fixed seed, so that a project renders the same way every time. The banks start differently from each
        // other, for stereo width. Nothing else is done about cancellation, which comes and goes as the rows drift,
        // and is part of the sound.
        let mut random: u32 = match side {
            Side::Left => 0x9E37_79B9,
            Side::Right => 0x85EB_CA6B,
        };
        let mut next_random = || {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            f64::from(random) / 4294967296.0
        };

        self.base_phase = next_random();
        for phase in &mut self.phases {
            *phase = next_random();
        }
    }

    /// `frequency` is in cycles per sample.
    pub fn process(&mut self, frequency: f64, motion: f64) -> f32 {
        let mut sum = saw(&mut self.base_phase, frequency);

        let spread = frequency * motion;
        for (i, (phase, multiplier)) in self.phases.iter_mut().zip(&self.multipliers).enumerate() {
            let value = saw(phase, frequency + spread * multiplier);
            sum += if i % 2 == 0 { -value } else { value };
        }

        (sum * BANK_GAIN) as f32
    }
}

fn saw(phase: &mut f64, increment: f64) -> f64 {
    *phase += increment;
    if *phase >= 1.0 {
        *phase -= phase.floor();
    }

    if increment >= FADE_END {
        return 0.0;
    }

    let mut value = 2.0 * *phase - 1.0;

    // Each wrap steps the output down by 2. Comparing first avoids dividing for most samples.
    let reach = 2.0 * increment;
    if *phase < reach {
        value -= 2.0 * step_correction(*phase / increment);
    }
    if 1.0 - *phase < reach {
        value -= 2.0 * step_correction((*phase - 1.0) / increment);
    }

    if increment > FADE_START {
        value *= (FADE_END - increment) / (FADE_END - FADE_START);
    }
    value
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_banks_start_the_same_way_every_time_and_differently_from_each_other() {
        let mut left = OscillatorBank::default();
        left.prepare(Side::Left);
        let mut again = OscillatorBank::default();
        again.prepare(Side::Left);
        let mut right = OscillatorBank::default();
        right.prepare(Side::Right);
        assert_eq!(left.base_phase, again.base_phase);
        assert_eq!(left.phases, again.phases);
        assert_ne!(left.base_phase, right.base_phase);
        assert_eq!(left.process(0.01, 0.25), again.process(0.01, 0.25));
    }

    #[test]
    fn a_saw_too_high_to_anti_alias_is_silent() {
        let mut phase = 0.3;
        assert_eq!(saw(&mut phase, 0.25), 0.0);
        assert_eq!(phase, 0.55);
    }

    #[test]
    fn a_saw_is_a_ramp_away_from_its_wraps() {
        let mut phase = 0.5;
        let value = saw(&mut phase, 0.01);
        assert!((value - (2.0 * 0.51 - 1.0)).abs() < 1e-12);
    }
}
