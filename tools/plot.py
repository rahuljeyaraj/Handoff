#!/usr/bin/env python3
"""
Plot Handoff score streams, raw ADC bursts and BER curves.

The receiver is the test instrument -- there is no oscilloscope in this project
(design R5) -- so this is where every measurement ends up looking like
something. In particular the plot that design section 14.1 is really about:
signal against condition, with the body removed and the geometry unchanged.

    python tools/plot.py score capture.s16              score stream over time
    python tools/plot.py raw burst.s16                  triggered ADC burst
    python tools/plot.py ber sweep.csv                  a BER curve
    python tools/plot.py ber a.csv b.csv --label N=25 --label N=50
    python tools/plot.py campaign 14_1.csv              the section 14.1 bars

Score streams and raw bursts are int16 little-endian, which is what
hal_pico/tlm_usb.c emits and what firmware/test/vectors/captures holds.

matplotlib is optional: without it, every mode falls back to numbers and an
ASCII plot, because a number written down beats a picture you cannot make on
the machine you happen to be sitting at.
"""

from __future__ import annotations

import argparse
import array
import csv
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except Exception:
    HAVE_MPL = False

ADC_FS_HZ = 500_000
DEFAULT_GZ_N = 25


def read_s16(path: Path) -> array.array:
    data = array.array("h")
    data.frombytes(path.read_bytes())
    if sys.byteorder == "big":
        data.byteswap()
    return data


# --------------------------------------------------------------------------
# ASCII fallback
# --------------------------------------------------------------------------

def ascii_plot(values, width=72, height=18, label=""):
    if not values:
        print("(no samples)")
        return

    lo, hi = min(values), max(values)
    if hi == lo:
        hi = lo + 1

    # Decimate to the terminal width by taking the max of each bucket: for a
    # score stream the peak is the signal, and averaging hides it.
    step = max(1, len(values) // width)
    cols = [max(values[i:i + step]) for i in range(0, len(values), step)][:width]

    print("%s  %d points, min %g, max %g" % (label, len(values), lo, hi))
    for row in range(height, 0, -1):
        threshold = lo + (hi - lo) * row / height
        line = "".join("#" if c >= threshold else " " for c in cols)
        print("%8.4g |%s" % (threshold, line))
    print("%8s +%s" % ("", "-" * len(cols)))


# --------------------------------------------------------------------------

def cmd_score(args) -> int:
    scores = read_s16(args.files[0])
    window_rate = ADC_FS_HZ / args.gz_n
    seconds = len(scores) / window_rate

    print("%s: %d scores, %.3f s at %d windows/s"
          % (args.files[0].name, len(scores), seconds, int(window_rate)))
    if scores:
        mean = sum(scores) / len(scores)
        print("  min %d  mean %.1f  max %d" % (min(scores), mean, max(scores)))

    if not HAVE_MPL:
        ascii_plot(list(scores), label="score")
        return 0

    t = [i / window_rate for i in range(len(scores))]
    plt.figure(figsize=(11, 4))
    plt.plot(t, scores, linewidth=0.7)
    plt.xlabel("time (s)")
    plt.ylabel("Goertzel score (LSB)")
    plt.title("Handoff score stream — %s" % args.files[0].name)
    plt.grid(alpha=0.3)
    plt.tight_layout()
    show(args)
    return 0


def cmd_raw(args) -> int:
    samples = read_s16(args.files[0])
    seconds = len(samples) / ADC_FS_HZ
    print("%s: %d samples, %.4f s at %d ksps"
          % (args.files[0].name, len(samples), seconds, ADC_FS_HZ // 1000))

    if samples:
        rms = math.sqrt(sum(float(s) * s for s in samples) / len(samples))
        print("  min %d  max %d  RMS %.1f LSB" % (min(samples), max(samples), rms))
        clipped = sum(1 for s in samples if s >= 2047 or s <= -2048)
        if clipped:
            print("  WARNING: %d samples clipped (%.2f%%) — design 15.2's LC "
                  "bandpass may be needed" % (clipped, 100.0 * clipped / len(samples)))

    if not HAVE_MPL:
        ascii_plot(list(samples), label="raw ADC")
        return 0

    t = [i / ADC_FS_HZ * 1e3 for i in range(len(samples))]
    plt.figure(figsize=(11, 4))
    plt.plot(t, samples, linewidth=0.4)
    plt.xlabel("time (ms)")
    plt.ylabel("ADC (LSB, DC-centred)")
    plt.title("Handoff raw burst — %s" % args.files[0].name)
    plt.grid(alpha=0.3)
    plt.tight_layout()
    show(args)
    return 0


def cmd_ber(args) -> int:
    """Consumes handoff_ber --csv. This is the curve M5 must reproduce."""
    series = []
    for path in args.files:
        with path.open(newline="", encoding="ascii") as fh:
            rows = list(csv.DictReader(fh))
        snr = [float(r["snr_db"]) for r in rows]
        fer = [float(r["fer"]) for r in rows]
        ber = [float(r["ber"]) for r in rows]
        series.append((path.name, snr, fer, ber))

    labels = args.label or [s[0] for s in series]

    for (name, snr, fer, ber), label in zip(series, labels):
        print("%s" % label)
        print("   SNR      FER         BER")
        for s, f, b in zip(snr, fer, ber):
            print("  %5.1f  %8.4f  %10.2e" % (s, f, b))
        knee = next((s for s, f in zip(snr, fer) if f == 0.0), None)
        print("  waterfall: first error-free point at %s dB\n"
              % ("%.1f" % knee if knee is not None else "never"))

    if not HAVE_MPL:
        return 0

    plt.figure(figsize=(8, 5))
    for (name, snr, fer, ber), label in zip(series, labels):
        plt.semilogy(snr, [max(b, 1e-7) for b in ber], marker="o", label="%s BER" % label)
        plt.semilogy(snr, [max(f, 1e-7) for f in fer], marker="x", linestyle="--",
                     label="%s FER" % label)
    plt.xlabel("input SNR at the ADC (dB)")
    plt.ylabel("error rate")
    plt.title("Handoff BER — simulator")
    plt.grid(alpha=0.3, which="both")
    plt.legend()
    plt.tight_layout()
    show(args)
    return 0


def cmd_campaign(args) -> int:
    """
    design section 14.1, the centrepiece plot.

    Expects condition,score rows. The bar that matters is not the tallest one:
    it is the TX-electrode-off-the-body case with the geometry unchanged. If
    that one does not collapse, the whole result is RF leaking through the air
    and the project has not demonstrated body coupling at all.
    """
    with args.files[0].open(newline="", encoding="utf-8") as fh:
        rows = [(r[0], float(r[1])) for r in csv.reader(fh) if r and not r[0].startswith("#")]

    if not rows:
        print("no rows", file=sys.stderr)
        return 1

    peak = max(v for _, v in rows)
    width = 48
    print("%-32s %8s" % ("condition", "score"))
    for name, value in rows:
        bar = "#" * int(width * value / peak) if peak else ""
        print("%-32s %8.1f  %s" % (name, value, bar))

    control = [v for n, v in rows if "off" in n.lower() or "no body" in n.lower()]
    if control:
        ratio = peak / max(max(control), 1e-9)
        print("\nbody path is %.1fx the no-body control (%.1f dB)"
              % (ratio, 20 * math.log10(ratio)))
        if ratio < 4:
            print("WARNING: that is not a convincing separation. See design 14.1.")

    if not HAVE_MPL:
        return 0

    plt.figure(figsize=(10, 5))
    plt.bar([n for n, _ in rows], [v for _, v in rows])
    plt.ylabel("mean score (LSB)")
    plt.title("Handoff — proof of body coupling (design 14.1)")
    plt.xticks(rotation=30, ha="right")
    plt.grid(alpha=0.3, axis="y")
    plt.tight_layout()
    show(args)
    return 0


def show(args) -> None:
    if args.out:
        plt.savefig(args.out, dpi=140)
        print("wrote %s" % args.out)
    else:
        plt.show()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["score", "raw", "ber", "campaign"])
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--gz-n", type=int, default=DEFAULT_GZ_N)
    ap.add_argument("--label", action="append")
    ap.add_argument("--out", type=Path, help="write a PNG instead of showing a window")
    args = ap.parse_args()

    for f in args.files:
        if not f.exists():
            print("no such file: %s" % f, file=sys.stderr)
            return 2

    if not HAVE_MPL:
        print("(matplotlib not installed — numbers and ASCII only)\n", file=sys.stderr)

    return {"score": cmd_score, "raw": cmd_raw,
            "ber": cmd_ber, "campaign": cmd_campaign}[args.mode](args)


if __name__ == "__main__":
    raise SystemExit(main())
