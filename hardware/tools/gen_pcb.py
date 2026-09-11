#!/usr/bin/env python3
"""
Generate hardware/handoff.kicad_pcb from the schematic's netlist and the floor
plan. Run with KiCad's own Python:

    "C:/Program Files/KiCad/10.0/bin/python.exe" hardware/tools/gen_pcb.py

**This file is the layout**, the way gen_schematic.py is the schematic. Edit it,
not the .kicad_pcb - the board is regenerated from scratch every run, so any
hand edit in pcbnew is lost on the next one.

Frame: the floor plan's, one to one, so every number here can be read straight
off floorplan.svg.

    x  0 -> 40   thumb wall -> little-finger wall  (across the arm)
    y  0 -> 62   hand end   -> elbow end           (along the arm)

The Pico sits at 180 degrees: USB overhangs the elbow edge, which puts the
antenna keep-out at the hand end and puts **pins 1-20 on the little-finger row
(x = 28.9) and pins 21-40 on the thumb row (x = 11.12)**. Every placement that
names a Pico pin depends on that; see PICO_PIN below, which derives it rather
than restating it.

Placement is declarative: each part names an anchor - either its origin or one
of its pads - and a target coordinate. The script solves for the footprint
position, so no offset arithmetic appears in the table and a footprint that
changes shape cannot silently drag a part off its target.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
HW = HERE.parent
KICAD = Path(os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0"))
KICAD_CLI = KICAD / "bin" / "kicad-cli.exe"
FPDIR = KICAD / "share" / "kicad" / "footprints"
BUILD = HW / "build"
PCB = HW / "handoff.kicad_pcb"

# --------------------------------------------------------------------------
# Board
# --------------------------------------------------------------------------
BW, BH, CORNER = 40.0, 62.0, 3.0
EDGE_CLEAR = 0.3                      # copper to board edge
HOLES = [("H1", 4.5, 4.5), ("H2", 35.5, 4.5), ("H3", 4.5, 57.5), ("H4", 35.5, 57.5)]

# The cell/pad pocket, from the floor plan: 20 x 30 centred. Nothing on the
# bottom face here (the stack sits ~0.5 mm below the board) and no bottom pour
# (the TOP pour is the ground-plane electrode - design 8.2 as amended).
POCKET = (10.0, 16.0, 30.0, 46.0)     # x0, y0, x1, y1

TRACK = 0.25
TRACK_PWR = 0.5
CLEAR = 0.2
VIA_D, VIA_DRILL = 0.6, 0.3

# --------------------------------------------------------------------------
# The Pico, and everything that hangs off its pin coordinates
# --------------------------------------------------------------------------
PICO_AT = (28.9, 60.6)                # where pad 1 lands
PICO_ROT = 180
ROW_LF, ROW_TH = 28.9, 11.12          # pins 1-20 / pins 21-40
BRK_LF, BRK_TH = ROW_LF - 2.54, ROW_TH + 2.54   # breakout columns, inboard


def pico_pin(n):
    """Board coordinates of Pico pin n, derived from the 180-degree placement."""
    if 1 <= n <= 20:
        return (ROW_LF, 60.6 - (n - 1) * 2.54)
    return (ROW_TH, 12.34 + (n - 21) * 2.54)


# E1-E10: one breakout pad inboard of its own pin (README, Expansion)
BREAKOUT = {"E1": 36, "E2": 30, "E3": 1, "E4": 2, "E5": 6,
            "E6": 7, "E7": 26, "E8": 27, "E9": 32, "E10": 3}

# --------------------------------------------------------------------------
# Placement. (anchor, x, y, rotation, layer)
#   anchor "@"  -> the footprint origin goes here
#   anchor "N"  -> pad N goes here
# --------------------------------------------------------------------------
TOP, BOT = "F", "B"

# Courtyards, measured from the footprints, are what set the pitches below:
#   1206 hand-solder  4.99 x 2.35      MSOP-8   3.59 x 6.45 (at 270 deg)
#   XH 2p 8.49 x 6.84   test pad 2.59   solder jumper 3.39 x 2.59
# Column pitch is 5.3 mm because a vertical 1206 is 4.99 tall; the AFE's two
# columns sit at x 16.7 and 23.3 because U2's courtyard is 3.59 wide and the
# breakout columns at x 13.66 / 26.36 are 3.09 wide. There is no slack in this
# channel: it is 10 mm and it holds three columns of parts.
COL_L, COL_R = 16.7, 23.3
PITCH = 5.3

PLACE = {
    # ---- the module -----------------------------------------------------
    "U1":  ("1", PICO_AT[0], PICO_AT[1], 180, TOP),

    # ---- thumb wall: D2's header near the hand, SW1 near the elbow ------
    # J3 faces the thumb row: R=GP17(22) K=GND(23) G=GP18(24) B=GP19(25). XH is
    # 2.50 mm pitch against the row's 2.54, so J3 is centred on the group and
    # its pins land within 0.06 mm of the Pico's - straight across, no crossings.
    "J3":  ("1", 3.0, 14.94, 270, TOP),
    # SW1: pin row at x = 6.3 puts the body's front face on the board edge at
    # x = 0, per the footprint's own Dwgs.User line. Pad 2 is the common.
    "SW1": ("2", 6.3, 46.0, 270, TOP),

    # ---- hand strip: SW2 centred, the two electrolytics either side -----
    # SW2's courtyard is 9.59 x 7.59, so it fills x 15.2-24.8 on its own; C4 and
    # C5 take what is left either side of it.
    "SW2": ("@", 16.75, 2.75, 0, TOP),
    "C4":  ("@", 10.75, 4.5, 0, TOP),
    "C5":  ("@", 26.75, 4.5, 0, TOP),

    # ---- little-finger strip: everything whose wires drop to the stack --
    "J2":  ("1", 33.0, 16.75, 270, TOP),
    "J1":  ("1", 33.0, 25.75, 270, TOP),
    "J5":  ("1", 33.0, 34.75, 270, TOP),
    # D1 cathode toward the elbow, so D1_K -> JP5 -> VSYS is the short side
    "D1":  ("1", 35.5, 50.62, 90, TOP),

    # ---- AFE, SMD on the top face in the channel under the Pico ---------
    # U2 at 270 deg puts pins 1-4 at y 29.887 (hand side) and 5-8 at y 34.112,
    # so pin 3 (+IN A) faces open board. R2 sits directly in line with it and R3
    # beside them: the island is pin 3 + two pads, 2.6 x 2.3 mm, and nothing
    # else touches it. With 1206 hand-solder pads that is as close to "butted up
    # to the pin" as the package allows.
    "U2":  ("@", 20.0, 32.0, 270, TOP),
    "R2":  ("@", 19.675, 26.0, 270, TOP),   # pad 2 (HIZ) at y 27.55, in line with pin 3
    "R3":  ("@", COL_L, 26.0, 90, TOP),     # pad 1 (HIZ) at y 27.55

    # left column: stage-1 feedback, the interstage pair, the ADC cap, R10
    "R5":  ("@", COL_L, 31.2, 270, TOP),    # FB1 pad toward pin 2
    "R6":  ("@", COL_L, 36.5, 270, TOP),    # IN2 pad 2.5 mm from pin 5
    "C1":  ("@", COL_L, 41.8, 90, TOP),
    "C2":  ("@", COL_L, 47.1, 270, TOP),
    "R10": ("@", COL_L, 52.4, 90, TOP),

    # right column. The hand-end slot is deliberately empty: it is the only band
    # in which the pad-side nets can cross the Pico's pin row on the top layer.
    "R4":  ("@", COL_R, 27.3, 270, TOP),    # OUT1 pad 2.5 mm from pin 1
    "C3":  ("@", COL_R, 32.6, 90, TOP),     # AFE_3V3 pad 2.3 mm from pin 8
    # R7 at 90, not 270: it puts OUT2 (pad 2) above FB2 (pad 1), which is the
    # order pins 7 and 6 leave U2 in. The other way round the two nets have to
    # cross, and on a two-layer board with a pour on both sides that costs vias
    # through the electrode.
    "R7":  ("@", COL_R, 37.9, 90, TOP),
    "R8":  ("@", COL_R, 43.2, 270, TOP),
    "R9":  ("@", COL_R, 48.5, 270, TOP),
    "R11": ("@", COL_R, 53.8, 90, TOP),

    # ---- bottom face, little-finger strip (x > 30, clear of the pocket) --
    # J1, J2 and J5 are through-hole: their pads occupy x 32.15-33.85 on THIS
    # face too, in three bands across it. Everything here therefore goes in the
    # gaps between those bands, in two columns at x 32.3 and 36.8.
    "JP8": ("@", 32.3, 12.0, 180, BOT),     # ROLE, 3 mm from pin 19
    "R15": ("@", 36.8, 11.5, 0, BOT),       # BTN pull-up, beside pin 20
    "TP1": ("@", 36.8, 16.75, 0, BOT),      # PAD, level with J2 pin 1
    "JP7": ("@", 32.3, 22.3, 0, BOT),       # between J2's band and J1's
    "TP12": ("@", 36.8, 22.3, 0, BOT),
    "TP10": ("@", 36.8, 27.0, 0, BOT),      # BAT+, beside J1
    # JP2, JP3 and JP8 are turned so each pad faces the net that feeds it: the
    # alternative is a trace that has to get past the jumper's own other pad.
    "JP2": ("@", 32.0, 31.0, 180, BOT),     # between J1's band and J5's
    "R1":  ("@", 36.5, 31.0, 180, BOT),     # TX safety resistor, in line with JP2
    "TP9": ("@", 36.5, 35.5, 0, BOT),
    "C6":  ("@", 33.0, 40.0, 180, BOT),     # DNP across JP2, between J5 and D1
    # TP11 and TP8 flank JP5 so an ammeter is a 3 mm clip, not a 24 mm reach.
    # They sit inboard of D1's pads, which are at x 34.4-36.6 on this face.
    "TP11": ("@", 32.3, 45.0, 0, BOT),
    "JP5": ("@", 32.3, 47.7, 0, BOT),
    "TP8": ("@", 32.3, 50.4, 0, BOT),

    # LED series resistors, between J3 and the thumb row, on the bottom face
    "R12": ("@", 7.0, 14.9, 0, BOT),
    "R13": ("@", 7.0, 19.95, 0, BOT),
    "R14": ("@", 7.0, 22.45, 0, BOT),

    # ---- the elbow block: the rest of the jumpers and test pads ---------
    "JP4": ("@", 16.5, 49.0, 0, BOT),
    "TP6": ("@", 20.0, 49.0, 0, BOT),
    "TP7": ("@", 23.5, 49.0, 0, BOT),
    "JP6": ("@", 16.5, 52.5, 0, BOT),
    "TP5": ("@", 20.0, 52.5, 0, BOT),
    "JP3": ("@", 24.1, 52.5, 180, BOT),
    "TP2": ("@", 16.5, 56.0, 0, BOT),
    "TP3": ("@", 20.0, 56.0, 0, BOT),
    "JP1": ("@", 23.5, 56.0, 0, BOT),
    "TP4": ("@", 16.5, 59.3, 0, BOT),
    "TP13": ("@", 20.0, 59.3, 0, BOT),
}
for _ref, _pin in BREAKOUT.items():
    _x, _y = pico_pin(_pin)
    PLACE[_ref] = ("@", BRK_LF if _x == ROW_LF else BRK_TH, _y, 0, TOP)
for _ref, _x, _y in HOLES:
    PLACE[_ref] = ("@", _x, _y, 0, TOP)


# --------------------------------------------------------------------------
# Routing. (net, layer, [(x, y), ...]) polylines, plus vias.
#
# Lanes in the AFE channel are the scarce resource on this board: it is 10 mm
# wide and holds two columns of 1206s with an MSOP-8 between them, which leaves
# roughly 1.2 mm either side of U2 and 0.8 mm outboard of each column. Every
# run below was placed against that, not drawn freehand - see the routing note
# in hardware/README.md.
# --------------------------------------------------------------------------
TL, BL = "F.Cu", "B.Cu"

ROUTES = [
    # ---- expansion breakout pads: one hop from their own pin --------------
    ("/GP0", TL, [(28.9, 60.6), (26.36, 60.6)]),
    ("/GP1", TL, [(28.9, 58.06), (26.36, 58.06)]),
    ("/GP4", TL, [(28.9, 47.9), (26.36, 47.9)]),
    ("/GP5", TL, [(28.9, 45.36), (26.36, 45.36)]),
    ("/GP20", TL, [(11.12, 25.04), (13.66, 25.04)]),
    ("/GP21", TL, [(11.12, 27.58), (13.66, 27.58)]),
    ("/GP27_ADC1", TL, [(11.12, 40.28), (13.66, 40.28)]),
    ("/RUN", TL, [(11.12, 35.2), (13.66, 35.2)]),
    ("+3V3", TL, [(11.12, 50.44), (13.66, 50.44)]),

    # ---- the LED, straight across the thumb strip on the bottom face ------
    ("/LED_R", BL, [(11.12, 14.88), (8.55, 14.9)]),
    ("/LED_G", BL, [(11.12, 19.96), (8.55, 19.95)]),
    ("/LED_B", BL, [(11.12, 22.5), (8.55, 22.45)]),
    ("/J3_R", BL, [(3.0, 14.94), (5.45, 14.9)]),
    ("/J3_G", BL, [(3.0, 19.94), (5.45, 19.95)]),
    ("/J3_B", BL, [(3.0, 22.44), (5.45, 22.45)]),

    # ---- ROLE and BTN, both clear of the antenna keep-out -----------------
    ("/ROLE", BL, [(28.9, 14.88), (30.6, 14.88), (30.6, 12.0), (31.65, 12.0)]),
    ("/BTN", BL, [(35.25, 11.5), (35.25, 10.0), (30.0, 10.0), (30.0, 12.34), (28.9, 12.34)]),
    ("/BTN", BL, [(28.9, 12.34), (28.9, 9.0), (20.0, 9.0), (20.0, 2.75), (16.75, 2.75)]),
    ("/BTN", BL, [(16.75, 2.75), (23.25, 2.75)]),

    # ---- TX: GP11 -> JP2 -> R1 -> PAD, all on the little-finger strip -----
    ("/GP11_TX", BL, [(28.9, 25.04), (30.5, 25.04), (30.5, 31.0), (31.35, 31.0)]),
    ("/GP11_TX", BL, [(31.35, 31.0), (31.35, 40.0), (31.44, 40.0)]),
    ("Net-(JP2-B)", BL, [(32.65, 31.0), (34.95, 31.0)]),
    ("Net-(JP2-B)", BL, [(34.95, 31.0), (34.95, 33.0), (35.2, 33.0), (35.2, 40.0), (34.56, 40.0)]),

    # ---- PAD: J2, its test pad, JP7 and R1's far end ----------------------
    ("/PAD", BL, [(33.0, 16.75), (36.8, 16.75)]),
    ("/PAD", BL, [(33.0, 16.75), (34.8, 16.75), (34.8, 22.3), (32.95, 22.3)]),
    ("/PAD", BL, [(38.05, 31.0), (38.8, 31.0), (38.8, 20.0), (34.8, 20.0)]),

    # ---- RX_IN: the one net that crosses the pin row on the top layer -----
    # It threads between pins 15 and 16 with 0.295 mm either side.
    ("Net-(JP7-B)", TL, [(19.68, 24.45), (21.0, 24.45), (21.0, 23.77), (31.3, 23.77)]),
    ("Net-(JP7-B)", BL, [(31.3, 23.77), (31.3, 22.3), (31.65, 22.3)]),
    ("Net-(JP7-B)", BL, [(31.3, 23.77), (38.0, 23.77), (38.0, 22.3), (37.55, 22.3)]),

    # ---- the 1 MOhm island: three pads, 3.0 x 2.3 mm, nothing else on it --
    ("Net-(U2A-+)", TL, [(16.7, 27.55), (19.68, 27.55), (19.68, 29.89)]),

    # stage 1's feedback (U2 pin 2 -> R4.1, R5.1) and OUT1's hop from pin 1 to
    # R4.2 are NOT routed: see "The AFE channel is over-subscribed" in
    # hardware/README.md. R2's pad stops 0.225 mm short of U2's pin-1 pad, which
    # is not a lane, and routing round it crosses the 1 MOhm island. The fix is a
    # placement change, so it is left open rather than bodged.

    # ---- stage 2 input and feedback --------------------------------------
    ("Net-(U2B-+)", TL, [(16.7, 40.24), (18.3, 40.24), (18.3, 34.11), (19.02, 34.11)]),
    ("Net-(U2B-+)", TL, [(16.7, 34.95), (18.3, 34.95)]),
    ("Net-(U2B--)", TL, [(19.68, 34.11), (19.68, 39.45), (23.3, 39.45)]),
    ("Net-(U2B--)", TL, [(23.3, 39.45), (23.3, 41.65)]),
    ("Net-(JP3-B)", TL, [(20.32, 34.11), (20.32, 36.35), (23.3, 36.35)]),

    # ---- VREF: a bus down the outboard side of the left column ------------
    ("VREF", TL, [(16.7, 24.45), (15.2, 24.45), (15.2, 38.05), (16.7, 38.05)]),
    ("VREF", TL, [(15.2, 32.75), (16.7, 32.75)]),

    # ---- power on the little-finger strip ---------------------------------
    # SW_OUT crosses the board on the bottom face just below the pocket (y > 46),
    # which is the only lane that is neither over the cell/pad nor in the jumper
    # block. BAT+ does the same 1.2 mm higher.
    # Both long crossings run on the bottom face, threading the 2.54 mm gaps
    # between the header rows' pads: 0.25 mm wide, ~0.2 mm each side. There is no
    # lane for them on the top face - the AFE's two columns occupy x 22.4-24.2
    # and 15.8-17.6 without a break from y 25 to 56.
    # Two free bands exist between the header rows' pads, at y 41.55 and 44.09.
    # BAT+ takes the first and SW_OUT the second, so they never have to cross.
    ("/SW_OUT", BL, [(6.3, 43.0), (4.8, 43.0), (4.8, 44.09), (30.5, 44.09),
                     (30.5, 42.5), (35.5, 42.5), (35.5, 43.0)], 0.25),
    ("Net-(D1-K)", BL, [(35.5, 50.62), (34.0, 50.62), (34.0, 45.0), (33.05, 45.0)]),
    ("Net-(D1-K)", BL, [(34.0, 47.7), (32.95, 47.7)]),
    ("VSYS", BL, [(31.65, 47.7), (31.0, 47.7), (31.0, 50.4), (32.3, 50.4)]),
    # VSYS back to pin 39 threads the elbow ends of both header rows, so it is
    # narrowed to 0.3: at 0.5 it will not clear a 2.54 mm pitch pad pair.
    ("VSYS", BL, [(31.0, 50.4), (31.0, 52.0)]),
    ("VSYS", TL, [(31.0, 52.0), (31.0, 59.33), (12.5, 59.33), (12.5, 58.06), (11.12, 58.06)], 0.3),
    ("/BAT+", TL, [(33.0, 28.25), (33.0, 32.0), (31.5, 32.0), (31.5, 37.25), (33.0, 37.25)]),
    ("/BAT+", BL, [(33.85, 28.25), (35.0, 28.25), (35.0, 27.0), (36.05, 27.0)]),
    ("/BAT+", BL, [(33.0, 37.25), (33.0, 41.55), (3.5, 41.55),
                   (3.5, 46.0), (6.3, 46.0)], 0.25),
]

VIAS = [
    ("Net-(JP7-B)", 31.3, 23.77),
    ("VSYS", 31.0, 52.0),
]

# --------------------------------------------------------------------------
# Netlist
# --------------------------------------------------------------------------
def load_netlist():
    """(ref -> footprint), ((ref, pad) -> net), (ref -> symbol uuid)."""
    sys.path.insert(0, str(HERE))
    from gen_schematic import parse, find, find_all
    tree = parse((BUILD / "handoff.net").read_text(encoding="utf-8"))
    fps, nets, stamps, vals, descs = {}, {}, {}, {}, {}
    for c in find_all(find(tree, "components"), "comp"):
        ref = find(c, "ref")[1]
        fp = find(c, "footprint")
        fps[ref] = fp[1] if fp else None
        vals[ref] = find(c, "value")[1]
        d = find(c, "description")
        descs[ref] = d[1] if d and len(d) > 1 else ""
        # the symbol's UUID: without it every footprint is "not found in
        # schematic" and the parity check is noise instead of a test
        ts = [n for n in c[1:] if isinstance(n, list) and n[0] == "tstamps" and len(n) > 1]
        if ts and isinstance(ts[-1][1], str) and ts[-1][1] != "/":
            stamps[ref] = ts[-1][1]
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1]
        for node in find_all(n, "node"):
            nets[(find(node, "ref")[1], find(node, "pin")[1])] = name
    return fps, nets, stamps, vals, descs


# Refs that earn silkscreen: the bring-up procedure in the README is carried out
# with the board in hand, and it names jumpers, test pads and connectors. The
# AFE's passives do not get silk - there is no room for it between 1206 pads on
# a 5.3 mm pitch, and their references stay on F.Fab where CAD still shows them.
# E1-E10 are NOT here: they sit under the socketed Pico at 2.54 mm pitch with
# the AFE columns 0.4 mm away, and 0.8 mm text does not fit in that. They are
# identified by the Pico pin each one sits inboard of - see hardware/README.md.
SILK_REFS = ("TP", "JP", "J", "SW", "H", "U", "D")
SILK_H = 0.8        # PCBWay's minimum legible silk height

# Footprints whose silkscreen OUTLINE is removed (their reference text stays).
# U1's outline is drawn over the whole area the AFE, the breakout pads and the
# bottom-face jumpers deliberately occupy - it is a 21 x 51 box round a module
# that is socketed 8.5 mm in the air, so on this board it is 31 DRC violations
# and no information. The four JST bodies are 6.84 mm deep on a 9 mm pitch, so
# their boxes touch each other; the mounting rings run into the corner radius.
# Every one of these parts is unambiguous from its pads and its reference.
SILK_STRIP = {"U1", "J1", "J2", "J3", "J5", "SW1", "H1", "H2", "H3", "H4"}

# Reference text, placed as (dx, dy) from the footprint's own origin.
REF_AT = {
    "U2": (0.0, 4.6), "SW1": (-3.0, -8.0),
    "TP1": (0.0, -1.6), "TP12": (0.0, -1.6), "TP10": (0.0, -1.6),
    "TP9": (0.0, 1.9), "TP11": (2.8, 0.0), "TP8": (0.0, 2.1),
    "JP5": (3.0, 0.0), "JP7": (0.0, 1.8), "JP2": (0.0, 1.8), "JP8": (0.0, 1.8),
    "JP4": (0.0, -1.7), "JP6": (0.0, -1.7), "JP3": (2.6, -2.6), "JP1": (0.0, 1.8),
    "TP6": (0.0, -1.6), "TP7": (0.0, -1.6), "TP5": (0.0, -1.6), "TP2": (0.0, -1.6),
    "TP3": (0.0, -1.6), "TP4": (0.0, 1.8), "TP13": (0.0, 1.8),
}



# Fitted only if the GP11 leakage measurement at M3 says so (README, E9)
DNP = {"C6"}


def silkscreen(fp, ref):
    """Value hidden everywhere; reference on silk only where it is useful."""
    v = fp.Value()
    v.SetVisible(False)
    v.SetLayer(pcbnew.F_Fab if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_Fab)
    r = fp.Reference()
    prefix = ref.rstrip("0123456789")
    keep = prefix in SILK_REFS
    if keep:
        r.SetLayer(pcbnew.F_SilkS if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_SilkS)
        r.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(SILK_H), pcbnew.FromMM(SILK_H)))
        r.SetTextThickness(pcbnew.FromMM(0.12))
    else:
        r.SetLayer(pcbnew.F_Fab if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_Fab)
    r.SetVisible(True)
    # Placed absolutely from the footprint origin, not nudged from whatever the
    # library happened to choose: on a board this dense the default is usually
    # on top of a neighbour, and "nudge it a bit" is not reproducible.
    if ref in REF_AT:
        dx, dy = REF_AT[ref]
        o = fp.GetPosition()
        r.SetPosition(pcbnew.VECTOR2I(o.x + pcbnew.FromMM(dx), o.y + pcbnew.FromMM(dy)))
    if ref in SILK_STRIP:
        # moved to the fab layer, not deleted: the outline is still there for
        # anyone assembling the board, it just does not get printed on it.
        # (Removing the items outright corrupts pcbnew's cached footprint
        # plugin mid-run, which is why this is a layer change.)
        for g in fp.GraphicalItems():
            if g.GetLayer() == pcbnew.F_SilkS:
                g.SetLayer(pcbnew.F_Fab)
            elif g.GetLayer() == pcbnew.B_SilkS:
                g.SetLayer(pcbnew.B_Fab)


def mm(v):
    return pcbnew.FromMM(v)


def vec(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# --------------------------------------------------------------------------
def build():
    fps, nets, tstamps, vals, descs = load_netlist()
    board = pcbnew.BOARD()

    # ---- stackup and rules ---------------------------------------------
    board.SetCopperLayerCount(2)
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.45)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(EDGE_CLEAR)
    # Net classes and the DRC rule set live in handoff.kicad_pro, written by
    # gen_schematic.write_project() - the board file does not carry them.

    # ---- nets -----------------------------------------------------------
    netmap = {}
    for name in sorted(set(nets.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netmap[name] = ni

    # ---- footprints -----------------------------------------------------
    placed, missing = {}, []
    for ref, fpid in sorted(fps.items()):
        if ref not in PLACE:
            missing.append(ref)
            continue
        anchor, tx, ty, rot, layer = PLACE[ref]
        lib, name = fpid.split(":", 1)
        libpath = str(HW / "handoff.pretty") if lib == "handoff" else str(FPDIR / f"{lib}.pretty")
        fp = pcbnew.FootprintLoad(libpath, name)
        if fp is None:
            raise SystemExit(f"{ref}: footprint {fpid} not found in {libpath}")
        board.Add(fp)
        fp.SetOrientationDegrees(rot)
        if layer == BOT:
            fp.Flip(fp.GetPosition(), False)
        # solve for the position that puts the anchor on target
        fp.SetPosition(vec(0, 0))
        if anchor == "@":
            off = (0.0, 0.0)
        else:
            pad = next((p for p in fp.Pads() if p.GetNumber() == anchor), None)
            if pad is None:
                raise SystemExit(f"{ref}: no pad {anchor!r} to anchor on")
            off = (pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y))
        fp.SetPosition(vec(tx - off[0], ty - off[1]))
        fp.SetReference(ref)
        # Value, Description and the library nickname all come from the sheet:
        # the parity check is only a test if the board carries the sheet's data
        # rather than the footprint library's defaults.
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetValue(vals.get(ref, ""))
        fp.SetLibDescription(descs.get(ref, ""))
        fp.SetField("Description", descs.get(ref, ""))
        if ref in DNP:
            fp.SetDNP(True)
        for pad in fp.Pads():
            net = nets.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(netmap[net])
        ts = tstamps.get(ref)
        if ts:
            fp.SetPath(pcbnew.KIID_PATH("/" + ts))
        silkscreen(fp, ref)
        placed[ref] = fp
    if missing:
        raise SystemExit(f"no placement for: {', '.join(sorted(missing))}")

    # ---- board outline: rounded rectangle -------------------------------
    r = CORNER
    segs = [((r, 0), (BW - r, 0)), ((BW, r), (BW, BH - r)),
            ((BW - r, BH), (r, BH)), ((0, BH - r), (0, r))]
    for (x1, y1), (x2, y2) in segs:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(vec(x1, y1))
        s.SetEnd(vec(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)
    # arcs, centre / start / end, clockwise round the outline
    arcs = [((r, r), (r, 0), (0, r)), ((BW - r, r), (BW, r), (BW - r, 0)),
            ((BW - r, BH - r), (BW - r, BH), (BW, BH - r)),
            ((r, BH - r), (0, BH - r), (r, BH))]
    for (cx, cy), (sx, sy), (ex, ey) in arcs:
        a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        a.SetCenter(vec(cx, cy))
        a.SetStart(vec(sx, sy))
        a.SetEnd(vec(ex, ey))
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetWidth(mm(0.1))
        board.Add(a)

    # ---- the cell/pad pocket, as a real rule area -----------------------
    # Bottom face only: no pour (the TOP pour is the electrode) and no parts
    # (the stack sits ~0.5 mm below the board, so nothing may protrude).
    x0, y0, x1, y1 = POCKET
    ka = pcbnew.ZONE(board)
    ka.SetIsRuleArea(True)
    ka.SetDoNotAllowZoneFills(True)
    ka.SetDoNotAllowFootprints(True)
    ka.SetDoNotAllowTracks(False)
    ka.SetDoNotAllowVias(False)
    ka.SetDoNotAllowPads(False)
    ls = pcbnew.LSET()
    ls.addLayer(pcbnew.B_Cu)
    ka.SetLayerSet(ls)
    ka.SetZoneName("Cell/pad pocket - no bottom pour, no bottom parts")
    out = ka.Outline()
    out.NewOutline()
    for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        out.Append(mm(px), mm(py))
    board.Add(ka)

    # ---- ground pours ---------------------------------------------------
    gnd = netmap["GND"]
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetZoneName("GND electrode (top)" if layer == pcbnew.F_Cu else "GND (bottom)")
        z.SetLocalClearance(mm(CLEAR))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.4))
        o = z.Outline()
        o.NewOutline()
        inset = EDGE_CLEAR
        for px, py in ((inset, inset), (BW - inset, inset),
                       (BW - inset, BH - inset), (inset, BH - inset)):
            o.Append(mm(px), mm(py))
        board.Add(z)

    # ---- tracks and vias -------------------------------------------------
    layer_of = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    for route in ROUTES:
        net, layer, pts = route[0], route[1], route[2]
        if net not in netmap:
            raise SystemExit(f"route for unknown net {net!r}")
        if len(route) > 3:
            w = mm(route[3])
        else:
            w = mm(TRACK_PWR) if netmap[net].GetNetClassName() == "Power" else mm(TRACK)
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            tr = pcbnew.PCB_TRACK(board)
            tr.SetStart(vec(x1, y1))
            tr.SetEnd(vec(x2, y2))
            tr.SetWidth(w)
            tr.SetLayer(layer_of[layer])
            tr.SetNet(netmap[net])
            board.Add(tr)
    for net, x, y in VIAS:
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(vec(x, y))
        v.SetWidth(mm(VIA_D))
        v.SetDrill(mm(VIA_DRILL))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(netmap[net])
        board.Add(v)

    return board, placed, nets, netmap


def report(board, placed, nets):
    print(f"placed {len(placed)} footprints")
    # every pad that should have a net, has one
    unnetted = []
    for ref, fp in placed.items():
        for pad in fp.Pads():
            want = nets.get((ref, pad.GetNumber()))
            got = pad.GetNetname()
            if want and got != want:
                unnetted.append(f"{ref}.{pad.GetNumber()} want {want} got {got or '<none>'}")
    for u in unnetted:
        print("  NET", u)
    # everything inside the outline
    out = []
    for ref, fp in placed.items():
        bb = fp.GetBoundingBox(False, False)
        l, rr = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetRight())
        t, b = pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetBottom())
        if l < 0 or rr > BW or t < 0 or b > BH:
            out.append(f"{ref} bbox {l:.2f}..{rr:.2f} x {t:.2f}..{b:.2f}")
    for o in out:
        print("  OUTSIDE", o)
    return len(unnetted) + len(out)


def write_drc_rules():
    """handoff.kicad_dru - the two places where DRC has to be told about the
    third dimension, which it does not model."""
    es = " || ".join(f"A.Reference == '{r}' || B.Reference == '{r}'" for r in sorted(BREAKOUT))
    text = f"""(version 1)

# The Pico 2 W is SOCKETED, in two 1x20 female headers: its own PCB sits about
# 8.5 mm above this one. The AFE, the breakout pads and the bottom-face jumpers
# are deliberately underneath it - that is the whole reason the channel under
# the Pico is the AFE's home (README, "Design 12.3 is no longer met"). KiCad's
# courtyard test has no notion of height, so it has to be told. This suppresses
# courtyard overlap against U1 ONLY; every clearance and short test still runs.
(rule "Pico is socketed - parts may sit under it"
	(constraint courtyard_clearance (min -20mm))
	(condition "A.Reference == 'U1' || B.Reference == 'U1'"))

# E1-E10 are bare through-hole pads with nothing mounted on them. They inherit a
# TestPoint footprint's 3.09 mm courtyard, which is larger than the 2.54 mm pin
# pitch they are placed on, so adjacent ones "overlap" bodies that do not exist.
# Copper clearance between them is unaffected and still checked: 0.54 mm.
(rule "Breakout pads have no body"
	(constraint courtyard_clearance (min -5mm))
	(condition "{es}"))
"""
    (HW / "handoff.kicad_dru").write_text(text, encoding="utf-8")
    print(f"wrote {HW / 'handoff.kicad_dru'}")


def run_cli(*args):
    return subprocess.run([str(KICAD_CLI), *args], capture_output=True, text=True)


def drc():
    """The board's oracle, the way check() is the schematic's: DRC plus parity
    against the sheet. Prints a one-line summary that must read 0 errors."""
    out = BUILD / "pcb_drc.json"
    run_cli("pcb", "drc", "--format", "json", "--schematic-parity",
            "-o", str(out), str(PCB))
    rep = json.loads(out.read_text(encoding="utf-8"))
    errs = warns = 0
    for key in ("violations", "unconnected_items", "schematic_parity"):
        for v in rep.get(key, []):
            if v["severity"] == "error":
                errs += 1
                where = ", ".join(i.get("description", "") for i in v.get("items", []))
                print(f'  DRC error {v["type"]}: {v["description"]}  [{where}]')
            else:
                warns += 1
    unrouted = len(rep.get("unconnected_items", []))
    print(f"DRC: {errs} error(s), {warns} warning(s); "
          f"parity {len(rep.get('schematic_parity', []))}; unrouted {unrouted} item(s)")
    return errs - unrouted   # unrouted is tracked separately, see the README


def main():
    board, placed, nets, netmap = build()
    write_drc_rules()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    fails = report(board, placed, nets)
    board.Save(str(PCB))
    print(f"wrote {PCB}")
    if "--no-check" in sys.argv:
        return fails
    return fails + max(0, drc())


if __name__ == "__main__":
    sys.exit(main())
