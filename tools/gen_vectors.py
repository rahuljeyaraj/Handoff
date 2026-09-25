#!/usr/bin/env python3
"""
Generate golden modulation vectors from the SPEC, not from the C encoder.

This is the point of the whole file, and it is worth being blunt about: if the
C encoder and the C decoder share a misreading of design section 9.4, a
loopback test passes and the link still fails on the bench. An independently
written generator is the only thing that catches that class of bug, so nothing
here imports, links to, or consults the C. It is written from the documents:

    link-v2 section 4   two tones, one per chip value, constant envelope
    design section 9.2  Manchester, mark = on-then-off
    design section 9.4  preamble 32 alternating chips, marker 11110000
    architecture 8.3    2-byte header, fixed payload, CRC-16
    design section 10.2 500 ksps, 12-bit, ADC0
    design section 10.3 Goertzel on a bin centre

Outputs into firmware/test/vectors/generated/:

    manifest.json   what each vector is and what it should decode to
    <name>.chips    one chip per line, 0 or 1
    <name>.s16      int16 little-endian ADC samples, DC-centred

    python tools/gen_vectors.py
    python tools/gen_vectors.py --out some/dir --gz-n 50
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "firmware" / "test" / "vectors" / "generated"

# ---------------------------------------------------------------------------
# the spec, transcribed
# ---------------------------------------------------------------------------

ADC_FS_HZ = 500_000          # design 10.2

# link-v2 section 4. Two tones, on ADJACENT bins, and the frequencies are
# DERIVED rather than typed: the bin spacing is the window rate, so a tone is
# its bin index times ADC_FS_HZ / gz_n. Chip 0 is tone A, chip 1 is tone B.
#
# The bins follow whatever the build under test is configured for, because a
# vector generated on the wrong bins lands in the wrong place in the transform
# and decodes as silence.
TONE_A_BIN = 9
TONE_B_BIN = 10
WINDOWS_PER_CHIP = 5         # design 9.3
PREAMBLE_CHIPS = 32          # design 9.4
START_MARKER = 0xF0          # design 9.4, "11110000"
HDR_BYTES = 2                # architecture 8.3
CRC_BYTES = 2                # architecture 8.3
FRAG_PAYLOAD = 32            # architecture 8.3

# Silent chips rendered after the burst. The receiver's timing tracker stretches
# and shrinks chips to follow the transmitter, so the last chip of a frame can
# still be inside the integrator when the burst ends -- and it carries part of
# the CRC. Real hardware gets this for free: the ADC free-runs and the
# turnaround silence flushes the pipeline.
TAIL_CHIPS = 4


def manchester(byte: int) -> list[int]:
    """design 9.2: a mark is on-then-off, a space off-then-on. MSB first."""
    chips = []
    for bit in range(7, -1, -1):
        b = (byte >> bit) & 1
        chips += [1, 0] if b else [0, 1]
    return chips


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE. poly 0x1021, init 0xFFFF, no reflection, no xorout."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack_header(frag_index: int, frag_count: int, record_id: int, flags: int) -> bytes:
    """architecture 8.3: index 4b | count 4b | record id 6b | flags 2b."""
    word = ((frag_index & 0x0F) << 12
            | ((frag_count - 1) & 0x0F) << 8
            | (record_id & 0x3F) << 2
            | (flags & 0x03))
    return struct.pack(">H", word)


def build_frame(payload: bytes, frag_index=0, frag_count=1, record_id=0, flags=0) -> list[int]:
    """Preamble, marker, header, payload, CRC — all of it, as chips."""
    if len(payload) > FRAG_PAYLOAD:
        raise ValueError("payload longer than one fragment")

    chips = [1 if i % 2 == 0 else 0 for i in range(PREAMBLE_CHIPS)]
    chips += manchester(START_MARKER)

    body = pack_header(frag_index, frag_count, record_id, flags)
    body += payload + bytes(FRAG_PAYLOAD - len(payload))   # tag 0 is NOP padding
    body += struct.pack(">H", crc16_ccitt(body))

    for byte in body:
        chips += manchester(byte)
    return chips


# ---------------------------------------------------------------------------
# modulation
# ---------------------------------------------------------------------------

def modulate(chips, gz_n, *, amplitude=200.0, noise_rms=0.0, dc=2048.0,
             carrier_ppm=0.0, clock_ppm=0.0, ramp_db=0.0, seed=1,
             tone_bins=(TONE_A_BIN, TONE_B_BIN)):
    """
    link-v2 section 4: a tone for every chip, tone A for 0 and tone B for 1.
    The pad is driven throughout the burst -- there is no space to render.

    Deliberately a different implementation from chan.c: a straight sine
    against absolute time rather than an integrated phase, its own LCG rather
    than xorshift. Agreement between the two is then evidence.

    THE TWO AGREE EXACTLY EVEN THOUGH THE TONE CHANGES MID-BURST, and that is
    arithmetic rather than luck. A chip is a whole number of periods of either
    tone (config.h asserts it), so at every chip boundary an integrated phase
    is back at zero -- which is where sin(2*pi*f*t) is too. Inside a chip both
    then reduce to the same sine of the same offset.
    """
    spc = gz_n * WINDOWS_PER_CHIP
    bin_hz = ADC_FS_HZ / gz_n
    ftone = [tone_bins[0] * bin_hz * (1.0 + carrier_ppm * 1e-6),
             tone_bins[1] * bin_hz * (1.0 + carrier_ppm * 1e-6)]
    chip_samples = spc * (1.0 + clock_ppm * 1e-6)
    # Sized off the OFFSET chip period, not the nominal one: a slow transmitter
    # needs more receiver samples for the same chips, and a buffer sized at the
    # nominal rate would clip the last chip -- which carries part of the CRC.
    n = int(round((len(chips) + TAIL_CHIPS) * chip_samples))

    state = seed & 0xFFFFFFFF
    out = bytearray()

    for i in range(n):
        t = i / ADC_FS_HZ
        ci = int(i / chip_samples)

        amp = amplitude
        if ramp_db and len(chips) > 1:
            amp *= 10.0 ** ((ramp_db * ci / (len(chips) - 1)) / 20.0)

        v = dc
        if ci < len(chips):
            v += amp * math.sin(2.0 * math.pi * ftone[1 if chips[ci] else 0] * t)

        if noise_rms:
            # Irwin-Hall: twelve uniforms sum to something near gaussian with
            # unit variance. Crude, independent of numpy, and good enough for a
            # vector whose job is to be reproducible.
            acc = 0.0
            for _ in range(12):
                state = (state * 1103515245 + 12345) & 0xFFFFFFFF
                acc += state / 4294967296.0
            v += noise_rms * (acc - 6.0)

        v = max(0.0, min(4095.0, v))
        out += struct.pack("<h", int(round(v)) - 2048)

    return bytes(out)


# ---------------------------------------------------------------------------
# the vectors
# ---------------------------------------------------------------------------

def vectors(gz_n):
    """Each entry: name, description, chips, payload, modulation kwargs."""
    short = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    full = bytes((i * 7 + 3) & 0xFF for i in range(FRAG_PAYLOAD))

    yield ("clean_short",
           "one frame, 4-byte payload, no impairments",
           build_frame(short), short, {})

    yield ("clean_full",
           "one frame, full 32-byte payload, no impairments",
           build_frame(full), full, {})

    yield ("frag_3_of_5",
           "header field coverage: fragment 3 of 5, record id 42, HAVE_YOURS",
           build_frame(short, frag_index=3, frag_count=5, record_id=42, flags=1),
           short, {})

    yield ("noisy_10db",
           "10 dB SNR, the region the link budget says must still work",
           build_frame(full), full,
           {"noise_rms": 200.0 / math.sqrt(2) / (10 ** 0.5), "seed": 7})

    yield ("ramp_20db",
           "20 dB amplitude ramp across the packet: grip tightens mid-handshake",
           build_frame(full), full, {"ramp_db": 20.0})

    yield ("offset_200ppm",
           "+200 ppm carrier and chip clock, 4x worse than two real crystals",
           build_frame(full), full, {"carrier_ppm": 200.0, "clock_ppm": 200.0})

    yield ("offset_minus_200ppm",
           "-200 ppm carrier and chip clock",
           build_frame(full), full, {"carrier_ppm": -200.0, "clock_ppm": -200.0})

    yield ("weak_signal",
           "20 LSB carrier — a tenth of the link budget, with the noise to match",
           build_frame(full), full,
           {"amplitude": 20.0, "noise_rms": 4.0, "seed": 11})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--gz-n", type=int, default=25,
                    help="Goertzel window length; sets samples per chip (default 25)")
    ap.add_argument("--tone-a-bin", type=int, default=TONE_A_BIN,
                    help="Goertzel bin of tone A, chip value 0 (link-v2 4)")
    ap.add_argument("--tone-b-bin", type=int, default=TONE_B_BIN,
                    help="Goertzel bin of tone B, chip value 1 (link-v2 4)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    spc = args.gz_n * WINDOWS_PER_CHIP

    manifest = {
        "generated_by": "tools/gen_vectors.py",
        "adc_fs_hz": ADC_FS_HZ,
        "tone_a_hz": args.tone_a_bin * ADC_FS_HZ // args.gz_n,
        "tone_b_hz": args.tone_b_bin * ADC_FS_HZ // args.gz_n,
        "gz_n": args.gz_n,
        "windows_per_chip": WINDOWS_PER_CHIP,
        "samples_per_chip": spc,
        "preamble_chips": PREAMBLE_CHIPS,
        "start_marker": START_MARKER,
        "frag_payload": FRAG_PAYLOAD,
        "crc16_check_123456789": crc16_ccitt(b"123456789"),
        "vectors": [],
    }

    for name, desc, chips, payload, kw in vectors(args.gz_n):
        chip_path = args.out / (name + ".chips")
        samp_path = args.out / (name + ".s16")

        chip_path.write_text("".join("%d\n" % c for c in chips), encoding="ascii")
        samp_path.write_bytes(modulate(chips, args.gz_n,
                                       tone_bins=(args.tone_a_bin, args.tone_b_bin),
                                       **kw))

        manifest["vectors"].append({
            "name": name,
            "description": desc,
            "chips": len(chips),
            "samples": int(round((len(chips) + TAIL_CHIPS) * spc
                                 * (1.0 + kw.get("clock_ppm", 0.0) * 1e-6))),
            "payload_hex": payload.hex(),
            "chips_file": chip_path.name,
            "samples_file": samp_path.name,
            **{k: v for k, v in kw.items() if k != "seed"},
        })
        if not args.quiet:
            print("  %-22s %5d chips  %7d samples"
                  % (name, len(chips), manifest["vectors"][-1]["samples"]))

    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="ascii")

    if not args.quiet:
        print("wrote %d vectors to %s" % (len(manifest["vectors"]), args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
