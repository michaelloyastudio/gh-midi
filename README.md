# GH MIDI

Turn a Guitar Hero controller into a real MIDI instrument.

A VST3 plugin and standalone app (macOS, Apple Silicon + Intel) that reads a
Guitar Hero controller over USB HID and plays it like an instrument — inside
your DAW, or as a MIDI controller for anything — with a real-time 3D note
highway rendered in OpenGL.

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

**No DAW required.** The release also ships a standalone macOS app: while it
runs, every DAW and synth app on the Mac sees a MIDI input called "GH MIDI"
(Logic, GarageBand, Ableton, FL, Reaper, ...). Run either the app or the
plugin, not both — only one process can hold the guitar.

The plugin also publishes a virtual MIDI source ("GH MIDI") so DAWs record
your performance as editable notes.

## Building

Requirements: macOS, CMake ≥ 3.24, Xcode command line tools. JUCE 8 and
hidapi are fetched automatically.

```
cd plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The VST3 installs to `~/Library/Audio/Plug-Ins/VST3` after a successful
build. A Standalone app is also built; run it with `GHMIDI_DEMO=1` for a
self-playing demo (no controller needed).

## Porting

The engine (`plugin/src/PluginProcessor.cpp`) is plain JUCE + hidapi and
should port to Windows/Linux with modest effort. Known Windows notes: the
virtual MIDI source needs a helper like loopMIDI (Windows has no built-in
virtual MIDI ports), and XInput-only controllers (Xbox 360 era) would need
an additional input backend beyond hidapi. PRs welcome.

`tools/` scripts from the prototyping era (Python HID dump / mapper /
MIDI bridge) are kept for adapter debugging.

## Made with Claude Code

The plugin, the README and the user guide were made with Claude Code. Some
details may be inaccurate; the code is the reference.

## License

AGPL-3.0 (JUCE is used under its AGPLv3 option). The bundled Metal Mania
font is by Open Window under the SIL Open Font License — see
`plugin/assets/OFL-MetalMania.txt`.
