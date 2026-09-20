GH MIDI v1.1 - Windows
======================
Play a Guitar Hero controller like a real instrument.

This is the first Windows build. It was built on GitHub's Windows machines
and has not been played on a real Windows PC yet. If something is off, tell
me: hello@michaelloya.studio, or github.com/michaelloyastudio/gh-midi/issues


WHAT'S IN HERE

  GH MIDI.exe         the app. Sends the guitar out of a MIDI port you pick
                      in SETTINGS. It makes no sound on its own.
  GH MIDI.vst3        the plugin. FL Studio, Ableton, Reaper, any VST3 host.
                      It is a folder - keep it whole.


SETUP

  1.  App:     put GH MIDI.exe anywhere.
      Plugin:  put the GH MIDI.vst3 folder in
                   C:\Program Files\Common Files\VST3
               then rescan plugins in your DAW.

  2.  The first time you open the app, Windows says "Windows protected your
      PC". Click "More info", then "Run anyway". Once.

  3.  Plug in the guitar.
      Plugin:  load GH MIDI on a track, route its MIDI to an instrument. Play.
      App:     it needs a MIDI port to talk through.
               Quick test:  SETTINGS > MIDI out > Microsoft GS Wavetable Synth.
                            That's the piano built into Windows. Laggy, but
                            it proves the guitar works.
               With a DAW:  install loopMIDI (free, tobias-erichsen.de),
                            add a port, pick it under SETTINGS > MIDI out,
                            then enable that port as a MIDI input in your DAW.
      Run the app OR the plugin, not both. Whichever opens first gets the guitar.

  Xbox 360 guitars are not supported yet (they are XInput, not USB HID).
  Wii guitars through a USB adapter and PlayStation guitars show up as HID
  and should work. Not recognised? SETTINGS > pick it > LEARN each control.


HELP

  The ? button inside the app, or the guide PDF on the download page (its
  install page is for Mac; everything else applies).
  michaelloya.studio/gh-midi


MADE WITH CLAUDE CODE

  GH MIDI, this README and the guide were made with Claude Code. Some
  details may be inaccurate. If something doesn't match what you see, trust
  the plugin and tell me: hello@michaelloya.studio
