//! The "Filter" block.

use hadronyche_dsp::{SvfCoefficients, SvfState, svf_coefficients, svf_tick, svf_warp};

const FIRST_Q: f64 = 0.5412;
const SECOND_Q: f64 = 1.3066;
const MAX_Q: f64 = 20.0;

/// A stereo 24 dB/octave low-pass, made of two topology-preserving state-variable stages, which stay stable however
/// fast the envelope sweeps them. Only the second stage resonates.
#[derive(Clone, Debug)]
pub struct LowPassFilter {
    sample_rate: f64,
    first: SvfCoefficients,
    second: SvfCoefficients,
    channels: [[SvfState; 2]; 2],
}

impl Default for LowPassFilter {
    fn default() -> Self {
        Self {
            sample_rate: 44100.0,
            first: SvfCoefficients::default(),
            second: SvfCoefficients::default(),
            channels: Default::default(),
        }
    }
}

impl LowPassFilter {
    pub fn prepare(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.reset();
    }

    pub fn reset(&mut self) {
        self.channels = Default::default();
    }

    /// Light enough to call for every sample. Resonance is 0..1.
    pub fn set_coefficients(&mut self, frequency: f64, resonance: f64) {
        let g = svf_warp(frequency, self.sample_rate);

        // At zero resonance these two Qs make a 4-pole Butterworth response.
        self.first = svf_coefficients(g, FIRST_Q);
        self.second = svf_coefficients(g, SECOND_Q * (MAX_Q / SECOND_Q).powf(resonance));
    }

    pub fn process(&mut self, input: f32, channel: usize) -> f32 {
        let stages = &mut self.channels[channel];
        let first = svf_tick(input, &self.first, &mut stages[0]).v2;
        svf_tick(first, &self.second, &mut stages[1]).v2
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dc_comes_through_and_the_channels_are_separate() {
        let mut filter = LowPassFilter::default();
        filter.prepare(48000.0);
        filter.set_coefficients(1000.0, 0.0);
        let mut left = 0.0;
        for _ in 0..48000 {
            left = filter.process(1.0, 0);
        }
        assert!((left - 1.0).abs() < 1e-3);
        assert_eq!(filter.process(0.0, 1), 0.0);
    }
}
