# RabidPhatt signal path

The design is in three diagrams: [the voice](images/general.png), the [left oscillator bank](images/left-oscillator-bank.png) and the [right oscillator bank](images/right-oscillator-bank.png). The bank diagrams show an earlier version of the oscillator rows: see [Oscillator banks](#oscillator-banks) for how they differ. **Requirement** marks what the owner specified. **Proposed** marks a choice made during implementation where the design says nothing, or that needed interpreting. Those are open to change.

```
MIDI → Note To Freq ─ frequency → Glide ─ smoothed frequency ─┬→ Left bank ──┐
            │                                                 └→ Right bank ─┤
            └ gate ─┬→ Amp env ─────────────────────────→ Amplifier (stereo) ←┘
                    └→ Filter env ─→ Filter (stereo) ←─ Amplifier
                                       └→ Main Volume → Out L, Out R
```

## Notes and glide

- **Requirement:** one voice, with one frequency and one gate.
- **Proposed:** last-note priority. Releasing the newest key returns to the one held before it. Notes come from all MIDI channels, and velocity isn't used, because nothing in the design takes it.
- **Proposed:** the gate stays high while any key is held, so playing legato moves the pitch without restarting the envelopes.
- **Requirement:** Glide sits between the note and both banks.
- **Proposed:** it glides in pitch rather than in Hz, so that it's even across octaves. Its time is how long it takes to cover 95% of the interval. It always applies, including from the previous note after a gap. Only the first note after activation has nothing to glide from. At its 1 ms minimum it's effectively off.

## Oscillator banks

- **Requirement:** 33 sawtooth oscillators per bank, summed. The first row plays the note's frequency.
- **Requirement:** Motion makes the other rows diverge from the note as it rises and converge on it as it falls. Each plays `frequency × (1 + Motion × multiplier)`. The diagrams draw `frequency × Motion × multiplier`, which was built first and tried in Cubase. It didn't do what the owner wanted, because it pulled the rows towards 0 Hz instead of towards the note.
- **Requirement:** Motion goes from 0.001 to 1, so that the rows are never exactly in sync, on a logarithmic knob: the middle of its travel is 0.032. It's a double, and operating it must be smooth: it's smoothed over about 20 ms and applied for every sample. Jumping it across its whole range doesn't make a larger sample-to-sample step than holding it still.
- **Requirement:** the left bank's multipliers are ±0.11 to ±0.41 and the right bank's ±0.12 to ±0.42, in steps of 0.02. These are the diagrams' multipliers divided by 10, which the owner chose in Cubase. The -4.1 on the last row of the right bank's diagram is a typo for -4.2, confirmed by the owner, which is -0.42 after the division.
- **Requirement:** the positive rows come first, then the negative ones. Rows alternate between inverted and not, starting with the first multiplied row inverted.
- **Requirement:** cancellation between rows is accepted, and nothing tries to prevent it. The sound is meant to be chaotic.

At Motion 1, the rows cover 0.58 to 1.42 times the note, which is about 9 semitones below it and 6 above. Because of the alternating inversions, rows with equal phases would largely cancel when converged, and the level would then grow as they drift apart.

- **Proposed:** each row starts at its own random phase, which is enough to avoid that collapse when converged. The generator has a fixed seed, different for each bank, so the same notes always render the same way.
- **Proposed:** the oscillators run free. Their phases aren't reset by notes, so repeated notes don't sound identical. They pause while the amplifier is closed.

### Motion LFO

- **Requirement:** a sine LFO modulates Motion, with controls for rate, amount and delay. It's sine only, to keep the synth simple. With a delay, the modulation fades in after that time. With no delay, it's there immediately.
- **Proposed:** the rate goes from 0.05 to 20 Hz. The amount is in terms of Motion's knob, and 100% moves it by the knob's whole range each way, so the same amount covers the same travel wherever the knob is. Motion stops at the ends of its range, so close to either end, one side of the sine flattens out. The amount starts at 0, so a new instance has no modulation.
- **Requirement:** the LFO can switch between free running, in Hz, and synced to the tempo, with subdivisions. The panel shows the rate knob or the division menu, depending on which is in use.
- **Proposed:** the divisions go from 4/1 down to 1/32, with dotted and triplet versions of 1/2 to 1/16, where a division is how long one whole cycle lasts. While the transport plays, the sine is set by the song's position, so that it can't drift, and a note can start anywhere on the grid. While it's stopped, the sine runs at the host's tempo, or at 120 BPM if the host doesn't give one. In either mode, the delay stays in seconds.
- **Proposed:** the delay goes up to 10 s, and is the time with no modulation at all. The fade in then takes the same time again, so that a short delay doesn't turn into a slow fade.
- **Proposed:** like the envelopes, the delay restarts when the gate opens, and playing legato leaves it running. A note out of silence also starts the sine at zero, so that it begins with Motion where its knob is. A note that catches the previous one's release keeps the sine where it was, because jumping it would move every oscillator's pitch at once.

### Implementation

- **Proposed:** each saw is band-limited with a 4-point polynomial step correction. There's no oversampling, so there's no latency. With the rows converged on a 2960 Hz note at 48 kHz, the worst alias is at -58 dB, and at -85 dB below 10 kHz. The correction also softens the very top of the spectrum, by a calculated 2.5 dB at 10 kHz and 11 dB at 20 kHz at 48 kHz.
- **Proposed:** an oscillator fades out as its frequency goes from a fifth to a quarter of the sample rate, because the correction doesn't work above that. At 48 kHz, this starts at about MIDI note 115 for the highest row at Motion 1, and all the rows are silent at note 127 when converged.

### Levels

- **Proposed:** each bank's sum is multiplied by 0.07, whatever Motion is, which keeps peaks under full scale with the filter open.

Measured over 19 s, with the filter open:

| Note | RMS across Motion 0.001 to 1 | Highest peak |
| --- | --- | --- |
| 33 | -12.7 to -10.6 dB | -0.6 dB |
| 57 | -12.9 to -10.9 dB | -0.5 dB |
| 81 | -13.1 to -12.5 dB | -1.0 dB |

Over 250 ms windows, the level moves by up to about 7 dB by itself at a fixed Motion when the rows are close together, from beating. Sweeping Motion doesn't add to that. The synth has no limiter or clipper, and high resonance can take it over full scale. The peaks depend on how the rows' phases line up, so a rare alignment could go higher than what was measured.

## Amplifier and envelopes

- **Requirement:** an amplifier before the filter, driven by its own envelope generator, and a filter driven by another, both from the same gate.
- **Proposed:** both are ADSR envelopes with analogue-style exponential stages. Attack time is from silence to full level, and decay and release times are to within 1% of their target. A new gate continues from the current level, so nothing clicks.

## Filter

- **Requirement:** a stereo filter with an envelope.
- **Proposed:** a 24 dB/octave low-pass, from two state-variable stages that stay stable under fast sweeps. Its controls are cutoff, resonance (up to a Q of 20, with no self-oscillation) and an envelope amount of ±8 octaves. It has no keyboard tracking.

## Main Volume

- **Requirement:** a stereo volume at the end.
- **Proposed:** -60 to +6 dB, where -60 dB is silence.

## Host behaviour

- There's no latency. Notes start on their exact sample.
- Parameter changes are smoothed over about 20 ms, while a note is sounding.
- Once the amplifier and filter envelopes have finished and the filter's tail is below about -120 dB, the voice stops running and the plug-in reports silence to the host.
