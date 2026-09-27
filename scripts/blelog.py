#!/usr/bin/env python3
"""
Handoff - body-link readings off two bands, by radio, through two phones.

THE POINT. scripts/link2.py reads a band over USB, and design §13 forbids a USB
tether to a mains-powered PC while anyone is touching an electrode. That is not
only a safety rule. On 24 Sep 2026 a tethered run measured a carrier and decoded
ZERO frames, because both bands then share the PC's ground and that wire IS the
return path under test - the reading was wrong, not just unsafe.

So the bands stay floating on their cells and say nothing over USB. Each one
pushes ble_bench_t AND ble_trig_t to its own phone over BLE twice a second; the
app logs one line per block; this reads both phones' logcat over adb. The phones
are tethered to the PC, the bands are not, and the radio hop is the isolation.

ALL THREE BLOCKS ARE LOGGED, and the LAST one is the one that answers the
question a two-band run is asking. The bank line carries the chart's own pair:

    listen  windows the band spent LISTENING - its own transmissions cut out -
            and busy, how many of those its detector called busy. Difference two
            bank lines and that is the fraction of the time this band could hear
            the other one: 0 on an uncoupled channel, climbing with coupling,
            and the same verdict the link itself runs on. Nothing has to be
            drawn against a threshold, which is why the chart was rebuilt on it
            on 26 Sep 2026 - see ble_bank_t in firmware/lib/hal_pico/ble.h.
    A B     the two tone bins at the loudest window of the interval, and the
            three guards averaged over it. Not a verdict; the picture that says
            WHERE energy was when busy is not still.

The bench line is what the signal is doing:

    signal  the louder tone bin, and noise the guard reference it was judged
            against; thr is k*noise, the ONE test the detector applies. There
            is no floor, no gate and no min_delta in link v2.

The trig line is whether the two bands hear EACH OTHER:

    beacons how many this band sent; peers how many it decoded from the other.
            Read them as a pair, and compare A's pair with B's - the step-7
            fault was an ASYMMETRY that no level would ever have shown.
    peak    the loudest window since the previous block, over its own
            threshold. This is the level reading that means something: a beacon
            is on air 11 ms and the blocks arrive twice a second, so the bench
            line's instantaneous signal is the empty room almost every time.
    echoes  beacons that came back carrying this band's own nonce.

    python scripts/blelog.py --run 120 --log bench.log

With two phones attached it finds them itself. Name them if the order matters,
by adb serial or by any unique tail of one:

    python scripts/blelog.py --a R5CT21 --b 4b8f9c2e --run 120

Every line is stamped with the seconds since the run started

    <seconds>  A| signal 48 noise 9 thr 150 present 1 good 447 ...
    <seconds>  A| nonce 4f21 beacons 107 peers 20 echoes 0 ... peak 310/150 ...

and a summary of the last block from each phone is printed at the end.

THE BENCH ORDER that makes the numbers mean something:

    1. Both bands OFF USB, on their cells.
    2. Both phones connected to their band, with Settings > Advanced open.
       Nothing is pushed to a phone that has not subscribed.
    3. Start this script.
    4. Gate the transmitter on at SW1, wear both bands, gate off at SW1.

Needs adb on PATH and both phones in USB debugging.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import threading
import time

# Set by BandClient.kt's Log.i(TAG, "bench ...") and Log.i(TAG, "trig ...").
# Filtering in adb rather than here keeps the phone from spending USB bandwidth
# on the whole system log.
TAG = "BandClient"
LOGCAT = ["logcat", "-T", "1", "-v", "brief", "%s:I" % TAG, "*:S"]

# Three blocks, one pattern. Anchored on the first field of each so a line that
# merely mentions the word cannot match, which is what the old `bench (level
# ...)` was doing for free and is worth keeping.
#
# THE BANK LINE IS HERE BECAUSE IT NOW CARRIES THE ANSWER. Its two counters --
# listening windows and how many of them the detector called busy -- are what
# the phone's chart draws, and differencing two of these lines is the whole
# measurement of a worn run: an uncoupled channel holds busy still while
# listening climbs. The five bins beside them say WHERE the energy was when it
# is not still. See ble_bank_t in firmware/lib/hal_pico/ble.h.
BLOCK = re.compile(r"\b(?:bench (signal .*)|trig (nonce .*)|bank (A .*))$")


def adb(*args: str) -> str:
    return subprocess.run(["adb", *args], capture_output=True, text=True).stdout


def devices() -> list:
    """Attached and authorised phones, in the order adb lists them."""
    out, found = adb("devices"), []
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            found.append(parts[0])
        elif len(parts) >= 2 and parts[1] == "unauthorized":
            print("warning: %s has not accepted this PC's debugging key"
                  % parts[0], file=sys.stderr)
    return found


def resolve(name: str, attached: list) -> str:
    """An adb serial, or any unique tail of one - so a short prefix of the
    label on the phone is enough, the way link2.py takes a Pico serial tail."""
    if not name:
        return ""
    for d in attached:
        if d.upper() == name.upper():
            return d
    hits = [d for d in attached if name.upper() in d.upper()]
    if len(hits) == 1:
        return hits[0]
    if len(hits) > 1:
        raise SystemExit("error: %s matches %s" % (name, ", ".join(hits)))
    raise SystemExit("error: no attached phone called %s (adb sees %s)"
                     % (name, ", ".join(attached) or "none"))


class Phone:
    def __init__(self, label: str, serial: str, sink, raw: bool):
        self.label = label
        self.serial = serial
        self.sink = sink
        self.raw = raw
        self.last = None
        self.last_trig = None
        self.last_bank = None
        self.blocks = 0
        self.proc = subprocess.Popen(
            ["adb", "-s", serial, *LOGCAT],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
            bufsize=1,
        )
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        for line in self.proc.stdout:
            line = line.rstrip()
            m = BLOCK.search(line)
            if m:
                body = m.group(1) or m.group(2) or m.group(3)
                # Only the bench block counts as a block, so the rate printed
                # at the end stays the rate it always was.
                if m.group(1):
                    self.blocks += 1
                    self.last = body
                elif m.group(2):
                    self.last_trig = body
                else:
                    self.last_bank = body
                self.sink(self.label, "bank " + body if m.group(3) else body)
            elif self.raw and line:
                self.sink(self.label, line)

    def close(self) -> None:
        self.proc.terminate()
        try:
            self.proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="body-link readings off two bands via their phones")
    ap.add_argument("--a", help="first phone: adb serial or a unique tail")
    ap.add_argument("--b", help="second phone")
    ap.add_argument("--run", type=float, default=120.0, help="seconds to run")
    ap.add_argument("--log", help="also write every line to this file")
    ap.add_argument("--raw", action="store_true",
                    help="every BandClient line, not only the bench blocks")
    args = ap.parse_args()

    if not shutil.which("adb"):
        raise SystemExit("error: adb is not on PATH")

    attached = devices()
    if not attached:
        raise SystemExit("error: adb sees no phone. Check the cable and that "
                         "USB debugging is on and this PC is trusted.")

    if args.a or args.b:
        chosen = [("A", resolve(args.a, attached))] if args.a else []
        if args.b:
            chosen.append(("B", resolve(args.b, attached)))
    else:
        chosen = list(zip("AB", attached[:2]))
        if len(attached) > 2:
            print("warning: %d phones attached, using %s - name them with "
                  "--a/--b" % (len(attached), " and ".join(attached[:2])),
                  file=sys.stderr)

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

    phones = {}
    for label, serial in chosen:
        phones[label] = Phone(label, serial, sink, args.raw)
        sink(label, "-- %s open" % serial)

    try:
        while time.time() - start < args.run:
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        for p in phones.values():
            p.close()

    print()
    for label, p in phones.items():
        if p.blocks == 0:
            # Every one of these is a setup mistake rather than a bad channel,
            # so say which so nobody reads silence as a dead link.
            print("%s| NOTHING. The app is not connected to the band, or "
                  "Advanced was never opened, or the app is not the build with "
                  "the bench block in it." % label)
        else:
            print("%s| %d blocks, last: %s" % (label, p.blocks, p.last))
            if p.last_trig:
                print("%s|              %s" % (label, p.last_trig))
            if p.last_bank:
                print("%s|              bank %s" % (label, p.last_bank))
            if " usb 1" in p.last:
                print("%s| THE BAND WAS ON USB. This run is not a valid "
                      "body-coupled measurement." % label)
    if logf:
        logf.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
