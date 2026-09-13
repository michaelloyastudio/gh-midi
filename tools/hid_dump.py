#!/usr/bin/env python3
"""Live raw-report viewer for the Wii Guitar Hero guitar (raphnet WUSBMote adapter).

Run it, then press frets / strum / wiggle the whammy. Each report the guitar
sends is printed as hex; bytes that changed since the last report light up green.
Ctrl+C to quit.
"""

import sys
import time
import hid

VID, PID = 0x289B, 0x0080

GREEN = "\033[1;92m"
DIM = "\033[2m"
RESET = "\033[0m"


def main():
    try:
        dev = hid.Device(VID, PID)
    except Exception as e:
        sys.exit(f"could not open the guitar adapter: {e}")

    print(f"reading: {dev.manufacturer} / {dev.product}")
    print("press things on the guitar (ctrl+C to quit)\n")

    prev = None
    try:
        while True:
            try:
                data = bytes(dev.get_input_report(1, 32))
            except Exception:
                time.sleep(0.05)
                continue
            if not data or data == prev:
                time.sleep(0.004)
                continue
            cells = []
            for i, b in enumerate(data):
                changed = prev is not None and (i >= len(prev) or prev[i] != b)
                cells.append(f"{GREEN}{b:02x}{RESET}" if changed else f"{DIM}{b:02x}{RESET}")
            print(" ".join(cells))
            prev = data
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
