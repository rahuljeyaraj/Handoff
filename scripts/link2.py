#!/usr/bin/env python3
"""
Handoff - two-port console logger for a two-board bench.

One process opens both boards' consoles, stamps every line with the time since
the run started, and sends commands on a schedule. That last part is why this
exists: a two-board bench turns on facts like "which board decided to send
first", and two separate terminals cannot answer that - their clocks are the
operator's hands.

    python scripts/link2.py --a COM7 --b COM8 --run 60 \
        --at 0:A:h --at 0:B:h --at 2:A:z --at 2:B:z --at 55:A:s --at 55:B:s

Every line is printed as

    <seconds since start>  A| <what the board said>

and written to --log if one is given. One board is fine too: give only --a.

Ports may be named by COM port or by Pico serial. A serial is matched as a
suffix, so --a 93D1 finds 4904EF1FFA2393D1 - which is the label on the bench,
and it survives the COM ports moving. See docs/hardware-bringup.md.

Needs pyserial (pip install pyserial).
"""
from __future__ import annotations

import argparse
import sys
import threading
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("error: pyserial is not installed.  pip install pyserial", file=sys.stderr)
    sys.exit(2)

BAUD = 115200
PICO_VID = 0x2E8A


def resolve(name: str) -> str:
    """A COM port, or the tail of a Pico serial number."""
    if not name:
        return ""
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.device.upper() == name.upper():
            return p.device
    hits = [p for p in ports
            if p.vid == PICO_VID and (p.serial_number or "").upper().endswith(name.upper())]
    if len(hits) == 1:
        return hits[0].device
    if len(hits) > 1:
        raise SystemExit("error: %s matches %s" % (name, ", ".join(h.device for h in hits)))
    raise SystemExit("error: no port called %s, and no Pico whose serial ends in it" % name)


class Board:
    def __init__(self, label: str, port: str, sink):
        self.label = label
        self.port = port
        self.sink = sink
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.stop = False
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        buf = b""
        while not self.stop:
            try:
                chunk = self.ser.read(256)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.sink(self.label, line.decode("utf-8", "replace").rstrip("\r"))

    def send(self, text: str) -> None:
        self.sink(self.label, ">> " + text)
        self.ser.write((text + "\r").encode())
        self.ser.flush()

    def close(self) -> None:
        self.stop = True
        self.thread.join(timeout=1.0)
        try:
            self.ser.close()
        except Exception:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(description="two-port Handoff console logger")
    ap.add_argument("--a", required=True, help="first board: COM port or serial tail")
    ap.add_argument("--b", help="second board (optional)")
    ap.add_argument("--run", type=float, default=60.0, help="seconds to run")
    ap.add_argument("--at", action="append", default=[],
                    help="scheduled command, SECONDS:A|B:TEXT (repeatable)")
    ap.add_argument("--log", help="also write every line to this file")
    args = ap.parse_args()

    schedule = []
    for item in args.at:
        parts = item.split(":", 2)
        if len(parts) != 3:
            raise SystemExit("error: --at wants SECONDS:A|B:TEXT, got %r" % item)
        when, who, text = parts
        schedule.append((float(when), who.upper(), text))
    schedule.sort(key=lambda s: s[0])

    logf = open(args.log, "w", encoding="utf-8") if args.log else None
    start = time.time()
    lock = threading.Lock()

    def sink(label: str, text: str) -> None:
        line = "%8.3f  %s| %s" % (time.time() - start, label, text)
        with lock:
            print(line, flush=True)
            if logf:
                logf.write(line + "\n")
                logf.flush()

    boards = {"A": Board("A", resolve(args.a), sink)}
    if args.b:
        boards["B"] = Board("B", resolve(args.b), sink)
    for label, b in boards.items():
        sink(label, "-- %s open" % b.port)

    try:
        i = 0
        while True:
            now = time.time() - start
            if now >= args.run:
                break
            while i < len(schedule) and schedule[i][0] <= now:
                when, who, text = schedule[i]
                i += 1
                if who in boards:
                    boards[who].send(text)
                else:
                    sink("?", "no board %s for %r" % (who, text))
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        for b in boards.values():
            b.close()
        if logf:
            logf.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
