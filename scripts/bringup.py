#!/usr/bin/env python3
"""
PCB bring-up from a plain terminal. Companion to docs/hardware-bringup.md,
which says which command to run at each step and what the meter must read.

Builds for the PCB (TX on GP11, build directory build-pcb/) and drives the
apps/bringup console over USB. Every subcommand is one line to the board and
the board's reply printed back; nothing is left running on the host.

    python scripts/bringup.py flash              build + flash apps/bringup
    python scripts/bringup.py flash blink        any app: blink, handoff, ...
    python scripts/bringup.py led red            red | green | blue | white | off
    python scripts/bringup.py motor on           on | off | pulse (half a second)
    python scripts/bringup.py pad high           high | low | off   (GP11 driven / released)
    python scripts/bringup.py carrier 40         40 | 200 | off     (kHz on GP11)
    python scripts/bringup.py freq               what the PIO counts on GP11
    python scripts/bringup.py adc                ADC0 mean / min / max, 4096 samples
    python scripts/bringup.py adc 100000         more samples (0.2 s): catches 50 Hz
    python scripts/bringup.py vsys               VSYS in mV, as the Pico sees it
    python scripts/bringup.py pins               BTN and ROLE now
    python scripts/bringup.py watch              stream the console; Ctrl+C to stop
    python scripts/bringup.py raw "l r"          any console line

Options: --port COM7 if more than one Pico is plugged in.

Needs pyserial (pip install pyserial). The first flash of a brand-new Pico
needs BOOTSEL held while plugging USB in; after that picotool reboots it.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("error: pyserial is not installed.  pip install pyserial", file=sys.stderr)
    sys.exit(1)

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = "build-pcb"
TX_PIN = 11
BAUD = 115200
PICO_VID = 0x2E8A


# ---------------------------------------------------------------------------
# the port
# ---------------------------------------------------------------------------

def find_port(wanted: str | None) -> str:
    if wanted:
        return wanted
    picos = [p for p in serial.tools.list_ports.comports() if p.vid == PICO_VID]
    if not picos:
        print("error: no Pico on USB. Is it plugged in, and running an image with "
              "a console (not sitting in BOOTSEL)?", file=sys.stderr)
        sys.exit(1)
    if len(picos) > 1:
        names = ", ".join(p.device for p in picos)
        print("error: more than one Pico on USB (" + names + "); pass --port",
              file=sys.stderr)
        sys.exit(1)
    return picos[0].device


def open_port(port: str) -> serial.Serial:
    try:
        return serial.Serial(port, BAUD, timeout=0.1)
    except serial.SerialException as exc:
        print("error: cannot open " + port + ": " + str(exc), file=sys.stderr)
        print("  (another program holding it? a serial monitor, or a logger "
              "from an earlier session)", file=sys.stderr)
        sys.exit(1)


def talk(port: str, lines: list[str], settle: float = 0.0, quiet: float = 0.6,
         limit: float = 6.0) -> None:
    """Send each line, print everything the board says until it goes quiet."""
    with open_port(port) as ser:
        time.sleep(0.2)                # DTR up; stdio_usb starts talking
        ser.reset_input_buffer()
        for i, line in enumerate(lines):
            if i:
                time.sleep(settle)
            ser.write((line + "\r\n").encode("ascii"))
            ser.flush()
        t_start = time.time()
        t_last = time.time()
        while time.time() - t_last < quiet and time.time() - t_start < limit:
            chunk = ser.readline()
            if not chunk:
                continue
            text = chunk.decode("ascii", errors="replace").rstrip()
            if text.startswith("    hb "):
                continue               # the heartbeat, not an answer
            print(text)
            t_last = time.time()


def watch(port: str, seconds: float | None) -> None:
    with open_port(port) as ser:
        t_end = None if seconds is None else time.time() + seconds
        print("watching " + port + " (Ctrl+C to stop)")
        try:
            while t_end is None or time.time() < t_end:
                chunk = ser.readline()
                if chunk:
                    print(chunk.decode("ascii", errors="replace").rstrip())
        except KeyboardInterrupt:
            pass


# ---------------------------------------------------------------------------
# build + flash
# ---------------------------------------------------------------------------

def flash(app: str) -> None:
    argv = [sys.executable, str(REPO_ROOT / "scripts" / "build.py"),
            "--build-dir", BUILD_DIR, "--tx-pin", str(TX_PIN),
            "--target", app, "--flash", "--flash-target", app]
    print("==> " + " ".join(argv[1:]))
    rc = subprocess.call(argv)
    if rc:
        sys.exit(rc)
    print("    flashed " + app + " (TX on GP" + str(TX_PIN) + "); give it "
          "two seconds to come back on USB")


# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Handoff PCB bring-up console.")
    ap.add_argument("--port", help="COM port (default: the one Pico on USB)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("flash", help="build for the PCB and flash")
    s.add_argument("app", nargs="?", default="bringup")

    s = sub.add_parser("led")
    s.add_argument("colour", choices=["red", "green", "blue", "white", "off"])

    s = sub.add_parser("motor")
    s.add_argument("state", choices=["on", "off", "pulse"])

    s = sub.add_parser("pad")
    s.add_argument("state", choices=["high", "low", "off"])

    s = sub.add_parser("carrier")
    s.add_argument("khz", choices=["40", "200", "off"])

    sub.add_parser("freq")

    s = sub.add_parser("adc")
    s.add_argument("samples", nargs="?", type=int, default=0)

    sub.add_parser("vsys")
    sub.add_parser("pins")

    s = sub.add_parser("watch")
    s.add_argument("seconds", nargs="?", type=float, default=None)

    s = sub.add_parser("raw")
    s.add_argument("line")

    args = ap.parse_args()

    if args.cmd == "flash":
        flash(args.app)
        return 0

    port = find_port(args.port)

    if args.cmd == "led":
        talk(port, ["l " + {"red": "r", "green": "g", "blue": "b",
                            "white": "w", "off": "0"}[args.colour]])
    elif args.cmd == "motor":
        if args.state == "pulse":
            talk(port, ["m 1", "m 0"], settle=0.5)
        else:
            talk(port, ["m " + ("1" if args.state == "on" else "0")])
    elif args.cmd == "pad":
        talk(port, ["p " + {"high": "1", "low": "0", "off": "x"}[args.state]])
    elif args.cmd == "carrier":
        talk(port, ["c " + ("0" if args.khz == "off" else args.khz)])
    elif args.cmd == "freq":
        talk(port, ["f"])
    elif args.cmd == "adc":
        talk(port, ["a " + str(args.samples)])
    elif args.cmd == "vsys":
        talk(port, ["v"])
    elif args.cmd == "pins":
        talk(port, ["i"])
    elif args.cmd == "watch":
        watch(port, args.seconds)
    elif args.cmd == "raw":
        talk(port, [args.line])
    return 0


if __name__ == "__main__":
    sys.exit(main())
