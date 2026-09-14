#!/usr/bin/env python3
"""
Feed a hardware capture back through the host decoder.

Development plan section 1, second supporting rule, and it is the one that
compounds:

    Every hardware failure becomes a host test. Any capture that broke the
    decoder on the bench is saved into firmware/test/vectors/ and replayed in
    CI forever. THE BUG IS NOT FIXED UNTIL IT IS A REGRESSION TEST.

So this is the bridge. It takes an int16 capture off the board, runs it through
handoff_decode -- the firmware's own pipeline, built by scripts/test.py
--decode -- and, with --adopt, files it into firmware/test/vectors/captures/
where test_vectors.c replays every entry of index.json on every run.

    python tools/replay.py capture.s16
    python tools/replay.py capture.s16 --from-log bench.log --seq 0
    python tools/replay.py capture.s16 --adopt m5-attenuator-fail --note "..."
    python tools/replay.py --list

Captures are committed. They are small, they are irreplaceable -- you cannot
re-record the exact moment a link failed -- and they are the only evidence in
this project that came from a body rather than from a model.
"""

from __future__ import annotations

import argparse
import array
import json
import shutil
import subprocess
import sys
from datetime import date
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CAPTURES = REPO_ROOT / "firmware" / "test" / "vectors" / "captures"
INDEX = CAPTURES / "index.json"
BUILD = REPO_ROOT / "build-host"


def load_index() -> list:
    if not INDEX.exists():
        return []
    return json.loads(INDEX.read_text(encoding="utf-8"))


def save_index(entries: list) -> None:
    CAPTURES.mkdir(parents=True, exist_ok=True)
    INDEX.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")


def cmd_list() -> int:
    entries = load_index()
    if not entries:
        print("no captures yet.")
        print("The first one arrives at M5, when the bench disagrees with the")
        print("simulator for the first time. That disagreement is the point of")
        print("M5 (see development plan), so expect it rather than dread it.")
        return 0

    print("%-28s %-10s %8s  %s" % ("name", "milestone", "samples", "note"))
    for e in entries:
        print("%-28s %-10s %8d  %s"
              % (e["name"], e.get("milestone", "?"), e.get("samples", 0),
                 e.get("note", "")))
    return 0


def decoder() -> Path:
    """firmware/test/host/decode.c: the firmware's own pipeline over a file."""
    exe = BUILD / ("handoff_decode" + (".exe" if sys.platform == "win32" else ""))
    if not exe.exists():
        print("build it first: python scripts/test.py --decode", file=sys.stderr)
        raise SystemExit(2)
    return exe


def describe(path: Path) -> dict:
    data = array.array("h")
    data.frombytes(path.read_bytes())
    if sys.byteorder == "big":
        data.byteswap()

    if not data:
        return {"samples": 0}

    n = len(data)
    mean = sum(data) / n
    peak = max(abs(min(data)), abs(max(data)))
    clipped = sum(1 for s in data if s >= 2047 or s <= -2048)

    return {
        "samples": n,
        "seconds": n / 500_000.0,
        "mean_lsb": round(mean, 2),
        "peak_lsb": peak,
        "clipped": clipped,
    }


def extract_dump(log: Path, out: Path) -> int:
    """Pull tlm_usb_raw_dump()'s text burst out of a console log, into .s16."""
    import re
    data = array.array("h")
    armed = False
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"(?:\[\s*[\d.]+\]\s*)?(.*)$", line)
        t = m.group(1).strip() if m else line.strip()
        if t.startswith("r end"):
            break
        if re.match(r"r \d+$", t):
            armed = True
            continue
        if armed and re.match(r"-?\d+$", t):
            data.append(int(t))
    if sys.byteorder == "big":
        data.byteswap()
    out.write_bytes(data.tobytes())
    return len(data)


def cmd_replay(args) -> int:
    if args.from_log:
        n = extract_dump(args.from_log, args.capture)
        print("extracted %d samples from %s" % (n, args.from_log.name))
    info = describe(args.capture)
    if not info["samples"]:
        print("%s is empty" % args.capture, file=sys.stderr)
        return 1

    print("%s" % args.capture.name)
    print("  %(samples)d samples, %(seconds).4f s at 500 ksps" % info)
    print("  mean %(mean_lsb)s LSB, peak %(peak_lsb)d LSB" % info)
    if info["clipped"]:
        print("  WARNING: %d samples clipped — see design 15.2" % info["clipped"])

    exe = decoder()
    cmd = [str(exe), str(args.capture), "--carrier", str(args.carrier)]
    if args.seq is not None:
        cmd += ["--seq", str(args.seq)]
    print("\nreplaying through %s" % exe.name)
    res = subprocess.run(cmd, cwd=str(REPO_ROOT))

    if args.adopt:
        CAPTURES.mkdir(parents=True, exist_ok=True)
        dest = CAPTURES / (args.adopt + ".s16")
        shutil.copyfile(args.capture, dest)

        entries = [e for e in load_index() if e["name"] != args.adopt]
        entry = {"name": args.adopt, "milestone": args.milestone,
                 "date": date.today().isoformat(), "note": args.note or "",
                 "file": dest.name, "carrier": args.carrier}
        if args.seq is not None:
            entry["seq"] = args.seq
        entry.update(info)
        entries.append(entry)
        entries.sort(key=lambda e: e["name"])
        save_index(entries)

        print("\nadopted as %s" % dest.relative_to(REPO_ROOT))
        print("Commit it. It is now replayed on every push, and the bug it came")
        print("from is not fixed until this file decodes.")

    return res.returncode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", nargs="?", type=Path)
    ap.add_argument("--list", action="store_true", help="list adopted captures")
    ap.add_argument("--adopt", metavar="NAME",
                    help="file this capture into the permanent regression set")
    ap.add_argument("--milestone", default="?", help="which milestone it came from")
    ap.add_argument("--note", help="what broke, in one line")
    ap.add_argument("--expect", help="payload the capture should decode to, as hex")
    ap.add_argument("--carrier", type=int, default=200000, help="carrier the capture was made at")
    ap.add_argument("--seq", type=int, help="loopback frame sequence number, for a BER")
    ap.add_argument("--from-log", metavar="LOG", type=Path,
                    help="extract the `r <n>` ... `r end` dump from a console log into CAPTURE first")
    args = ap.parse_args()

    if args.list:
        return cmd_list()
    if not args.capture:
        ap.error("give a capture file, or --list")
    if not args.capture.exists() and not args.from_log:
        print("no such file: %s" % args.capture, file=sys.stderr)
        return 2

    return cmd_replay(args)


if __name__ == "__main__":
    raise SystemExit(main())
