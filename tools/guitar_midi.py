#!/usr/bin/env python3
"""Wii Guitar Hero guitar -> virtual MIDI port for FL Studio.

Modes (minus button cycles EASY -> REAL -> NOTES):
  EASY   - each fret is a chord in the current key (green I, red V, yellow vi,
           blue IV, orange ii). Strum down = full voicing rolled low->high,
           strum up = brighter voicing rolled high->low. Open strum = bass root.
           Plus button shifts the key up a semitone.
  REAL   - frets are a binary number (G=1 R=2 Y=4 B=8 O=16) choosing a
           chromatic note from E2 up (32 notes). Changing the combo while a
           note rings = hammer-on/pull-off (no strum needed). Releasing all
           frets mutes. Plus button shifts octave (E2/E3/E4).
           Fingering chart written to real_chart.txt.
  NOTES  - the original five-note mode: frets = C4 D4 E4 F4 G4, open strum C3.

Whammy = pitch bend (down, up to 2 semitones). Polls the adapter (~250Hz);
its push reports are unreliable. Diagnostics heartbeat -> bridge.log.
"""

import sys
import time
from pathlib import Path

import hid
import mido

VID, PID = 0x289B, 0x0080
PORT_NAME = "GH Guitar"
POLL_S = 0.004  # ~250Hz
LOG = Path(__file__).parent / "bridge.log"
CHART = Path(__file__).parent / "real_chart.txt"

# byte 13 masks (from mapping.json)
FRET_MASKS = [0x01, 0x02, 0x04, 0x08, 0x10]  # green red yellow blue orange
FRET_LABELS = ["green", "red", "yellow", "blue", "orange"]
STRUM_DOWN = 0x20  # byte 13
STRUM_UP = 0x01    # byte 14
PLUS = 0x40        # byte 13
MINUS = 0x80       # byte 13

MODES = ["easy", "real", "notes"]

# NOTES mode: fret -> midi note
SIMPLE_NOTES = [60, 62, 64, 65, 67]  # C4 D4 E4 F4 G4
SIMPLE_OPEN = 48                     # C3

# EASY mode: fret -> (scale degree offset, quality)  == I, V, vi, IV, ii
DEGREES = [(0, "maj"), (7, "maj"), (9, "min"), (5, "maj"), (2, "min")]
CHORD_ROOT_BASE = 48  # key root C3; voicings span roughly C2..C5
BASS_BASE = 36        # open-strum bass root C2
VEL_DOWN = 100
VEL_UP = 78
VEL_LEGATO = 70
ROLL_S = 0.010  # delay between notes of a rolled chord

# REAL mode: binary frets, chromatic from E2
REAL_BASE = 40            # E2
LEGATO_STABLE_S = 0.025   # combo must hold this long to hammer-on/pull-off

# whammy: bytes 11-12, 16-bit little-endian, rests high, pressed -> 0
WHAMMY_REST = 56536
BEND_DEADZONE = 0.03

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def combo_pattern(combo):
    return "".join(l[0].upper() if combo & m else "-"
                   for l, m in zip(FRET_LABELS, FRET_MASKS))


def write_chart():
    lines = ["REAL MODE FINGERING CHART  (G R Y B O, - = not pressed)",
             f"base octave shown from {note_name(REAL_BASE)}; plus button shifts octave", ""]
    for combo in range(32):
        lines.append(f"  {combo_pattern(combo)}   {note_name(REAL_BASE + combo):>4}")
    CHART.write_text("\n".join(lines) + "\n")


def chord_for(fret, key, direction):
    """Return (notes_in_send_order, chord_label) for a fret in the given key."""
    degree, quality = DEGREES[fret]
    root = CHORD_ROOT_BASE + key + degree
    third = root + (4 if quality == "maj" else 3)
    fifth = root + 7
    label = NOTE_NAMES[root % 12] + ("m" if quality == "min" else "")
    if direction == "down":
        notes = [root - 12, root, third, fifth, root + 12]
    else:  # up-strum: no low root, brighter, rolled high->low
        notes = [third + 12, root + 12, fifth, third, root]
    return [n for n in notes if 0 <= n <= 127], label


def main():
    logf = open(LOG, "w", buffering=1)

    def emit(line):
        print(line)
        logf.write(line + "\n")

    try:
        dev = hid.Device(VID, PID)
    except Exception as e:
        emit(f"could not open the guitar adapter: {e!r}")
        sys.exit(1)
    out = mido.open_output(PORT_NAME, virtual=True)
    write_chart()
    emit(f"guitar connected, MIDI port '{PORT_NAME}' is live (polling mode)")
    print("mode: EASY (minus button cycles EASY/REAL/NOTES, plus = key/octave)")
    print("key of C — green=C  red=G  yellow=Am  blue=F  orange=Dm")
    print(f"real-mode fingering chart: {CHART}")
    print("ctrl+C to quit\n")

    mode = "easy"
    key = 0          # semitones: easy = key above C, real = base above E2
    octave = 0       # real mode: 0/12/24 above E2
    easy_oct = 0     # easy mode: 0/+12/-12
    joy_pos = "center"  # joystick X state for transpose flicks
    strummed = False
    prev_b13 = 0
    ringing = set()
    ring_fret = None   # easy mode: fret of the ringing chord
    ring_combo = None  # real mode: combo of the ringing note (None = nothing/open)
    cand_combo = None  # real mode: legato candidate combo
    cand_since = 0.0
    bend = 0
    prev = None
    polls = errs = notes_sent = shorts = 0
    last_short = None
    first_err = None
    last_status = time.time()

    def all_off():
        for n in ringing:
            out.send(mido.Message("note_off", note=n))
        ringing.clear()

    def key_layout():
        names = [chord_for(i, key, "down")[1] for i in range(5)]
        return "  ".join(f"{FRET_LABELS[i]}={names[i]}" for i in range(5))

    def real_note(combo):
        return REAL_BASE + key + octave + combo

    try:
        while True:
            now = time.time()
            if polls == 500 and shorts > 450:
                emit("")
                emit("⚠️  the adapter is answering EMPTY — it can't see the guitar.")
                emit("   fix: ctrl+C, unplug the adapter's USB, reseat the guitar")
                emit("   plug, then plug USB back in and rerun this.")
                emit("")
            if now - last_status >= 2.0:
                state = prev.hex(" ") if prev else "none"
                emit(f"[{time.strftime('%H:%M:%S')}] polls_ok={polls} errors={errs}"
                     f" short_reports={shorts} notes_sent={notes_sent} mode={mode}"
                     f" key={NOTE_NAMES[key]} state={state}"
                     + (f" last_short={last_short}" if last_short else "")
                     + (f" first_error={first_err!r}" if first_err else ""))
                last_status = now

            try:
                data = bytes(dev.get_input_report(1, 32))
                polls += 1
            except Exception as e:
                errs += 1
                if first_err is None:
                    first_err = e
                time.sleep(0.05)
                continue
            if len(data) < 15:
                shorts += 1
                last_short = f"len={len(data)}:{data.hex(' ') if data else 'empty'}"
                time.sleep(POLL_S)
                continue
            prev = data

            b13, b14 = data[13], data[14]
            combo = b13 & 0x1F
            held = [i for i, m in enumerate(FRET_MASKS) if b13 & m]
            down = bool(b13 & STRUM_DOWN)
            up = bool(b14 & STRUM_UP)
            strum = down or up

            # minus: cycle mode / plus: key up (easy) or octave up (real)
            if (b13 & MINUS) and not (prev_b13 & MINUS):
                all_off()
                ring_fret = ring_combo = cand_combo = None
                mode = MODES[(MODES.index(mode) + 1) % len(MODES)]
                emit(f"== mode: {mode.upper()} ==")
                if mode == "easy":
                    emit(f"key of {NOTE_NAMES[key]} — {key_layout()}")
                elif mode == "real":
                    emit(f"binary frets from {note_name(REAL_BASE + octave)}"
                         f" — chart in {CHART.name}")
            if (b13 & PLUS) and not (prev_b13 & PLUS):
                if mode == "real":
                    octave = (octave + 12) % 36
                    emit(f"== octave: from {note_name(REAL_BASE + key + octave)} ==")
                else:
                    easy_oct = 12 if easy_oct == 0 else (-12 if easy_oct == 12 else 0)
                    emit(f"== octave: {'+1' if easy_oct > 0 else '-1' if easy_oct < 0 else 'normal'} ==")
            prev_b13 = b13

            # joystick X flick = transpose a semitone (right +1, left -1)
            jx = data[1] | (data[2] << 8)
            if joy_pos == "center":
                delta = 1 if jx > 26000 else (-1 if jx < 6000 else 0)
                if delta:
                    joy_pos = "off"
                    key = (key + delta) % 12
                    if mode == "real":
                        emit(f"== base: {note_name(REAL_BASE + key + octave)} ==")
                    else:
                        emit(f"== key of {NOTE_NAMES[key]} — {key_layout()} ==")
            elif 10000 < jx < 22000:
                joy_pos = "center"

            # strum edge
            if strum and not strummed:
                direction = "down" if down else "up"
                vel = VEL_DOWN if direction == "down" else VEL_UP
                all_off()
                cand_combo = None
                if mode == "easy":
                    if held:
                        fret = max(held)  # highest fret wins
                        notes, label = chord_for(fret, key + easy_oct, direction)
                        for i, n in enumerate(notes):
                            out.send(mido.Message("note_on", note=n, velocity=vel))
                            ringing.add(n)
                            if i < len(notes) - 1:
                                time.sleep(ROLL_S)
                        ring_fret = fret
                        emit(f"♪ {label} ({direction})")
                    else:
                        bass = BASS_BASE + key + easy_oct
                        out.send(mido.Message("note_on", note=bass, velocity=VEL_DOWN))
                        ringing.add(bass)
                        ring_fret = None
                        emit(f"♪ {note_name(bass)} (bass)")
                elif mode == "real":
                    n = real_note(combo)
                    out.send(mido.Message("note_on", note=n, velocity=vel))
                    ringing.add(n)
                    ring_combo = combo
                    emit(f"♪ [{combo_pattern(combo)}] {note_name(n)}")
                else:  # NOTES mode
                    notes = sorted(SIMPLE_NOTES[i] for i in held) or [SIMPLE_OPEN]
                    for n in notes:
                        out.send(mido.Message("note_on", note=n, velocity=VEL_DOWN))
                        ringing.add(n)
                    ring_fret = None
                    emit("♪ " + " ".join(note_name(n) for n in notes))
                notes_sent += len(ringing)
            strummed = strum

            # release / legato behavior
            if mode == "easy":
                if ring_fret is not None and ring_fret not in held:
                    all_off()
                    ring_fret = None
            elif mode == "real":
                if ring_combo is not None and ring_combo != 0:
                    if combo == 0:
                        # lifted all fingers: mute
                        all_off()
                        ring_combo = cand_combo = None
                    elif combo != ring_combo:
                        # hammer-on / pull-off after the combo holds steady
                        if cand_combo != combo:
                            cand_combo = combo
                            cand_since = now
                        elif now - cand_since >= LEGATO_STABLE_S:
                            all_off()
                            n = real_note(combo)
                            out.send(mido.Message("note_on", note=n,
                                                  velocity=VEL_LEGATO))
                            ringing.add(n)
                            ring_combo = combo
                            cand_combo = None
                            notes_sent += 1
                            emit(f"♪ [{combo_pattern(combo)}] {note_name(n)} (legato)")
                    else:
                        cand_combo = None
            else:
                for n in list(ringing):
                    if n != SIMPLE_OPEN and n not in {SIMPLE_NOTES[i] for i in held}:
                        out.send(mido.Message("note_off", note=n))
                        ringing.discard(n)

            # whammy -> pitch bend down (up to 2 semitones, per synth setting)
            w = data[11] | (data[12] << 8)
            amt = max(0.0, min(1.0, (WHAMMY_REST - w) / WHAMMY_REST))
            if amt < BEND_DEADZONE:
                amt = 0.0
            pb = -int(8191 * amt)
            if pb != bend and (abs(pb - bend) > 64 or pb == 0):
                out.send(mido.Message("pitchwheel", pitch=pb))
                bend = pb
            time.sleep(POLL_S)
    except KeyboardInterrupt:
        pass
    finally:
        all_off()
        out.send(mido.Message("pitchwheel", pitch=0))
        out.close()
        dev.close()
        emit("bridge stopped")
        logf.close()
        print("\nbye")


if __name__ == "__main__":
    main()
