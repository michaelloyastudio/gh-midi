# GH MIDI

Turn a Guitar Hero controller into a real MIDI instrument.

A VST3 plugin and standalone app (macOS Apple Silicon + Intel, and Windows
x64) that reads a Guitar Hero controller over USB HID and plays it like an
instrument — inside your DAW, or as a MIDI controller for anything — with a
real-time 3D note highway rendered in OpenGL.

**Full guide:** https://michaelloya.studio/gh-midi

## Modes

- **CHORDS** — every fret is a chord in the current key (green I, red V,
  yellow vi, blue IV, orange ii). Fret combos add sevenths and the borrowed
  chords the five frets can't make. You cannot play a wrong note.
- **NOTES** — the frets are a binary number (G=1 R=2 Y=4 B=8 O=16) selecting
  fully chromatic notes: 32 per octave position. A real instrument you have
  to learn.
- **SOLO** — each fret is one note of the key's pentatonic scale. Every note
  fits over every chord.
- **CHART** — each fret sends its Clone Hero lane note, all four difficulties
  at once. Record a rough chart in your DAW, export the .mid, finish it in
  Moonscraper.

Whammy = pitch bend (+ CC20 for knob-linking). Minus cycles modes, plus taps
through strum speeds, the joystick changes key (left/right) and octave
(up/down, ±3 octaves in every mode) — and on-screen arrows do all four for
controllers without a joystick. Any HID controller can be mapped
from the in-plugin settings (per-control LEARN, live input testing).

**No DAW required.** The release also ships a standalone app: while it runs
on a Mac, every DAW and synth app sees a MIDI input called "GH MIDI" (Logic,
GarageBand, Ableton, FL, Reaper, ...). Windows has no virtual MIDI ports, so
there the app's SETTINGS panel has a MIDI OUT picker instead: a
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) port to
reach a DAW, or any real port (the Microsoft GS Wavetable Synth works as an
instant test). Run either the app or the plugin, not both — only one process
can hold the guitar.

On macOS the plugin also publishes the virtual "GH MIDI" source so DAWs
record your performance as editable notes.

**Windows is a beta.** The Windows zip is built on GitHub's Windows runners
(there is no Windows machine in this project) and has not been played on real
Windows hardware yet. Xbox 360 guitars (XInput) are not supported; Wii guitars
through a USB adapter and PlayStation guitars enumerate as HID and should
work. Please report what you find.

## Building

JUCE 8 and hidapi are fetched automatically by CMake (≥ 3.24).

**macOS** (Xcode command line tools):

```
cd plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The VST3 installs to `~/Library/Audio/Plug-Ins/VST3` after a successful
build. A Standalone app is also built; run it with `GHMIDI_DEMO=1` for a
self-playing demo (no controller needed).

**Windows** (Visual Studio 2022 with the Desktop C++ workload):

```
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64
cmake --build plugin/build --config Release
```

Outputs land in `plugin/build/GHMidi_artefacts/Release/`: the `VST3/GH
MIDI.vst3` folder and `Standalone/GH MIDI.exe`. Every push to `main` also
builds this on GitHub Actions (`.github/workflows/windows.yml`); the zip is
the workflow's artifact, and running the workflow by hand with a release tag
attaches it to that release.

## Porting

The engine (`plugin/src/PluginProcessor.cpp`) is plain JUCE + hidapi; the
only platform seams are the virtual MIDI source (macOS/Linux) versus the
MIDI OUT picker (Windows) and the settings path. Linux should need little
beyond a build. Still open: an XInput backend for Xbox 360-era guitars,
which don't speak HID. PRs welcome.

`tools/` scripts from the prototyping era (Python HID dump / mapper /
MIDI bridge) are kept for adapter debugging.

## Made with Claude Code

The plugin, the README and the user guide were made with Claude Code. Some
details may be inaccurate; the code is the reference.

## License

AGPL-3.0 (JUCE is used under its AGPLv3 option). The bundled Metal Mania
font is by Open Window under the SIL Open Font License — see
`plugin/assets/OFL-MetalMania.txt`.
