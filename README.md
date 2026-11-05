# RabidPhatt

<img src="docs/images/VST_Compatible_Logo_Steinberg_with_TM.png" alt="VST Compatible" width="110">

A monophonic VST® 3 synthesizer, with 66 sawtooth oscillators that a single Motion control pulls apart and brings back together, across the stereo field. It's built directly on the Steinberg VST 3 SDK and VSTGUI.

`docs/signal-path.md` describes the design and how each module behaves.

## Download

Get the installer from the [latest release](https://github.com/HadronycheAudio/RabidPhatt/releases/latest). It needs 64-bit Windows 10 or later, and installs to `C:\Program Files\Common Files\VST3`. The installer isn't code-signed, so Windows will probably warn you: choose **More info**, then **Run anyway**.

## Using it

**Motion** is the heart of the synth. Each stereo side has one sawtooth on the note and 32 others that spread out around it as Motion rises, reaching from 0.58 to 1.42 times the note at the top of the range. Bring it down and they converge on the note, without ever quite lining up. The two sides use different spreads, and the oscillators drift against each other, so the sound moves by itself and sometimes cancels. That's part of it.

**The Motion LFO** moves Motion with a sine wave:

- Amount is in terms of the knob's travel: 100% moves it by the whole range each way. Motion stops at the ends of its range, so near either end, one side of the sine flattens out.
- Rate sets the speed in Hz. Tick Sync to replace it with a division of the tempo. While the transport plays, the sine follows the song's position, so it stays on the grid.
- Delay holds the modulation back for that time after a note starts, then fades it in over the same time again. At 0, it's there immediately.

**The rest** follows the panel from left to right:

- Glide moves the pitch between notes. It's monophonic with last-note priority, and playing legato doesn't restart the envelopes.
- The amp envelope shapes the level, and comes before the filter.
- The filter is a 24 dB/octave low-pass, with its own envelope. Its amount can be positive or negative.
- Volume is last. There's no limiter, and high resonance can go over full scale.

## Requirements for building

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- Git

## Getting the code

The SDK is a pinned git submodule in `external/`:

```powershell
git -c core.longpaths=true clone --recurse-submodules <repo-url>
# or, in an existing clone:
git -c core.longpaths=true submodule update --init --recursive
```

Long paths are needed if you clone into a deep folder, because some of the SDK's sample files would otherwise exceed the Windows path limit.

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target RabidPhatt
```

This produces `build/VST3/Release/RabidPhatt.vst3`. Each build also runs the Steinberg validator on the plug-in, and a validation failure fails the build.

To open the project in the Visual Studio IDE, use `build/RabidPhatt.sln`.

## Testing

```powershell
cmake --build build --config Release --target RabidPhattTests
ctest --test-dir build -C Release --output-on-failure -j 4
```

The tests load the built plug-in and drive it like a host, including its panel. They measure its output and check that it stays compatible with saved projects. They don't touch your mouse. `AGENTS.md` explains how they work.

## Installing your own build

Close your host, then run this from an elevated PowerShell in the project folder:

```powershell
Copy-Item -Recurse -Force build/VST3/Release/RabidPhatt.vst3 "C:\Program Files\Common Files\VST3\"
```

## Project layout

| Path | Purpose |
| --- | --- |
| `src/PluginConfig.h` | Plug-in name, vendor details and class IDs |
| `src/PluginEntry.cpp` | VST3 module entry, registers the classes below |
| `src/Parameters.*` | Parameter IDs, ranges and text, plus saving and loading their values |
| `src/Processor.*` | Handles notes and runs the voice |
| `src/Controller.*` | Registers the parameters and creates the editor |
| `src/Editor.*` | The plug-in window |
| `src/dsp/` | Oscillator banks, envelopes, LFO, filter, note stack and parameter smoothing |
| `tests/` | Tests that drive the built plug-in like a host |
| `installer/` | The Windows installer, and the script that tests it |
| `docs/` | The behaviour specification and the original design diagrams |

## Licence

RabidPhatt is released under the MIT licence: see `LICENSE`.

It's built with the following, which keep their own licences:

- VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
- VSTGUI (`external/vst3sdk/vstgui4/LICENSE`)

VST is a registered trademark of Steinberg Media Technologies GmbH.
