//! An analogue-style envelope generator.

/// Used for both "Amp Env Generator" and "Filter Env Generator". Each stage is a capacitor charging towards a target,
/// so a gate arriving during any stage continues from the current level rather than jumping, which keeps
/// retriggering free of clicks.
///
/// Sustain has no stage of its own: decay settles on the sustain level, and keeps following it if the knob moves
/// while a key is held.
#[derive(Clone, Debug)]
pub struct Adsr {
    sample_rate: f64,
    attack_coefficient: f64,
    decay_coefficient: f64,
    release_coefficient: f64,
    sustain: f64,
    value: f64,
    stage: Stage,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Stage {
    Idle,
    Attack,
    Decay,
    Release,
}

/// Charging towards a level above full gives the attack its fast-then-slowing analogue shape, and means that it
/// reaches full level in a finite time.
const ATTACK_TARGET: f64 = 1.3;
/// -80 dB, where a release can stop without being heard.
const SILENCE: f64 = 1e-4;

impl Default for Adsr {
    fn default() -> Self {
        Self {
            sample_rate: 44100.0,
            attack_coefficient: 0.0,
            decay_coefficient: 0.0,
            release_coefficient: 0.0,
            sustain: 1.0,
            value: 0.0,
            stage: Stage::Idle,
        }
    }
}

impl Adsr {
    pub fn prepare(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.reset();
    }

    pub fn reset(&mut self) {
        self.stage = Stage::Idle;
        self.value = 0.0;
    }

    /// Times are how long a stage takes to get there: attack from silence to full level, decay and release to within
    /// 1% of their target. Sustain is 0..1.
    pub fn set_parameters(&mut self, attack_ms: f64, decay_ms: f64, sustain: f64, release_ms: f64) {
        self.attack_coefficient = self.coefficient(attack_ms, (ATTACK_TARGET / (ATTACK_TARGET - 1.0)).ln());
        self.decay_coefficient = self.coefficient(decay_ms, 100f64.ln());
        self.release_coefficient = self.coefficient(release_ms, 100f64.ln());
        self.sustain = sustain;
    }

    pub fn gate_on(&mut self) {
        self.stage = Stage::Attack;
    }

    pub fn gate_off(&mut self) {
        if self.stage != Stage::Idle {
            self.stage = Stage::Release;
        }
    }

    pub fn is_idle(&self) -> bool {
        self.stage == Stage::Idle
    }

    pub fn step(&mut self) -> f64 {
        match self.stage {
            Stage::Idle => {}
            Stage::Attack => {
                self.value = ATTACK_TARGET + (self.value - ATTACK_TARGET) * self.attack_coefficient;
                if self.value >= 1.0 {
                    self.value = 1.0;
                    self.stage = Stage::Decay;
                }
            }
            Stage::Decay => {
                self.value = self.sustain + (self.value - self.sustain) * self.decay_coefficient;
            }
            Stage::Release => {
                self.value *= self.release_coefficient;
                if self.value < SILENCE {
                    self.value = 0.0;
                    self.stage = Stage::Idle;
                }
            }
        }
        self.value
    }

    fn coefficient(&self, time_ms: f64, time_constants: f64) -> f64 {
        (-time_constants * 1000.0 / (self.sample_rate * time_ms)).exp()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_rises_settles_on_sustain_and_releases_to_idle() {
        let mut env = Adsr::default();
        env.prepare(1000.0);
        env.set_parameters(10.0, 10.0, 0.5, 10.0);
        assert!(env.is_idle());
        env.gate_on();
        let mut peak: f64 = 0.0;
        for _ in 0..100 {
            peak = peak.max(env.step());
        }
        assert_eq!(peak, 1.0, "the attack lands on exactly full level");
        assert!((env.step() - 0.5).abs() < 0.01, "then decays to within 1% of sustain");
        env.gate_off();
        for _ in 0..200 {
            env.step();
        }
        assert!(env.is_idle());
        assert_eq!(env.step(), 0.0);
    }
}
