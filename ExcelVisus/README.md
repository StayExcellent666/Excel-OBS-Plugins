# ExcelVisus for OBS

ExcelVisus is a native OBS source plugin for flexible, real-time audio
visualization. It listens to any audio-producing OBS source and draws directly
through the OBS graphics API, so there is no browser-source latency or external
web service.

## Included styles

- Spectrum bars
- Mirrored spectrum
- Oscilloscope waveform
- Radial spectrum bars
- Circular spectrum line
- Spectrum dots

The mirrored style can mirror top/bottom, left/right, or both. For horizontal
mirroring, bass can sit in the center or on the two outside edges. Bars can use
flat or rounded outer caps, including radial bars. Standard and radial bar
styles can also render adjustable layered neon glow halos.
Rounded bars keep a small baseline-attached cap even during silence. When glow
is enabled, a tightly clamped idle halo stays attached to that cap instead of
forming detached glow dots around the baseline.

Every style shares the same analysis and color engine. You can change canvas
size, frequency range, band count, FFT resolution, logarithmic/linear spacing,
gain, noise floor, attack/release smoothing, peak hold, channel selection,
padding, spacing, line/dot dimensions, radial arc and rotation, transparent
backgrounds, solid colors, two-color gradients, and animated rainbow colors.
Gradients can run across frequency bands or respond to loudness.
An optional waveform, spectrum-line, or dot overlay can be combined with any
primary style to create hybrid designs rather than relying only on presets.

Six one-click starting presets are included: Clean Rounded Bars, Neon Wings,
Bass Outside, Rainbow Orbit, Green Oscilloscope, and Sunset Dots.

## Compatibility

- Windows x64
- Tested with OBS Studio 32.2.2

## Install a release build

1. Close OBS Studio completely.
2. Download the latest Windows x64 ZIP from the GitHub Releases page.
3. Extract the ZIP and copy its `obs-plugins` and `data` folders into your OBS
   Studio installation folder.
4. Reopen OBS and add **ExcelVisus Visualizer** from the Sources panel.

The current release package uses OBS's legacy installation layout for custom or
portable OBS installations. A per-user/ProgramData installer is planned for a
future release.

## Understanding the analysis controls

- **Number of bars / points** controls the spectrum detail. A horizontal mirror
  renders that number on each side.
- **FFT resolution** is the number of recent audio samples analyzed together.
  `2048` is a balanced default, `4096` gives cleaner bass separation, and
  `1024` reacts faster for speech.
- **Logarithmic frequency spacing** gives low frequencies more room, matching
  musical pitch and human hearing. It is usually best for music.
- **Noise floor** is the quietest signal shown. A more negative value reveals
  quieter sound and background noise; a less negative value hides it.
- **Sensitivity / analysis gain** is the main movement control. Raise it for
  more bar movement at the same audio volume.
- **Attack** controls how quickly bars rise. **Release** controls how slowly
  they fall.

## Build

Requirements:

- OBS Studio development files (libobs headers, library, and CMake package)
- CMake 3.22 or newer
- A C++17 compiler (Visual Studio 2022 on Windows)

Point `CMAKE_PREFIX_PATH` at an OBS build/install tree that contains
`libobsConfig.cmake`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\path\to\obs-development-files"
cmake --build build --config Release
cmake --install build --config Release --prefix package
```

The install step creates an OBS-compatible directory layout under `package`.
Copy the contents of that directory into the OBS Studio installation directory,
or zip it for distribution. On Windows the built module is
`obs-plugins/64bit/obs-spectrum-canvas.dll` and locale data is under
`data/obs-plugins/obs-spectrum-canvas` in the OBS installation.

The DSP tests do not require OBS:

```powershell
cmake -S . -B build-tests -DSPECTRUM_CANVAS_BUILD_PLUGIN=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

## Use in OBS

1. Add **ExcelVisus Visualizer** from the Sources panel.
2. Choose the microphone, application audio, media source, or other audio source
   to visualize.
3. Select a visualization style and tune the grouped settings.
4. Leave the background transparent to layer the visualization over video.

For a musical spectrum, start with 64 bands, a 2048-point FFT, logarithmic
spacing, 40–16,000 Hz, a -72 dB floor, 45 ms attack, and 240 ms release.

## Architecture

The OBS audio callback only downmixes samples into a fixed-size ring buffer.
FFT, smoothing, waveform extraction, and geometry generation run on the video
thread. This keeps expensive work away from OBS's real-time audio path. The DSP
component has no OBS dependency and is covered by a standalone sine-wave test.

## Development disclosure

ExcelVisus was developed with substantial coding assistance from OpenAI Codex.
Feature direction and hands-on testing in OBS were performed by the project
owner. Releases are also checked with standalone DSP tests and an OBS module
initialization/source-registration smoke test. Bugs can be reported through the
repository's Issues page.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
