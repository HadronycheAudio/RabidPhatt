# AGENTS.md

Guidance for coding agents working in this repository. Read [Rules that protect users' work](#rules-that-protect-users-work) before changing anything.

RabidPhatt is a monophonic VST® 3 synthesizer for Windows, with 66 sawtooth oscillators that a single Motion control pulls apart and brings back together. It uses the Steinberg VST3 SDK and VSTGUI directly. JUCE was deliberately rejected, so don't introduce it.

`docs/signal-path.md` is the behaviour specification. `README.md` covers building and usage.

Code shared with the other Hadronyche plug-ins is in the `HadronycheCore` submodule, at `external/hadronychecore`: the parameters' curves and saved format, `PanelEditor`, `Smoother`, the test host and the build setup. Its `AGENTS.md` has its own rules.

## Commands

Windows x64 with Visual Studio 2022 and CMake 3.25 or newer is where this is developed. macOS is built and tested by CI, on Intel and Apple Silicon, so see [macOS](#macos).

```powershell
# Clone. Without long paths, cloning into a folder deeper than about 100 characters fails partway
# through the SDK, because some of its sample files exceed the Windows path limit.
git -c core.longpaths=true clone --recurse-submodules <repo-url>

# In an existing clone, if submodules are missing
git -c core.longpaths=true submodule update --init --recursive

# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build. This also runs the Steinberg validator, and a failure fails the build.
cmake --build build --config Release --target RabidPhatt
cmake --build build --config Debug --target RabidPhatt

# Run the validator on its own, for example with extra output
build/bin/Release/validator.exe build/VST3/Release/RabidPhatt.vst3

# Build and run all the tests, which takes about 20 seconds in Release and 80 in Debug
cmake --build build --config Release --target RabidPhattTests
ctest --test-dir build -C Release --output-on-failure -j 4

# Run a single test, and list them
ctest --test-dir build -C Release --output-on-failure -R "^motion-spread$"
ctest --test-dir build -C Release -N

# Install for testing in Cubase. Close Cubase first, and use an elevated shell.
Copy-Item -Recurse -Force build/VST3/Release/RabidPhatt.vst3 "C:\Program Files\Common Files\VST3\"
```

There's no linter or formatter.

Follow the existing style, which comes from the SDK: a space before call parentheses (`foo (x)`), `_member` for private members, `kName` for constants, and comments that explain why rather than what.

## Rules that protect users' work

People use this plug-in in real projects. Breaking these rules damages saved projects, and usually nothing fails at build time.

### Saved projects

- **Never change the Audio Unit codes,** `aumu` `RbPh` here and the manufacturer `Hdrn` in HadronycheCore. Hosts use them to find the plug-in in saved projects.
- **Never change the class IDs in `src/PluginConfig.h`.** Hosts use them to find the plug-in in saved projects.
- **Never renumber, reorder or remove entries in `ParamId`** (`src/Parameters.h`). Their values are saved in projects and automation. Add new parameters just before `kParamCount`, and add the matching row to `kSpecs` in `src/Parameters.cpp`, which is listed in the same order.
- **Don't change the range or curve of an existing parameter.** Hosts store values normalized to 0..1, so this silently changes what saved projects sound like. If it's unavoidable, migrate old values and bump the state version.
- **Only ever add to the end of `kLfoDivisions`,** and keep `kLfoDivisionQuarters` in step with it. Projects save a position in the list.
- **Changing the saved format requires bumping `kStateVersion` and still reading every older version.** The format is `int32` version, `int32` count, then `int32` ID and `double` normalized value pairs (`writeParamState`). A state without a parameter leaves it at its default, and an unknown one is skipped.
- **Don't change the oscillators' start phases or their seeds without a good reason.** The same notes render the same way every time, and people will rely on that.
- **Loading must tolerate missing or damaged data.**

### The audio thread

`Processor::process` and everything it calls runs on the audio thread, including all of `src/dsp/`.

- **No allocation, locks, file access, system calls or anything else that can block.** This includes `std::vector` growth, `std::string` and `std::function` copies. The failure is audio dropouts that may not show up in testing.

### Sound and timing

- **There's no latency, and `getLatencySamples` stays at the base class's 0.** This is an instrument, so anything that adds latency, such as oversampling, makes it worse to play.
- **The oscillators' anti-aliasing must stay in step with their fade.** `Hadronyche::stepCorrection` reaches 2 samples each side of a wrap, which only works up to a quarter of the sample rate. That's why oscillators fade out by there.
- **`Hadronyche::Smoother` must stay double precision.** In single precision it stalls just short of its target, which would leave a held note out of tune after a glide.
- **While the transport plays, a synced LFO's phase is calculated from the host's musical position.** Don't accumulate it instead, or it will drift off the grid.
- **Motion's modulation isn't smoothed, and mustn't be.** A smoother slow enough to matter would eat the top of the LFO's range. `Motion` itself is smoothed before the LFO is added.

### Decisions made by the project owner

These were chosen deliberately, and were tried in Cubase, so don't "fix" them without the owner's agreement:

- Each moving row plays `frequency × (1 + Motion × multiplier)`, converging on the note and diverging from it, both ways. The diagrams in `docs/images/` draw `frequency × Motion × multiplier`, which was built first and rejected.
- The multipliers are the diagrams' divided by 10. The right bank's last row is -0.42, because the diagram's -4.1 is a typo for -4.2.
- Motion goes from 0.001 to 1, on a logarithmic knob, so that the rows are never exactly in sync.
- Cancellation between rows is part of the sound. Don't add anything to prevent it. The level also moves by itself from beating, by up to about 7 dB, and that's intended.
- The LFO is sine only, to keep the synth simple. The owner has rejected more shapes in advance.
- It's monophonic, and the design has no velocity.

In `docs/signal-path.md`, **Requirement** marks the owner's decisions and **Proposed** marks suggestions that were accepted by default. Update it when behaviour changes, and don't turn a Proposed item into a Requirement yourself.

### The repository

- **Don't edit anything in `external/vst3sdk`.** It's a submodule pinned to `v3.8.1_build_84`. `git submodule status` shows a misleading `v3.7.3...` label for that same commit, so check with `git -C external/vst3sdk describe --tags`.
- **Don't edit `external/hadronychecore` from here.** Change it in its own repository, and move the submodule here afterwards, once the new version has been compared with the old as its `AGENTS.md` describes.
- Don't change git configuration. Commit or push only when asked.

## Architecture

### Signal path

`Processor::render` runs this for every sample:

```
Note stack → Glide ─┬→ Left bank ──┐
                    └→ Right bank ─┴→ Amplifier (amp envelope) → Low-pass (filter envelope) → Volume → Out
       Motion knob + sine LFO ─→ both banks
```

### Threads

| Thread | Runs | Talks to the others through |
| --- | --- | --- |
| Audio | `Processor::process` | Atomics only |
| UI | `Editor`, on a 33 ms timer | The controller's parameters |
| Host | `setState`, `getState`, `setActive` | Atomics |

The processor and controller share no memory, so the plug-in is marked as distributable. `getParamSpec` is the one definition of every parameter's range and text, shared by `Processor`, `Controller` and `Editor`. The last parameter value per block is used, followed by smoothing.

### The voice

- **Events** are handled on their exact sample, by rendering up to each one in `Processor::process`.
- **The gate** is high while any key is held, so legato moves the pitch and leaves the envelopes and LFO delay alone.
- **Sleep:** once both envelopes are idle and the filter's tail has gone, the voice stops running and reports silence through the silence flags. The oscillators pause rather than run on, which is what makes renders repeatable. Smoothers snap to their targets at the start of a sleeping block, so that a parameter change arriving with a note doesn't ramp audibly.
- **The oscillators** are band-limited with a 4-point polynomial correction around each wrap. Only the first row plays the note exactly.

### The editor

`Editor.cpp` builds the panel in `buildPanel`, on `Hadronyche::PanelEditor`, with no `.uidesc` file. A 33 ms timer in `PanelEditor` pushes parameter values to the controls.

- A control's tag is its `ParamId`.
- The rate knob and division menu share a column, and `onSync` shows whichever applies.

## Verifying a change

Before calling a change done:

1. **Build Debug and Release, and run the tests on both.** The validator runs as part of the build, but it only proves that the plug-in follows the VST3 rules.
2. **Add a test for any new behaviour,** and check that it fails without your change. This has caught tests that passed for the wrong reason here more than once.
3. **Test it in Cubase** if you can, or say clearly that you couldn't. The tests measure the output, and nothing about how it sounds. Don't report behaviour you haven't observed as working.

### The tests

`.github/workflows/ci.yml` builds Debug and Release and runs all of this on every push and pull request. Don't weaken or skip it to get a change through.

`tests/` builds one program, `RabidPhattTests`, which loads the built plug-in and drives it through the VST3 interfaces, the way a host does. It doesn't link the plug-in's code, so tests can only see what a host can.

| File | Covers |
| --- | --- |
| `CompatibilityTests.cpp` | Class IDs, parameter IDs and ranges, the saved format and the buses |
| `OscillatorTests.cpp` | The banks' rows, Motion and its spread, the LFO and its sync, levels and aliasing |
| `VoiceTests.cpp` | Notes, glide, envelopes, filter, volume, state and parameter text |
| `PanelTest.cpp` | The real editor, driven with mouse messages |

- **Never change an expected value in `CompatibilityTests.cpp` to make a test pass.** Those values are written out by hand on purpose, and a failure there means the change would damage saved projects. Adding new parameters to them is fine, and expected.
- **Don't loosen a limit elsewhere to get a test passing** without working out why it failed.
- **To add a test,** add it to `Tests.h`, `kTests` in `TestMain.cpp` and `RABIDPHATT_TESTS` in `tests/CMakeLists.txt`.
- **`PanelTest.cpp` clicks fixed positions,** so moving the panel's controls means updating the positions at the top of that file. It can't open the division menu, whose pop-up would block it.
- **To see the panel,** set `RABIDPHATT_SCREENSHOTS` to a folder before running the `panel` test, which saves a BMP at each step.
- **To measure CPU use,** run `build/bin/Release/RabidPhattTests.exe <plug-in DLL> cpu`, in a Release build. The DLL is at `build/VST3/Release/RabidPhatt.vst3/Contents/x86_64-win/RabidPhatt.vst3`.

These tests run much faster than real time, which matters when writing new ones:

- With many saws at different phases, any single harmonic can be in a null, and zero crossings mean little. `measureFrequency` uses autocorrelation for this reason, and it's better to sum several harmonics than to read one.
- The level beats, so compare with a fixed Motion, or an identical instance, rather than with a fixed number.
- A window has to be long enough for what it measures: scanning for narrow lines needs fine steps, and an average needs several cycles of the slowest row.

## Releasing

Pushing a tag `v<version>` publishes a release, once everything has passed:

1. Set the version in `project()` in `CMakeLists.txt`. CI fails if the tag doesn't match it.
2. Add `docs/release-notes/v<version>.md`, which becomes the release's text.
3. Commit, push, and wait for CI to pass.
4. Tag that commit and push the tag.

CI builds the installer on every push, installs it on its own machine, runs the validator and all the tests on the installed copy, and uninstalls it. `installer/test-installer.ps1` writes to Program Files, so don't run it on a machine you care about.

Rules for releases:

- **Don't change `AppId` in `installer/RabidPhatt.iss`.** It's how an upgrade finds the previous version.
- **Keep `SMTG_USE_STATIC_CRT` on,** which HadronycheCore's `cmake/HadronychePlugin.cmake` does. Without it, the plug-in fails to load on PCs without Microsoft's runtime, and neither the tests nor CI would notice, because that runtime is on those machines. If you configured a build before this option existed, its cached value overrides the default.
- **The installer must keep shipping the licence files.** The VST 3 SDK's, VSTGUI's and HadronycheCore's licences require their notices to accompany binaries.
- **Follow Steinberg's usage guidelines** (`external/vst3sdk/VST3_Usage_Guidelines.pdf`) wherever "VST" appears in product-related material: use ® on first mention, include the sentence "VST is a registered trademark of Steinberg Media Technologies GmbH.", show the logo from `docs/images/` unaltered, and don't put "VST" in product or company names.

## macOS

- **Build with the Xcode generator,** which the SDK's Audio Unit wrapper needs: `cmake -S . -B build -G Xcode`, then `cmake --build build --config Release --target RabidPhattTests RabidPhatt RabidPhatt-au`.
- **`CMAKE_OSX_DEPLOYMENT_TARGET` has to stay before `project ()`,** or the plug-in only loads on macOS as new as the build machine, and only an older Mac would notice. `test-installer.sh` checks for this, because a host on the build machine wouldn't.
- **The SDK's `-ffast-math` stays off,** as HadronycheCore's AGENTS.md explains.
- **The installer is built and tested by HadronycheCore's `tools/macos/`.** `test-installer.sh` writes to `/Library`, so it's meant for CI machines.
- **The panel test only exists on Windows,** because it uses Windows' mouse messages. Nothing automated opens the panel on macOS, so its look needs checking by eye.

## Linux

- **Use one build folder per configuration, with Ninja:** `cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build/Release --target RabidPhattTests RabidPhatt`. The SDK's Linux output paths break with a multi-configuration generator.
- **`external/hadronychecore/tools/linux/install-build-packages.sh` installs the build's packages.** gtkmm and SQLite are only needed to configure, and the plug-in doesn't link them.
- **`panel-opens` needs an X server,** so run `xvfb-run -a ctest ...` where there's no display. It opens and closes the real editor, and nothing more: `panel`, which drives it, only exists on Windows, so the panel's look and its controls need checking by eye.
- **The version built on a machine only loads on a glibc as new as that machine's.** Nothing here checks that yet, and nothing is released for Linux yet.

## Known limits

- It has only been used on Windows, and Cubase is the only host it's been used in.
- There's no limiter. Peaks measured just under full scale with the filter open, but they depend on how the rows' phases line up, and high resonance can go over.
- The installer isn't code-signed, so Windows warns users when they run it. The macOS one isn't signed by Apple or notarized, so macOS refuses it until the user allows it.
- The Linux version has only been tried briefly, in REAPER on Debian 13, by the owner.
- The Mac version hasn't been used in a real host on a real Mac. The tests measure its output, and Apple's `auval` validates it, on CI only.
