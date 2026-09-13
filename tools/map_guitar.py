#!/usr/bin/env python3
"""Guided input mapper for the Wii Guitar Hero guitar (raphnet WUSBMote adapter).

Walks through every control one at a time, records the raw HID report for each,
and writes the results to mapping.json. Follow the prompts; if your guitar
doesn't have a control (e.g. touch strip), just press ENTER without touching it
and it gets marked absent.
"""

import json
import sys
import time
from pathlib import Path

import hid

VID, PID = 0x289B, 0x0080
OUT = Path(__file__).parent / "mapping.json"

BUTTONS = [
    ("green", "HOLD the GREEN fret"),
    ("red", "HOLD the RED fret"),
    ("yellow", "HOLD the YELLOW fret"),
    ("blue", "HOLD the BLUE fret"),
    ("orange", "HOLD the ORANGE fret"),
    ("strum_up", "push the strum bar UP and HOLD it"),
    ("strum_down", "push the strum bar DOWN and HOLD it"),
    ("plus", "HOLD the + button"),
    ("minus", "HOLD the - button"),
]

ANALOGS = [
    ("whammy", "sweep the WHAMMY bar all the way down and back up, a few times"),
    ("touch_strip", "slide a finger along the TOUCH STRIP end to end (ENTER first if none)"),
]


def drain(dev):
    """Read until quiet; return the last report seen (or None)."""
    last = None
    while True:
        data = dev.read(64, 120)
        if not data:
            return last
        last = data


def sample_window(dev, seconds):
    """Collect all reports for `seconds`; return list of bytes objects."""
    out = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        data = dev.read(64, 100)
        if data:
            out.append(bytes(data))
    return out


def main():
    try:
        dev = hid.Device(VID, PID)
    except Exception as e:
        sys.exit(f"could not open the guitar adapter: {e}")

    print(f"mapping: {dev.manufacturer} / {dev.product}\n")
    print("first, tap the GREEN fret once (press and release) so we can grab")
    input("the guitar's idle state, then press ENTER... ")
    drain(dev)
    time.sleep(0.3)
    idle = drain(dev)
    if idle is None:
        # no report seen yet; poke the user once more
        input("didn't see anything — tap green again, wait a beat, then ENTER... ")
        idle = drain(dev)
    if idle is None:
        sys.exit("still no reports from the guitar; is it on and plugged in?")
    idle = bytes(idle)
    print(f"\nidle state: {idle.hex(' ')}\n")

    result = {
        "device": {"vid": VID, "pid": PID, "product": dev.product},
        "idle": idle.hex(" "),
        "buttons": {},
        "analogs": {},
    }

    for name, prompt in BUTTONS:
        input(f"--> {prompt}, then press ENTER (keep holding)... ")
        reports = sample_window(dev, 1.0)
        held = reports[-1] if reports else drain(dev)
        if held is None or bytes(held) == idle:
            print(f"    no change seen — marking {name} absent\n")
            result["buttons"][name] = None
        else:
            held = bytes(held)
            diff = [i for i in range(min(len(held), len(idle))) if held[i] != idle[i]]
            print(f"    held: {held.hex(' ')}  (changed bytes: {diff})\n")
            result["buttons"][name] = {"held": held.hex(" "), "changed": diff}
        input("    release it, then press ENTER... ")
        drain(dev)

    for name, prompt in ANALOGS:
        input(f"--> press ENTER, then {prompt} for ~3s... ")
        reports = sample_window(dev, 3.5)
        if not reports:
            print(f"    no reports — marking {name} absent\n")
            result["analogs"][name] = None
            continue
        length = min(len(r) for r in reports)
        spans = {}
        for i in range(length):
            vals = [r[i] for r in reports]
            span = max(vals) - min(vals)
            if span > 8:
                spans[i] = {"min": min(vals), "max": max(vals), "rest": reports[-1][i]}
        print(f"    {len(reports)} reports, moving bytes: {spans}\n")
        result["analogs"][name] = {"bytes": spans, "samples": len(reports)}
        drain(dev)

    OUT.write_text(json.dumps(result, indent=2))
    print(f"\ndone — wrote {OUT}")
    dev.close()


if __name__ == "__main__":
    main()
