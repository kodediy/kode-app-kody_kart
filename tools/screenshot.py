#!/usr/bin/env python3
"""What the Dot's glass shows, as a BMP, over the console on the cable.

    python3 tools/screenshot.py [out.bmp] [--port /dev/cu.usbmodem101]

The protocol is kodeOS's (packages/device-link/src/screenshot.ts): send
`screenshot`, collect every `>|d=<base64>` body, decode once, check `len=`.
Needs pyserial (ESP-IDF's Python has it). Close `kode console` first: one
owner per port.
"""
import base64
import glob
import re
import sys
import time

import serial

args = [a for a in sys.argv[1:] if not a.startswith("--port")]
out = args[0] if args else "shot.bmp"
port = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--port=")), None)
port = port or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
if not port:
    sys.exit("no /dev/cu.usbmodem* - is the Dot on the cable?")

def grab():
    """One attempt: the BMP bytes, or None when the transfer came back damaged."""
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        s.write(b"screenshot\n")
        header, chunks, last, buf = None, [], time.time(), b""
        while time.time() - last < 15:
            data = s.read(65536)
            if not data:
                continue
            last = time.time()
            buf += data
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode(errors="replace").strip()
                if line.startswith(">|shot"):
                    header = dict(kv.split("=", 1) for kv in line[2:].split()[1:])
                elif line.startswith(">|d="):
                    chunks.append(line[4:])
                elif line.startswith(">err"):
                    sys.exit(f"the Dot refused: {line}")
                elif line.startswith(">ok") and header:
                    try:
                        bmp = base64.b64decode("".join(chunks), validate=True)
                    except ValueError:
                        return header, None
                    return header, bmp if len(bmp) == int(header["len"]) else None
    return header, None


# A log line from another task can land in the middle of a record (the radio's
# Wi-Fi scan does): the transfer then fails its length check, and we ask again.
for attempt in range(3):
    header, bmp = grab()
    if bmp:
        break
    if not header:
        sys.exit("no screenshot header came back")
else:
    sys.exit("three damaged transfers in a row")
open(out, "wb").write(bmp)
print(f"{out}: {header['w']}x{header['h']} {header['fmt']}")
