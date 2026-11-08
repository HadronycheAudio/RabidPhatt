//! A sine LFO, with a delay before its modulation arrives.

/// Nothing for the delay's time, and then a fade in that takes the same time again. With no delay, it's at full
/// depth straight away.
#[derive(Clone, Debug, Default)]
pub struct Lfo {
    seconds_per_sample: f64,
    increment: f64,
    phase: f64,
    delay: f64,
    elapsed: f64,
}

impl Lfo {
    pub fn prepare(&mut self, sample_rate: f64) {
        self.seconds_per_sample = 1.0 / sample_rate;
        self.phase = 0.0;
        self.restart_delay();
    }

    pub fn set_rate(&mut self, hz: f64) {
        self.increment = hz * self.seconds_per_sample;
    }

    pub fn set_delay(&mut self, seconds: f64) {
        self.delay = seconds;
    }

    /// Starting at zero means the note starts with Motion where its knob is.
    pub fn reset_phase(&mut self) {
        self.phase = 0.0;
    }

    pub fn restart_delay(&mut self) {
        self.elapsed = 0.0;
    }

    /// For following the host's musical position. Whole cycles don't matter.
    pub fn set_phase(&mut self, cycles: f64) {
        self.phase = cycles - cycles.floor();
    }

    pub fn has_delay(&self) -> bool {
        self.delay > 0.0
    }

    pub fn step_sine(&mut self) -> f64 {
        let value = (2.0 * core::f64::consts::PI * self.phase).sin();
        self.phase += self.increment;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
        }
        value
    }

    pub fn step_fade(&mut self) -> f64 {
        self.elapsed += self.seconds_per_sample;
        if self.has_delay() {
            (self.elapsed / self.delay - 1.0).clamp(0.0, 1.0)
        } else {
            1.0
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_fade_waits_for_the_delay_and_then_takes_as_long_again() {
        let mut lfo = Lfo::default();
        lfo.prepare(10.0);
        lfo.set_delay(1.0);
        let fades: Vec<f64> = (0..20).map(|_| lfo.step_fade()).collect();
        assert_eq!(fades[9], 0.0);
        assert!((fades[14] - 0.5).abs() < 1e-9);
        assert_eq!(fades[19], 1.0);
        lfo.set_delay(0.0);
        assert_eq!(lfo.step_fade(), 1.0);
    }

    #[test]
    fn the_sine_follows_a_set_phase_without_its_whole_cycles() {
        let mut lfo = Lfo::default();
        lfo.prepare(100.0);
        lfo.set_rate(25.0);
        lfo.set_phase(3.25);
        assert!((lfo.step_sine() - 1.0).abs() < 1e-12);
        assert!(lfo.step_sine().abs() < 1e-12);
    }
}
