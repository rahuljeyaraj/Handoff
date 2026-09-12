#!/usr/bin/env python3
"""
Write hardware/bom.csv from kicad-cli's BOM export (build/bom.csv).

kicad-cli gives Reference, Value, Footprint, Description and DNP, grouped by
Value+Footprint. What it cannot give is a Package column a human can order or
sort against - that is derived here from the footprint name (or, for the three
off-board parts with no footprint, from what the README says of them), so the
derivation lives in one place and is regenerated with everything else instead
of being typed in by hand once and left to drift.

    Reference,Qty,Value,Package,Footprint,Description,DNP

gen_schematic.py calls write_bom() after its own BOM check; it can also be run
on its own once build/bom.csv exists.
"""
import csv
import re
from pathlib import Path

HW = Path(__file__).resolve().parent.parent
SRC = HW / "build" / "bom.csv"
OUT = HW / "bom.csv"

# Parts with no footprint: they are not on the board, they plug into it.
OFF_BOARD = {
    "BT1": "Off-board, on a JST-XH pigtail",
    "D2": "Off-board: solders into J3, or plugs in via a 4-pin XH pigtail",
    "M1": "Off-board: leads solder into J6",
}

# footprint name -> package, first match wins
RULES = [
    (r"_(0402|0603|0805|1206)_", lambda m: m.group(1)),
    (r"D_SMB$", lambda m: "SMB (DO-214AA)"),
    (r"D_SMA$", lambda m: "SMA (DO-214AC)"),
    (r"D_SOD-123FL$", lambda m: "SOD-123FL"),
    (r"D_SOD-123$", lambda m: "SOD-123"),
    (r"JST_XH_B(\d+)B-XH-A_1x\d+_P2\.50mm", lambda m: f"THT, JST-XH 2.50 mm, {int(m.group(1))}-pin"),
    (r"PinHeader_1x(\d+)_P2\.54mm", lambda m: f"THT, 2.54 mm pin header, {int(m.group(1))}-pin"),
    (r"SOT-23$", lambda m: "SOT-23"),
    (r"MSOP-8_3x3mm_P0\.65mm", lambda m: "MSOP-8 (3x3 mm, 0.65 mm pitch)"),
    (r"SW_Slide_SS-12F23G5", lambda m: "THT, SS-12F23G5 slide switch (right-angle SPDT)"),
    (r"SW_PUSH_6mm", lambda m: "THT, 6x6 mm tactile button"),
    (r"RaspberryPi_Pico_Common_THT", lambda m: "THT, socketed (2x 1x20 female headers), not soldered"),
]


def package(refs, footprint):
    if not footprint:
        first = refs.split(",")[0].strip()
        if first in OFF_BOARD:
            return OFF_BOARD[first]
        raise SystemExit(f"BOM: {refs} has no footprint and no OFF_BOARD entry")
    name = footprint.split(":", 1)[-1]
    for pat, fn in RULES:
        m = re.search(pat, name)
        if m:
            return fn(m)
    raise SystemExit(f"BOM: no package rule for footprint {footprint!r} ({refs})")


def write_bom(src=SRC, out=OUT):
    with src.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    with out.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, quoting=csv.QUOTE_ALL, lineterminator="\n")
        w.writerow(["Reference", "Qty", "Value", "Package", "Footprint", "Description", "DNP"])
        for r in rows:
            refs = r["Reference"]
            w.writerow([refs, len(refs.split(",")), r["Value"], package(refs, r["Footprint"]),
                        r["Footprint"], r["Description"], r["DNP"]])
    pk = sorted({package(r["Reference"], r["Footprint"]) for r in rows
                 if re.match(r"[RC]\d", r["Reference"])})
    print(f"BOM: {len(rows)} lines -> {out.relative_to(HW.parent)}; passive packages: {', '.join(pk)}")
    return len(rows)


if __name__ == "__main__":
    write_bom()
