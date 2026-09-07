# Spectrum Canvas for OBS

Spectrum Canvas is a native OBS source plugin for flexible, real-time audio
visualization. It listens to any audio-producing OBS source and draws directly
through the OBS graphics API, so there is no browser-source latency or external
web service.

## Included styles

- Spectrum bars
- Mirrored spectrum
- Spectrum line
- Oscilloscope waveform
- Radial spectrum bars
- Circular spectrum line
- Spectrum dots

The mirrored style can mirror top/bottom, left/right, or both. For horizontal
mirroring, bass can sit in the center or on the two outside edges. Bars can use
flat or rounded outer caps.

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

1. Add **Spectrum Canvas** from the Sources panel.
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

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
