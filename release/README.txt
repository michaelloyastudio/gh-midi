GH MIDI v1.1
============
Turn a Guitar Hero controller into a real MIDI instrument.
by Loya  ·  full guide: https://michaelloya.studio/gh-midi


INSTALL (macOS)
---------------
1. Copy "GH MIDI.vst3" to:   ~/Library/Audio/Plug-Ins/VST3
2. macOS quarantines downloaded plugins. Open Terminal and run:
       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"GH MIDI.vst3"
3. Rescan plugins in your DAW. Add "GH MIDI" as an instrument/generator.

Works on Apple Silicon and Intel. VST3 hosts (FL Studio, Ableton, Reaper, ...).


YOUR CONTROLLER
---------------
Ships pre-mapped for a Wii Guitar Hero guitar on a raphnet WUSBMote adapter.
Other USB HID controllers: SETTINGS -> pick your device -> use the MAPPING
table: hit LEARN on a row, press that control on your guitar, done. Each row
can be re-learned or cleared (X) on its own. Every controller keeps its
own remembered setup - switch devices and your mappings follow.


HOW TO PLAY  (also in the "?" help inside the plugin)
-----------------------------------------------------
FRETS + STRUM     play (down and up strums are different voicings in CHORDS)
WHAMMY            pitch bend, and/or CC20 you can link to any knob (SETTINGS)
MINUS             switch mode:  CHORDS -> NOTES -> SOLO
PLUS              tap through strum speeds (0-50ms)
STICK             left/right = key, up/down = octave

MODES
  CHORDS  every fret is a chord in the current key (green I, red V,
          yellow vi, blue IV, orange ii). You cannot play a wrong note.
  SOLO    each fret is one note of the key's pentatonic scale - every note
          fits over every chord.
  NOTES   frets are a binary number (G=1 R=2 Y=4 B=8 O=16) choosing
          chromatic notes. 32 notes per octave position - a real instrument
          you have to learn.


RECORDING & ROUTING (FL Studio)
-------------------------------
Record NOTES: enable the "GH MIDI" device under Options > MIDI Settings >
Input - your playing records into the piano roll like a MIDI keyboard.
Route audio live: add GH MIDI inside Patcher, wire its green MIDI-out to any
instrument's green MIDI-in, instrument audio to "To FL Studio".


TROUBLESHOOTING
---------------
"CONTROLLER NOT FOUND" - plug it in, or SETTINGS > pick the device. If it
still won't appear, check System Settings > Privacy & Security > Input
Monitoring and allow your DAW.
"ADAPTER CAN'T SEE THE GUITAR" (WUSBMote) - unplug the adapter's USB, reseat
the guitar plug, plug USB back in.
No pitch bend from the whammy - some presets ignore bend; set whammy to CC20
in SETTINGS and link it to a pitch or filter knob (right-click the knob).

Every mode: strum to sound a note or chord - changing frets never
plays anything by itself. Release the fret to let it ring out.
