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

Routing is declarative too: polylines of right-angle and 45-degree legs. Every
right-angle corner is mitred at 45 degrees by mitre() before it becomes copper,
so ROUTES can be written on the grid and the board still has no right angles.

The run ends with four proofs, all of which must pass: DRC (0 errors), parity
with the sheet (0), 0 unrouted, and an independent short check that walks
every pad, track, via and pour and confirms no two items on different nets are
within CLEAR of each other. Any silkscreen or fab text outside the outline,
or any footprint outside it that is not in OVERHANG, fails the run.
"""
import itertools
import json
import os
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
PRO = HW / "handoff.kicad_pro"

sys.path.insert(0, str(HERE))
from gen_schematic import NET_CLASSES, apply_board_settings, parse, find, find_all  # noqa: E402

# --------------------------------------------------------------------------
# Board
# --------------------------------------------------------------------------
BW, BH, CORNER = 40.0, 62.0, 3.0
EDGE_CLEAR = 0.3                      # copper to board edge (the fab minimum)
EDGE_MARGIN = 0.5                     # what the layout actually keeps to the edge
HOLES = [("H1", 4.5, 4.5), ("H2", 35.5, 4.5), ("H3", 4.5, 57.5), ("H4", 35.5, 57.5)]
BOSS_R = 4.0                          # the 8 mm enclosure boss, as gen_mount_footprint draws it

# The cell/pad pocket, from the floor plan: 20 x 30 centred. Nothing on the
# bottom face here (the stack sits ~0.5 mm below the board) and no bottom pour
# (the TOP pour is the ground-plane electrode - design 8.2 as amended).
POCKET = (10.0, 16.0, 30.0, 46.0)     # x0, y0, x1, y1

TRACK = 0.25
CLEAR = 0.2
VIA_D, VIA_DRILL = 0.6, 0.3
MITRE = 0.5                           # chamfer on every right-angle corner

# Track width per net, from the shared net-class table (gen_schematic.py):
# 0.5 mm on the cell's path, 0.4 mm on the rails, 0.25 mm on everything else.
WIDTH = {net: w for w, nets in NET_CLASSES.values() for net in nets}
NECK = 0.4       # a Power track threading a Pico pin-row gap (0.94 mm) narrows
                 # to this for 2.6 mm: 0.27 mm to each pad instead of 0.22

# --------------------------------------------------------------------------
# The Pico, and everything that hangs off its pin coordinates
# --------------------------------------------------------------------------
PICO_AT = (28.9, 60.6)                # where pad 1 lands
PICO_ROT = 180
ROW_LF, ROW_TH = 28.9, 11.12          # pins 1-20 / pins 21-40
# Breakout columns sit OUTBOARD of their pin rows, between the row and the wall.
# They were inboard, one hop into the channel, and that is what over-subscribed
# it: two columns of pads plus two columns of 1206s plus an MSOP-8 in 10 mm.
# Outboard they cost the channel nothing, and each pad is still a single hop
# from its own pin. 8.5 on the thumb side leaves 0.97 mm between the pads and
# the pin row, which is the lane VSYS runs down; 31.44 on the little-finger
# side keeps that column out of H4's 8 mm boss keep-out.
BRK_LF, BRK_TH = 31.44, 8.5


def pico_pin(n):
    """Board coordinates of Pico pin n, derived from the 180-degree placement."""
    if 1 <= n <= 20:
        return (ROW_LF, 60.6 - (n - 1) * 2.54)
    return (ROW_TH, 12.34 + (n - 21) * 2.54)


def gap(n, m):
    """y of the centre of the gap between adjacent pins n and m."""
    return (pico_pin(n)[1] + pico_pin(m)[1]) / 2


# E1-E10: one breakout pad outboard of its own pin (README, Expansion)
# Pins 1, 2 and 3 are not here. A pad at x 31.44 reaches x 32.19, which is
# 3.31 mm from H4's centre and so inside the 8 mm boss the M3 screw stands in -
# DRC does not see it because the boss keep-out bars tracks, not pads. The
# little-finger wall is clear only between J5's housing (ends y 40.24) and that
# boss (starts y 53.5), which is pins 5-8. GP0 and GP1 give way to GP3 and a
# ground pad; GP1's breakout moves to GP22 on the thumb row.
BREAKOUT = {"E1": 36, "E2": 30, "E3": 5, "E4": 29, "E5": 6,
            "E6": 7, "E7": 26, "E8": 27, "E9": 32, "E10": 8}

# --------------------------------------------------------------------------
# A route point that means "the centre of this pad", resolved against the real
# footprint after placement.
#
# Every endpoint in ROUTES used to be a literal, which meant each one silently
# encoded its part's package: a 1206 hand-solder pad sits 1.55 mm from the
# body centre and an 0603 one 0.9125 mm, so re-ordering the 100 k / 10 k / 100 R
# in 0603 moved twenty-odd track ends off their pads at once. P() says which
# pad is meant and lets pcbnew work out where it is; dx/dy give a point a fixed
# offset from it (the start of a 45, a lane that has to stay in line with a pad
# row). Lane coordinates that are not pads stay literal.
# --------------------------------------------------------------------------
class Pad:
    __slots__ = ("ref", "num", "dx", "dy")

    def __init__(self, ref, num, dx=0.0, dy=0.0):
        self.ref, self.num, self.dx, self.dy = ref, num, dx, dy

    def __repr__(self):
        return f"P({self.ref}.{self.num}{self.dx:+g}{self.dy:+g})"


def P(ref, num, dx=0.0, dy=0.0):
    return Pad(ref, num, dx, dy)


# --------------------------------------------------------------------------
# Placement. (anchor, x, y, rotation, layer)
#   anchor "@"  -> the footprint origin goes here
#   anchor "N"  -> pad N goes here
# --------------------------------------------------------------------------
TOP, BOT = "F", "B"

# Courtyards, measured from the footprints, are what set the pitches below:
#   1206 hand-solder  4.99 x 2.35      MSOP-8   3.59 x 6.45 (at 270 deg)
#   XH 2p 8.49 x 6.84   test pad 2.59   solder jumper 3.39 x 2.59
# Column pitch is 5.3 mm because a vertical 1206 is 4.99 tall and two of them
# end to end need 0.2 mm of copper clearance between their pads (0.425 at
# 5.3; at 5.0 it would be 0.125, a DRC error). The AFE's two columns sit at
# x 15.6 and 24.4: a 1.43 mm lane each side of U2 and 2.5 mm outboard of each.
COL_L, COL_R = 15.6, 24.4
PITCH = 5.3

# The little-finger connectors' pin column. At 34.5 the XH housings (6.8 mm
# across, and deeper on the inboard side) span x 30.6-37.4: clear of the
# Pico's own PCB edge at x 30.5, which is 8.5 mm up in its sockets, so a plug
# and its wires have nothing above them. At the floor plan's 33.0 the housing
# ran 1.4 mm under the Pico.
J_X = 34.5

PLACE = {
    # ---- the module -----------------------------------------------------
    "U1":  ("1", PICO_AT[0], PICO_AT[1], 180, TOP),

    # ---- thumb wall, hand to elbow: J3, then the battery path, then SW1 --
    # J3 faces the thumb row: R=GP17(22) K=GND(23) G=GP18(24) B=GP19(25). XH is
    # 2.50 mm pitch against the row's 2.54, so J3 is centred on the group and
    # its pins land within 0.06 mm of the Pico's - straight across, no
    # crossings. Pin 1 at x 4.0 puts the housing's outer face on the board
    # edge (its courtyard starts at x 0.05); at 3.0 it overhung by 0.45 mm.
    "J3":  ("1", 4.0, 14.94, 270, TOP),
    # SW1: pin row at x = 6.3 puts the body's front face on the board edge at
    # x = 0, per the footprint's own Dwgs.User line. Pad 2 is the common.
    "SW1": ("2", 6.3, 46.0, 270, TOP),
    # D1 and JP5 sit between J3 and SW1, so the whole battery path lives on
    # the thumb wall: BAT+ crosses the board once (J1 to SW1), SW_OUT is a
    # 7 mm hop from SW1's pin 1 to D1's anode, and VSYS runs down the lane
    # between the breakout pads and the pin row straight into pin 39, which
    # is on this row. On the little-finger side, where they were, the path
    # crossed the board three times and threaded the pin rows six times.
    # D1's cathode faces the hand, toward JP5; the anode faces SW1.
    "D1":  ("1", 4.5, 29.8, 270, TOP),
    "JP5": ("@", 4.0, 26.9, 180, BOT),    # pad 1 D1_K west, pad 2 VSYS east

    # ---- hand strip: SW2 centred, the two bulk caps either side ---------
    # SW2's courtyard is 9.59 x 7.59, so it fills x 15.2-24.8 on its own; C4 and
    # C5 take what is left either side of it.
    #
    # C4 and C5 were 5 mm radial electrolytics on the TOP face, and every track
    # that fed them ran on the BOTTOM and reached them through their through-hole
    # leads. As 0805 MLCC they have no leads, so they move to the bottom face -
    # where their tracks already were - and each is anchored on pad 1, on the
    # exact point the old part's + lead stood. That keeps both nets' routes
    # unchanged, and hands the top face 26 mm2 of electrode back.
    "SW2": ("@", 16.75, 2.75, 0, TOP),
    "C4":  ("1", 10.75, 5.2, 180, BOT),   # + on the divider run down x 10.75
    "C5":  ("1", 26.75, 4.5, 180, BOT),   # + where the radial's + lead was
    # The VREF divider sits with C4, its own hold-up capacitor, on the bottom
    # face of the hand strip, instead of at the elbow. That is 46 mm less
    # divider net, and it frees the one top-face lane on the thumb side of
    # the Pico (x 9.7), which VSYS needs, and the VREF via at the elbow. The
    # divider's supply comes off the same bottom-face run that feeds C5.
    "R10": ("2", 10.75, 1.9, 0, BOT),     # pad 2 (divider) in line with C4's +
    "R11": ("1", 11.5, 8.4, 180, BOT),    # pad 1 (divider) toward C4, pad 2 GND
    "JP6": ("@", 9.2, 10.4, 270, BOT),    # pad 1 divider north, pad 2 VREF south

    # ---- little-finger strip: everything whose wires drop to the stack --
    "J2":  ("1", J_X, 16.75, 270, TOP),
    "J1":  ("1", J_X, 25.75, 270, TOP),
    "J5":  ("1", J_X, 34.75, 270, TOP),

    # ---- AFE, SMD on the top face in the channel under the Pico ---------
    # U2 at 270 deg puts pins 1-4 at y 29.887 (hand side) and 5-8 at y 34.112,
    # so pin 3 (+IN A) faces open board. R2 sits directly in line with it and R3
    # beside them: the island is pin 3 + two pads, 2.6 x 2.3 mm, and nothing
    # else touches it. With 1206 hand-solder pads that is as close to "butted up
    # to the pin" as the package allows.
    "U2":  ("@", 20.0, 32.0, 270, TOP),
    # R2 sits 2 mm further toward the hand than the first pass put it. Its pad is
    # 1.3 mm wide and lands on x 19.025-20.325, which is exactly over pins 3 and
    # 2; at y 26.0 it left a 0.9 mm slot above U2's pad row and pins 1 and 2 had
    # nowhere to escape to. At 24.0 the slot is 2.65 mm and takes two tracks.
    # The cost is the 1 MOhm island: 8.4 mm of trace instead of 6.4.
    "R2":  ("@", 19.675, 24.0, 270, TOP),   # pad 2 (HIZ) at y 25.55, in line with pin 3
    "R3":  ("@", COL_L, 26.0, 90, TOP),     # pad 1 (HIZ) at y 27.55

    # left column: stage-1 feedback, the interstage pair, the ADC cap
    "R5":  ("@", COL_L, 30.5625, 270, TOP),  # FB1 pad on 29.65, where the 1206 put it
    "R6":  ("@", COL_L, 36.5, 270, TOP),    # IN2 pad 2.5 mm from pin 5
    "C1":  ("@", COL_L, 41.8, 90, TOP),
    "C2":  ("@", COL_L, 47.1, 270, TOP),

    # right column. The hand-end slot is deliberately empty: it is the only band
    # in which the pad-side nets can cross the Pico's pin row on the top layer.
    "R4":  ("@", COL_R, 27.3, 270, TOP),    # OUT1 pad 2.5 mm from pin 1
    # C3's supply pad is 3.4 mm from pin 8 and its ground pad returns through
    # the pour to pin 4, which is the diagonally opposite corner of the
    # package. The slot above (R4) is what lets pins 1 and 2 out and the slot
    # below (R7) is what lets 6 and 7 out; turning C3 flat would block one
    # row's fan-out. 3.4 mm of 0.4 mm track on a 100 nF at a 10 MHz op-amp
    # is the loop the package allows.
    "C3":  ("@", COL_R, 32.6, 90, TOP),     # AFE_3V3 pad toward pin 8
    # R7 at 90, not 270: it puts OUT2 (pad 2) above FB2 (pad 1), which is the
    # order pins 7 and 6 leave U2 in. The other way round the two nets have to
    # cross, and on a two-layer board with a pour on both sides that costs vias
    # through the electrode.
    "R7":  ("@", COL_R, 37.9, 90, TOP),
    "R8":  ("@", COL_R, 43.2, 270, TOP),
    "R9":  ("@", COL_R, 48.5, 270, TOP),

    # ---- bottom face, little-finger strip (x > 30, clear of the pocket) --
    # J1, J2 and J5 are through-hole: their pads occupy x 33.65-35.35 on THIS
    # face too, in three bands across it. Everything here goes in the gaps
    # between those bands, or outboard of them.
    "JP8": ("@", 32.3, 14.88, 180, BOT),    # ROLE, in line with pin 19
    "JP7": ("@", 32.3, 22.3, 0, BOT),       # between J2's band and J1's; pad 1 PAD east
    # JP2 and R1 sit in line between J1's band and J5's: GP11 arrives from the
    # west, PAD leaves at the wall. C6 (DNP) straddles BAT+'s run south from J5.
    "JP2": ("@", 31.7, 31.0, 180, BOT),    # x 31.7: its courtyard just clears the pocket
    "R1":  ("@", 36.5, 31.0, 180, BOT),     # TX safety resistor, in line with JP2
    "C6":  ("@", J_X, 40.0, 180, BOT),      # DNP across JP2, pads either side of BAT+
    # R15 is the BTN pull-up. It sits at the elbow end of this strip because
    # +3V3 is on the elbow end of the thumb row (pin 36): the rail crosses the
    # channel once, at the elbow, and the long run up the wall to pin 20 is
    # the static BTN line at 0.25 mm, not the Pico's switching rail beside the
    # receive node.
    "R15": ("@", 36.5, 46.63, 180, BOT),    # pad 1 +3V3 west, pad 2 BTN at the wall

    # LED series resistors, between J3 and the thumb row, on the bottom face.
    # Each sits on the mean of its Pico pin's y and its J3 pin's y (the rows
    # are 2.54 and 2.50 pitch), so both stubs slant by 0.03 mm, not one by 0.06.
    "R12": ("@", 7.5, 14.91, 0, BOT),
    "R13": ("@", 7.5, 19.95, 0, BOT),
    "R14": ("@", 7.5, 22.47, 0, BOT),

    # ---- the elbow block ------------------------------------------------
    # Three solder jumpers now (JP6 went to the hand end with its divider).
    # JP4 and JP3 sit on y 50.44, pin 36's row, so +3V3 runs straight out of
    # the pin into JP4's pad; each jumper is turned so the pad faces the net
    # that feeds it.
    "JP4": ("@", 16.5, 50.44, 180, BOT),   # pad 1 +3V3 west, pad 2 AFE_3V3 east
    "JP3": ("@", 23.5, 50.44, 180, BOT),   # pad 1 OUT1 west, pads 2/3 to R9/R7
    "JP1": ("@", 21.5, 54.0, 0, BOT),      # pad 2 ADC0 west, pad 1 to R9 east

    # ---- test pads, README priority order -------------------------------
    # Through-hole, so each is probed from either face. TP1 is the scope's
    # ground beside the amplifier's input stage, in the top pour above R4 (the
    # pour under U2 is an island between the fan-out tracks and is removed).
    # TP2 and TP3 sit below the elbow block, TP4 beside JP6, TP5 beside R1,
    # and TP6/TP7 either side of JP5 on the thumb wall - TP6 is also where
    # VSYS changes face. AFE_3V3 has no pad: C5's + lead is through-hole.
    "TP1": ("@", 23.0, 23.8, 0, TOP),
    "TP2": ("@", 19.6, 58.0, 0, BOT),
    "TP3": ("@", 23.5, 58.0, 0, BOT),
    "TP4": ("@", 6.5, 10.3, 0, BOT),
    "TP5": ("@", 38.05, 36.0, 0, BOT),
    "TP6": ("@", 6.6, (25.04 + 27.58) / 2, 0, BOT),
    "TP7": ("@", 1.9, 31.5, 0, BOT),   # ref/value text default is fine on the fab layer here
    # TP8 is the eighth of the eight the README asked for. It was dropped last
    # session because C5's + lead was through-hole and made a better probe
    # point than a pad would have; C5 is 0805 now, so that reason is gone.
    "TP8": ("@", 27.5, 8.2, 0, BOT),

    # ---- haptic: the motor driver, bottom face at the thumb end of the elbow
    # This is the only free ground left on the board: x 12-18 between the Pico's
    # thumb pin row and JP1, below JP4. It is also the right place electrically -
    # VSYS arrives at pin 39 (11.12, 58.06), two millimetres away, so the motor's
    # 100 mA pulses never travel the length of the board, and it is 10 mm of
    # board and a ground pour away from the AFE channel.
    # J6's holes sit at x 14.9, not on the Pico's own pin lane: the gate has to
    # come south past them on the top face and a 0603 pad needs 0.98 mm of it.
    "J6":  ("1", 14.9, 53.7, 0, BOT),     # pad 1 MOT_SW (north), pad 2 VSYS
    # 180, so the drain (pad 3) faces east toward J6's switched hole and D3,
    # and the gate and source face the wall. At 0 the drain track would have to
    # cross both of them.
    "Q1":  ("@", 15.0, 60.0, 180, BOT),
    # D3 is on the TOP face: the bottom strip holds J6 and Q1 and nothing else
    # fits. One via closes the flyback loop, which is the only via in it.
    "D3":  ("@", 16.9, 58.6, 270, TOP),
    # The gate chain runs on the top face, where the lane past the pin row is
    # not contested: on the bottom, VSYS already owns it. R16 sits beside
    # pin 34 (GP28) so the drive leaves the module through its series resistor
    # before it goes anywhere.
    # R16 stands on end in the 1.5 mm lane between the Pico's thumb pads and
    # C2's courtyard; R17 lies across the lane below it so the gate run enters
    # its pad 1 from the north and leaves it south - a shunt part that the run
    # passes straight through, not a T. Both pad columns are on x 13.5875.
    "R16": ("@", 13.5875, 47.9, 270, TOP),
    "R17": ("@", 14.5, 51.3, 0, TOP),
}
for _ref, _pin in BREAKOUT.items():
    _x, _y = pico_pin(_pin)
    PLACE[_ref] = ("@", BRK_LF if _x == ROW_LF else BRK_TH, _y, 0, TOP)
for _ref, _x, _y in HOLES:
    PLACE[_ref] = ("@", _x, _y, 0, TOP)

# Footprints allowed to poke past the outline, and why. Everything else that
# does fails the run.
OVERHANG = {
    "SW1": "the 5 mm handle, through the enclosure wall by design",
    "U1": "the USB-cable courtyard past the elbow edge, by design",
}

# --------------------------------------------------------------------------
# Routing. (net, layer, [(x, y), ...]) polylines, optional explicit width.
#
# Lanes in the AFE channel are the scarce resource on this board: it is 10 mm
# wide and holds two columns of 1206s with an MSOP-8 between them. Every run
# below was placed against that - see the routing notes in hardware/README.md.
# Corners are right angles or 45s; mitre() chamfers every right angle.
# --------------------------------------------------------------------------
TL, BL = "F.Cu", "B.Cu"

Y_BATP_LF = gap(8, 9)      # 41.55: BAT+ crosses the little-finger row here
Y_BATP_TH = gap(34, 35)    # 46.63: and the thumb row here, into SW1's common
Y_3V3_LF = gap(6, 7)       # 46.63: +3V3 crosses the little-finger row to R15
Y_RXIN_LF = gap(16, 17)    # 21.23: RX_IN crosses it to JP7
Y_VSYS_E = (25.04 + 27.58) / 2   # 26.31: VSYS threads between E7 and E8
X_VSYS = 9.79              # the lane between the thumb breakout pads and the row
X_VREF_N = 12.45           # VREF's lane north past the antenna keep-out (x 12.91)

ROUTES = [
    # ---- expansion breakout pads: one hop OUTBOARD to their own pin --------
    # The thumb five run on the bottom face so the top lane between the pads
    # and the row stays clear for VSYS. E10 is GND and is in both pours.
    ("/GP20", BL, [pico_pin(26), (BRK_TH, pico_pin(26)[1])]),
    ("/GP21", BL, [pico_pin(27), (BRK_TH, pico_pin(27)[1])]),
    ("/RUN", BL, [pico_pin(30), (BRK_TH, pico_pin(30)[1])]),
    ("/GP27_ADC1", BL, [pico_pin(32), (BRK_TH, pico_pin(32)[1])]),
    ("/GP22", BL, [pico_pin(29), (BRK_TH, pico_pin(29)[1])]),
    ("+3V3", BL, [pico_pin(36), (BRK_TH, pico_pin(36)[1])]),
    # The little-finger five stop at x 31.2, not on the pad centre: H4's 8 mm
    # boss keep-out reaches x 31.5 and bars tracks, though not pads.
    ("/GP3", BL, [pico_pin(5), (31.2, pico_pin(5)[1])]),
    ("/GP4", BL, [pico_pin(6), (31.2, pico_pin(6)[1])]),
    ("/GP5", BL, [pico_pin(7), (31.2, pico_pin(7)[1])]),

    # ---- the LED, straight across the thumb strip on the bottom face ------
    ("/LED_R", BL, [pico_pin(22), P("R12", "1")]),
    ("/LED_G", BL, [pico_pin(24), P("R13", "1")]),
    ("/LED_B", BL, [pico_pin(25), P("R14", "1")]),
    ("/J3_R", BL, [(4.0, 14.94), P("R12", "2")]),
    ("/J3_G", BL, [(4.0, 19.94), P("R13", "2")]),
    ("/J3_B", BL, [(4.0, 22.44), P("R14", "2")]),

    # ---- the battery path, all on the thumb wall ---------------------------
    # BAT+: J1 to J5 on the top face (the connector pins are through-hole, so
    # no via), jogging round J5's ground pin; then down the bottom face from
    # J5, through C6's pad gap, across the whole channel between pins 8/9 and
    # 33/34 (BAT+ is the only track on this band), and into SW1's common from
    # the east. It enters pin 2 from the east so that pin 1's north side is
    # free for SW_OUT to leave.
    ("/BAT+", TL, [(J_X, 28.25), (J_X, 30.0), (33.0, 31.5), (33.0, 35.75), (J_X, 37.25)]),
    ("/BAT+", BL, [(J_X, 37.25), (J_X, Y_BATP_LF), (30.2, Y_BATP_LF)]),
    ("/BAT+", BL, [(30.2, Y_BATP_LF), (27.6, Y_BATP_LF)], NECK),
    ("/BAT+", BL, [(27.6, Y_BATP_LF), (13.2, Y_BATP_LF), (12.7, Y_BATP_LF + 0.5),
                   (12.7, Y_BATP_TH - 0.5), (12.2, Y_BATP_TH)]),
    ("/BAT+", BL, [(12.2, Y_BATP_TH), (10.0, Y_BATP_TH)], NECK),
    ("/BAT+", BL, [(10.0, Y_BATP_TH), (6.93, Y_BATP_TH), (6.3, 46.0)]),
    # SW_OUT: pin 1 north-west, past the switch's mounting ear, into D1's anode.
    # D1 is SMD (SMB) now, F.Cu only: the old bend point becomes a via and a
    # short F.Cu stub continues straight on to the pad (still x = 4.5, so the
    # via-to-pad leg is collinear with the B.Cu leg above it, not a corner).
    ("/SW_OUT", BL, [(6.3, 43.0), (4.5, 41.2), (4.5, 37.42)]),
    ("/SW_OUT", TL, [(4.5, 37.42), P("D1", "2")]),
    # D1's cathode to JP5, and JP5 to VSYS. VSYS goes through TP7 to the top
    # face, threads between E7 and E8, and runs the lane between the breakout
    # pads and the pin row (0.97 mm: 0.28 mm each side of a 0.5 mm track)
    # straight down into pin 39 from the north-west. D1's cathode (pad 1) is
    # also F.Cu only: each branch's old 45-degree bend point becomes a via,
    # with a short F.Cu stub on into the pad at the same 45, so the angle at
    # the via is unchanged (135 degrees, same as it always was at the pad).
    ("/D1_K", BL, [(3.35, 26.9), (3.35, 28.65)]),
    ("/D1_K", TL, [(3.35, 28.65), P("D1", "1")]),
    ("/D1_K", BL, [(1.9, 31.5), (2.8, 31.5)]),
    ("/D1_K", TL, [(2.8, 31.5), P("D1", "1")]),
    ("VSYS", BL, [(4.65, 26.9), (5.24, Y_VSYS_E), (6.6, Y_VSYS_E)]),
    ("VSYS", TL, [(6.6, Y_VSYS_E), (X_VSYS - 0.49, Y_VSYS_E), (X_VSYS, Y_VSYS_E + 0.49),
                  (X_VSYS, 58.06 - 0.49), (X_VSYS + 0.49, 58.06), pico_pin(39)]),

    # ---- ROLE and BTN ------------------------------------------------------
    ("/ROLE", BL, [pico_pin(19), (31.65, 14.88)]),
    # BTN: from R15 up the little-finger wall on the bottom face (a static
    # line, 0.25 mm, 0.68 mm from the edge), west along pin 20's row into the
    # pin, then on the top face round the antenna keep-out's corner to SW2.
    ("/BTN", BL, [P("R15", "2"), (39.2, 44.8425), (39.2, 13.34), (38.2, 12.34), pico_pin(20)]),
    ("/BTN", TL, [pico_pin(20), (28.9, 11.3), (27.3, 9.7), (25.6, 8.0), (25.6, 5.9),
                  (23.25, 3.55), (23.25, 2.75)]),
    ("/BTN", BL, [(16.75, 2.75), (23.25, 2.75)]),    # SW2's two pad-1s

    # ---- TX: GP11 -> JP2 -> R1 -> PAD, all on the little-finger strip -----
    ("/GP11_TX", BL, [pico_pin(15), (30.35, 26.49), (30.35, 30.3), (31.05, 31.0)]),
    ("/GP11_TX", BL, [(31.05, 31.0), (31.05, 38.11), (32.94, 40.0)]),
    ("Net-(JP2-B)", BL, [(32.35, 31.0), (34.95, 31.0)]),
    ("Net-(JP2-B)", BL, [(34.95, 31.0), (36.06, 32.11), (36.06, 40.0)]),

    # ---- PAD: R1's far end up the wall to JP7, and on to J2 ---------------
    ("/PAD", BL, [(38.05, 31.0), (38.05, 23.3), (37.05, 22.3), (32.95, 22.3)]),
    ("/PAD", BL, [(32.95, 22.3), (32.95, 17.6), (33.8, 16.75), (J_X, 16.75)]),
    ("/PAD", BL, [(38.05, 31.0), (38.05, 36.0)]),

    # ---- RX_IN threads the pin row between pins 16 and 17, at y 21.23 -----
    # One gap further toward the hand than the first pass used, leaving the
    # y 23-25 band free.
    ("Net-(JP7-B)", TL, [(19.675, 22.45), (20.895, Y_RXIN_LF), (31.32, Y_RXIN_LF), (31.65, 20.9)]),
    ("Net-(JP7-B)", BL, [(31.65, 20.9), (31.65, 22.3)]),

    # ---- the 1 MOhm island: R3, R2 and pin 3, a T 8.4 mm end to end -------
    # Branches start only at pads or vias, never part-way along a track: a
    # T-junction is two right angles, and DRC's track_angle rule says so.
    ("Net-(U2A-+)", TL, [(19.675, 25.55), (19.675, 29.887)]),
    ("Net-(U2A-+)", TL, [(15.6, 27.55), (17.675, 27.55), (19.675, 25.55)]),

    # ---- stage 1 -----------------------------------------------------------
    # Pin 2 leaves its pad twice: north into the 2.65 mm slot R2 vacated, and
    # south under U2's body, where the island's vertical does not reach.
    ("Net-(U2A--)", TL, [(20.325, 29.887), (20.325, 27.2),
                         P("R4", "1", -2.5, 0.8125), P("R4", "1", -1.6875), P("R4", "1")]),
    ("Net-(U2A--)", TL, [(20.325, 29.887), (20.325, 31.3), (17.0, 31.3),
                         P("R5", "1", 1.4), P("R5", "1")]),
    # OUT1: pin 1 -> R4.2 -> down the OUTERMOST of the three east lanes -> and
    # then across to C1 on the bottom face, not the top.
    #
    # The east lane (x 25.05 column edge to 28.1 Pico pads, 3.05 mm) is the only
    # way out of the AFE toward the elbow: the top face is walled at y 43 by C1
    # and C2. Three nets need it - OUT1, AFE_3V3 and OUT2 (JP3-B) - and it takes
    # three tracks with room to spare. What it cannot take is a crossing, so
    # the lanes are ordered, innermost to the net that enters lowest:
    #   x 25.7  OUT2      enters at y 36.35 (R7.2)
    #   x 26.5  AFE_3V3   enters at y 34.16 (C3.1)
    #   x 27.2  OUT1      enters at y 28.85 (R4.2)
    # Each entry crosses only lanes that have not started yet. OUT1 then
    # leaves the lane by diving: the bottom face at y 43.36, C1's own pad row,
    # is empty across the whole channel. Two vias, and nothing on the top
    # face crosses the channel between the AFE and the elbow.
    ("/OUT1", TL, [(20.975, 29.887), P("R4", "2", -3.425), P("R4", "2"),
                   P("R4", "2", 2.8), (27.2, 43.362)]),
    ("/OUT1", BL, [(27.2, 43.362), (21.8, 43.362), (17.2, 43.362)]),
    ("/OUT1", TL, [(17.2, 43.362), (15.6, 43.362)]),
    # the branch to JP3: up out of the corridor at x 21.8, straight down the
    # top face (clear to the elbow) and through one via into JP3's pad
    ("/OUT1", TL, [(21.8, 43.362), (21.8, 48.6)]),
    ("/OUT1", BL, [(21.8, 48.6), (22.2, 49.0), (22.2, 50.44)]),
    ("/OUT1", TL, [(21.8, 48.6), (21.8, 56.3), (23.5, 58.0)]),

    # ---- stage 2 -----------------------------------------------------------
    # The lower row fans out planar: pin 5 west, then 6, 7 and 8 east in that
    # order, each one lane further out than the pin to its right.
    ("Net-(U2B-+)", TL, [(19.025, 34.112), P("R6", "1", 3.425), P("R6", "1")]),
    ("Net-(U2B-+)", TL, [P("R6", "1"), P("R6", "1", 1.3, 1.3), (16.9, 38.937), (15.6, 40.237)]),
    ("Net-(U2B--)", TL, [(19.675, 34.112), (19.675, 37.6), (22.6, 37.6),
                         P("R7", "1", -1.8), P("R7", "1")]),
    ("Net-(U2B--)", TL, [P("R7", "1"), P("R8", "1")]),
    ("Net-(JP3-B)", TL, [(20.325, 34.112), P("R7", "2", -4.075), P("R7", "2")]),
    # OUT2 on to JP3: the innermost east lane, then one via down to the
    # jumper pad. It enters lowest of the three, so nothing crosses it.
    ("Net-(JP3-B)", TL, [P("R7", "2"), P("R7", "2", 1.3), (25.7, 48.3)]),
    ("Net-(JP3-B)", BL, [(25.7, 48.3), (24.8, 49.2), (24.8, 50.44)]),

    # ---- VREF: the left outboard lane, from R8's return up to the divider --
    # A DC bus down the lane at x 13.9 with a stub to each of the four
    # returns; no via in it anywhere along the amplifier. Its north end
    # continues up the lane between the thumb row and the antenna keep-out
    # (x 12.45: 0.46 mm from the pads, 0.34 from the keep-out) to one via at
    # the hand end, where JP6 is.
    ("VREF", TL, [P("R8", "2"), P("R8", "2", -1.6), (22.8, 41.6), (13.9, 41.6),
                  P("R6", "2", -1.7, 1.7), P("R6", "2")]),
    ("VREF", TL, [P("R6", "2"), P("R6", "2", -1.7, -1.7), P("R5", "2", -1.7, 1.7), P("R5", "2")]),
    ("VREF", TL, [P("R5", "2"), P("R5", "2", -0.9), P("R5", "2", -1.7, -0.8),
                  (13.9, 26.15), (15.6, 24.45)]),
    ("VREF", TL, [(15.6, 24.45), (X_VREF_N, 21.3), (X_VREF_N, 11.05)]),
    ("VREF", BL, [(X_VREF_N, 11.05), (9.2, 11.05)]),
    ("VREF", BL, [(9.2, 11.05), (7.25, 11.05), (6.5, 10.3)]),

    # ---- the VREF divider at the hand end: R10, R11, JP6 and C4 ------------
    # C4's + lead is through-hole, so the divider net is four short hops on the
    # bottom face and R10's supply pad is fed straight off the run to C5.
    ("Net-(JP6-A)", BL, [P("R10", "2"), P("C4", "1"), (10.75, 7.65), P("R11", "1")]),
    ("Net-(JP6-A)", BL, [(11.5, 8.4), (11.5, 8.65), (10.4, 9.75), (9.2, 9.75)]),

    # ---- ADC0: C2 to pin 31 up the inner lane, and down to JP1 -------------
    ("/ADC0", TL, [(15.6, 45.538), (13.2, 45.538), (13.2, 37.74), pico_pin(31)]),
    ("/ADC0", TL, [(15.6, 45.538), (20.85, 45.538), (20.85, 52.5)]),
    ("/ADC0", BL, [(20.85, 52.5), (20.85, 54.0)]),
    ("/ADC0", BL, [(20.85, 54.0), (20.85, 56.75), (19.6, 58.0)]),

    # ---- AFE supply: C3 and pin 8, the lane down to JP4, and north to C5 ---
    ("/AFE_3V3", TL, [(20.975, 34.112), (24.4, 34.163)]),
    # South, to JP4: the middle east lane, then under the elbow block on the
    # bottom face at y 51.7 - the band between the jumper pads at 50.44 and
    # ADC0's via at 52.5 - and up into JP4's pad on its axis.
    ("/AFE_3V3", TL, [(24.4, 34.163), (26.5, 34.163), (26.5, 48.8)]),
    ("/AFE_3V3", BL, [(26.5, 48.8), (26.5, 51.2), (26.0, 51.7), (17.65, 51.7),
                      (17.15, 51.2), (17.15, 50.44)]),
    # North, to C5 and the divider, at the hand end. Past the antenna keep-out
    # (x 27.11) and before the Pico's pads (28.1) there is one lane, and on
    # the top face RX_IN crosses it; so this runs on the bottom face, which is
    # free the whole way (the pocket has no pour) into C5's through-hole +
    # lead, and on between SW2's pad rows to R10.
    ("/AFE_3V3", BL, [(26.5, 34.163), (27.5, 33.163), P("TP8", "1"), (27.5, 5.45), P("C5", "1")]),
    ("/AFE_3V3", BL, [P("C5", "1"), (26.25, 5.0), (15.3, 5.0), (15.3, 2.7),
                      P("R10", "1", 1.925), P("R10", "1")]),

    # ---- +3V3: pin 36 straight into JP4, and one branch to R15 -------------
    # Pin 36 is on y 50.44 and so is JP4's pad: 4.7 mm of straight track. The
    # branch to R15 leaves the pad north, runs the band between the pocket and
    # the jumper pads, and crosses the little-finger row between pins 6 and 7.
    ("+3V3", BL, [pico_pin(36), (15.85, 50.44)]),
    ("+3V3", BL, [(15.85, 50.44), (15.85, 47.6), (16.25, 47.2), (26.8, 47.2),
                  (27.37, Y_3V3_LF), P("R15", "1")]),

    # ---- the elbow jumpers ------------------------------------------------
    ("Net-(JP3-C)", TL, [(24.4, 46.95), (23.5, 47.85), (23.5, 48.4)]),
    ("Net-(JP3-C)", BL, [(23.5, 48.4), (23.5, 50.44)]),
    ("Net-(JP1-A)", TL, [(24.4, 50.05), (24.4, 53.0), (23.4, 54.0)]),
    ("Net-(JP1-A)", BL, [(23.4, 54.0), (22.15, 54.0)]),

    # ---- haptic ------------------------------------------------------------
    # The gate chain, top face. GP28 leaves pin 34 into R16 and goes south down
    # the one lane between the thumb pin row and J6's holes that the bottom
    # face cannot offer: on the bottom that lane is 1.23 mm wide and VSYS, at
    # 0.5 mm, already has it. Two 0.25 mm tracks would not fit beside it.
    ("/MOT_DRV", TL, [pico_pin(34), (12.35, 45.36), (13.5875, 46.5975), P("R16", "1")]),
    # U1 pin 33 is AGND and used to reach the electrode through the 0.96 mm pour
    # sliver between the Pico's thumb pads and the ADC0 lane. MOT_DRV leaving
    # pin 34 crosses that sliver - 0.25 mm of track plus two 0.2 mm clearances
    # is 0.65 of it - so AGND gets an explicit tie instead: west on the bottom
    # face, out of the cell pocket, into the bottom pour. That is a better
    # ground than the sliver was.
    ("GND", BL, [pico_pin(33), (9.0, 42.82)]),
    # One straight line from R16 through R17's pad and on to the via: the
    # pull-down is in the middle of the run, so there is no branch to make an
    # angle at.
    ("Net-(Q1-G)", TL, [P("R16", "2"), P("R17", "1"), (13.5875, 57.8), (12.8, 58.5875)]),
    ("Net-(Q1-G)", BL, [(12.8, 58.5875), (12.8, 60.5), (13.25, 60.95), P("Q1", "1")]),
    # VSYS: two millimetres from pin 39 into J6's supply hole, then on to D3's
    # cathode. The motor's current never leaves this corner of the board.
    ("VSYS", BL, [pico_pin(39), (12.6, 56.58), (12.6, 56.24), P("J6", "2")]),
    ("VSYS", TL, [P("J6", "2"), (16.19, 56.24), P("D3", "1")]),
    # MOT_SW: J6's switched hole east and down into Q1's drain, and D3's anode
    # onto the same pad through the one via in the flyback loop.
    ("/MOT_SW", BL, [P("J6", "1"), (16.0, 53.7), (16.5, 54.2), (16.5, 60.0), P("Q1", "3")]),
    ("/MOT_SW", TL, [P("D3", "2"), (16.9, 61.15)]),
    ("/MOT_SW", BL, [(16.9, 61.15), (16.5, 60.75), (16.5, 60.0)]),
]

VIAS = [
    ("Net-(JP7-B)", 31.65, 20.9),
    # OUT1's corridor: down at the east lane, up at C1, and up again at x 22.2
    # for the branch to JP3
    ("/OUT1", 27.2, 43.362), ("/OUT1", 17.2, 43.362), ("/OUT1", 21.8, 43.362), ("/OUT1", 21.8, 48.6),
    ("Net-(JP3-C)", 23.5, 48.4),
    ("Net-(JP3-B)", 25.7, 48.3),
    ("Net-(JP1-A)", 23.4, 54.0),
    ("/ADC0", 20.85, 52.5),
    ("VREF", X_VREF_N, 11.05),
    # AFE_3V3: the lane start doubles as the drop to the bottom face for the
    # run north; the other is the end of the lane, under the elbow block to JP4
    ("/AFE_3V3", 26.5, 34.163), ("/AFE_3V3", 26.5, 48.8),
    ("GND", 9.0, 42.82),
    ("Net-(Q1-G)", 12.8, 58.5875),
    ("/MOT_SW", 16.9, 61.15),
    # D1 became SMD (SMB) when it swapped from a THT diode; its three approach
    # tracks used to end on the THT pad itself and now go via-to-F.Cu at what
    # used to be their last 45-degree bend, so the angle there is unchanged.
    ("/SW_OUT", 4.5, 37.42), ("/D1_K", 3.35, 28.65), ("/D1_K", 2.8, 31.5),
    # The two pours are one net and have to be stitched, or DRC reports them
    # unconnected. Four, all outside the cell pocket and the antenna keep-out.
    ("GND", 5.0, 11.7), ("GND", 31.0, 7.0), ("GND", 2.0, 53.0), ("GND", 7.0, 33.0),
]

# --------------------------------------------------------------------------
# Text. (layer, x, y, text, rotation) - 0.8 mm, the fab minimum.
# --------------------------------------------------------------------------
TEXTS = [
    ("F.SilkS", 34.3, 10.6, "HANDOFF", 0),
    ("B.SilkS", 38.8, 21.0, "HANDOFF  rev A  2026-09", 90),
    # test pad names: the point of a test pad
    ("F.SilkS", 23.0, 21.2, "GND", 0), ("B.SilkS", 19.6, 59.7, "ADC0", 0),
    ("B.SilkS", 23.5, 59.7, "OUT1", 0), ("B.SilkS", 6.5, 12.1, "VREF", 0),
    ("B.SilkS", 38.05, 37.7, "PAD", 0),
    ("B.SilkS", 5.5, 24.8, "VSYS", 0), ("B.SilkS", 1.9, 30.1, "D1K", 0),
    ("B.SilkS", 22.5, 60.8, "JP1-8 = OPEN", 0),
    # J6 takes the motor leads and has no silk of its own (NO_SILK_REF)
    ("B.SilkS", 13.3, 51.4, "MOT", 0),
    # J3's pin order, the LED's lead order, beside the housing
    ("F.SilkS", 6.9, 14.94, "R", 0), ("F.SilkS", 6.9, 17.44, "K", 0),
    ("F.SilkS", 6.9, 19.94, "G", 0), ("F.SilkS", 6.9, 22.44, "B", 0),
    # J1 / J5 polarity and J2's pins, beside the housings on the wall side.
    # The square pad is pin 1 in every case; these say what pin 1 is.
    ("F.SilkS", 38.4, 16.75, "PAD", 90),
    ("F.SilkS", 38.4, 25.75, "-", 90), ("F.SilkS", 38.4, 28.25, "+", 90),
    ("F.SilkS", 38.4, 34.75, "-", 90), ("F.SilkS", 38.4, 37.25, "+", 90),
    ("F.SilkS", 38.4, 41.7, "CHG", 90),
]
FAB_NOTE = ("Cmts.User", 20.0, 31.0,
            "HANDOFF rev A - 2 layer, 40 x 62 mm, 1.6 mm FR-4, 1 oz Cu, "
            "HASL lead-free, green mask, white silk. Min track/space 0.25/0.2 "
            "(design), 0.15 (fab floor). Min solder-mask dam 0.1. Drills 0.3 / "
            "0.7 / 0.95 / 1.0 / 1.1 / 1.15 PTH and 3.4 NPTH - see build/drills.md. "
            "H1-H4 3.4 mm unplated, each inside an 8 mm enclosure boss.", 0)

# --------------------------------------------------------------------------
# Netlist
# --------------------------------------------------------------------------
def load_netlist():
    """(ref -> footprint), ((ref, pad) -> net), (ref -> symbol uuid)."""
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
# with the board in hand, and it names jumpers and connectors. The AFE's
# passives do not get silk - there is no room for it between 1206 pads on a
# 5.3 mm pitch - and their references stay on F.Fab where CAD still shows
# them. E1-E10 are NOT here: they sit under the socketed Pico at 2.54 mm pitch
# and are identified by the Pico pin each one sits outboard of. The mounting
# holes are not here either: a 3.4 mm hole in a corner needs no label, and
# the label had nowhere to go but off the board.
SILK_REFS = ("JP", "J", "SW", "U", "D")
SILK_H = 0.8        # PCBWay's minimum legible silk height

# Footprints whose silkscreen OUTLINE is removed (their reference text stays).
# U1's outline is drawn over the whole area the AFE, the breakout pads and the
# bottom-face jumpers deliberately occupy - it is a 21 x 51 box round a module
# that is socketed 8.5 mm in the air, so on this board it is 31 DRC violations
# and no information. The four JST bodies are 6.84 mm deep on a 9 mm pitch, so
# their boxes touch each other; the mounting rings run into the corner radius.
# Every one of these parts is unambiguous from its pads and its reference.
# D1's SMB body outline (with its cathode bar) sits close enough to TP7 that
# the bar clips TP7's solder mask; the part's own moulded band still marks
# polarity for assembly, so the on-board outline is not load-bearing.
SILK_STRIP = {"U1", "J1", "J2", "J3", "J5", "SW1", "H1", "H2", "H3", "H4",
               "J6", "D1"}

# References that would otherwise earn silk by their prefix but have nowhere to
# put it: J6, Q1 and D3 are packed into 6 x 9 mm at the elbow, between JP4's
# silk and the JP1-8 legend, and three more labels there collide with what is
# already printed. Only the TEXT goes to F.Fab/B.Fab - D3's cathode bar and
# Q1's pin-1 mark stay on silk, because a hand-assembled diode needs its band
# - and the "MOT" legend below names J6.
NO_SILK_REF = {"J6", "Q1", "D3"}

# Reference text, placed as (dx, dy[, rotation]) from the footprint's own origin.
REF_AT = {
    "U2": (0.0, 4.6), "SW1": (-3.0, 8.0), "SW2": (3.25, 6.55),
    "C4": (-3.6, 0.0), "C5": (5.25, 0.0), "D1": (-2.6, 3.8, 90),
    "J1": (3.9, 4.65, 90), "J2": (3.9, 3.85, 90), "J5": (3.9, 4.65, 90), "R10": (0.0, 0.0),
    "J3": (0.0, -3.5),
    "JP5": (-2.5, 0.0, 90), "JP6": (0.0, -2.3), "JP7": (0.0, 1.8), "JP2": (0.0, 1.8),
    "JP8": (0.0, -1.8), "JP4": (1.5, 1.9), "JP3": (0.0, 1.9), "JP1": (0.0, 1.8),
    "E3": (0.0, -1.3), "H1": (0.0, 5.0), "H2": (0.0, 5.0), "H3": (0.0, -5.0), "H4": (0.0, -5.0),
    "TP5": (-3.0, 0.0), "TP7": (3.0, 0.0),
    "Q1": (0.0, -2.5), "D3": (-2.6, 0.0), "J6": (-2.4, 1.3),
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
    keep = prefix in SILK_REFS and ref not in NO_SILK_REF
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
        dx, dy, *rot = REF_AT[ref]
        o = fp.GetPosition()
        r.SetPosition(pcbnew.VECTOR2I(o.x + pcbnew.FromMM(dx), o.y + pcbnew.FromMM(dy)))
        r.SetTextAngleDegrees(rot[0] if rot else 0)
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


def mitre(pts, c=MITRE):
    """Chamfer every right-angle corner of a polyline at 45 degrees, c along
    each leg (or half the shorter leg if that is less). Corners that are not
    right angles - the explicit 45s - pass through untouched."""
    out = [pts[0]]
    for a, b, d in zip(pts, pts[1:], pts[2:]):
        (ax, ay), (bx, by), (dx, dy) = a, b, d
        u, v = (ax - bx, ay - by), (dx - bx, dy - by)
        lu, lv = (u[0] ** 2 + u[1] ** 2) ** 0.5, (v[0] ** 2 + v[1] ** 2) ** 0.5
        dot = (u[0] * v[0] + u[1] * v[1]) / (lu * lv)
        if abs(dot) > 1e-6:          # not a right angle
            out.append(b)
            continue
        k = min(c, lu / 2, lv / 2)
        out.append((bx + u[0] / lu * k, by + u[1] / lu * k))
        out.append((bx + v[0] / lv * k, by + v[1] / lv * k))
    out.append(pts[-1])
    return out


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
    # Net classes and the DRC rule set live in handoff.kicad_pro; pcbnew's
    # Save() rewrites that file from the blank project this board was built
    # under, so main() re-applies them after every save.

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
    # Corner arcs, given as start / mid / end. The mid point sits ON the
    # corner (centre + r along the diagonal toward the board corner): given only
    # a centre and two ends, pcbnew drew the 270-degree arc the long way round
    # through the board, so every corner was a near-circle.
    k = r * (1 - 1 / 2 ** 0.5)
    arcs = [((r, 0), (k, k), (0, r)), ((BW, r), (BW - k, k), (BW - r, 0)),
            ((BW - r, BH), (BW - k, BH - k), (BW, BH - r)),
            ((0, BH - r), (k, BH - k), (r, BH))]
    for (sx, sy), (mx, my), (ex, ey) in arcs:
        a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        a.SetArcGeometry(vec(sx, sy), vec(mx, my), vec(ex, ey))
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

    # ---- the AGND sliver, removed ---------------------------------------
    # Between the Pico's thumb pad column and the ADC0 lane the top pour is
    # 0.96 mm wide, walled north by ADC0's run into pin 31 and south by
    # MOT_DRV's out of pin 34. It is 10 mm2 of copper that reaches nothing but
    # U1 pin 33 (AGND), through two thermal spokes, and AGND now has an
    # explicit tie west to the bottom pour instead. Left to fill it is an
    # isolated island and a starved thermal relief; removed, the pad is clean.
    sl = pcbnew.ZONE(board)
    sl.SetIsRuleArea(True)
    sl.SetDoNotAllowZoneFills(True)
    sl.SetDoNotAllowFootprints(False)
    sl.SetDoNotAllowTracks(False)
    sl.SetDoNotAllowVias(False)
    sl.SetDoNotAllowPads(False)
    sls = pcbnew.LSET()
    sls.addLayer(pcbnew.F_Cu)
    sl.SetLayerSet(sls)
    sl.SetZoneName("AGND sliver - no top pour, AGND is tied explicitly")
    so = sl.Outline()
    so.NewOutline()
    for px, py in ((10.1, 37.9), (13.05, 37.9), (13.05, 46.0), (10.1, 46.0)):
        so.Append(mm(px), mm(py))
    board.Add(sl)

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
        inset = EDGE_MARGIN
        for px, py in ((inset, inset), (BW - inset, inset),
                       (BW - inset, BH - inset), (inset, BH - inset)):
            o.Append(mm(px), mm(py))
        board.Add(z)

    # ---- tracks and vias -------------------------------------------------
    def where(pt):
        """Resolve a P() pad reference to board millimetres; pass tuples through."""
        if not isinstance(pt, Pad):
            return pt
        fp = placed.get(pt.ref)
        if fp is None:
            raise SystemExit(f"route names pad {pt.ref}.{pt.num}, which is not placed")
        pad = next((p for p in fp.Pads() if p.GetNumber() == pt.num), None)
        if pad is None:
            raise SystemExit(f"{pt.ref} has no pad {pt.num!r}")
        pos = pad.GetPosition()
        return (round(pcbnew.ToMM(pos.x) + pt.dx, 4), round(pcbnew.ToMM(pos.y) + pt.dy, 4))

    layer_of = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    for route in ROUTES:
        net, layer, pts = route[0], route[1], [where(q) for q in route[2]]
        if net not in netmap:
            raise SystemExit(f"route for unknown net {net!r}")
        w = mm(route[3] if len(route) > 3 else WIDTH.get(net, TRACK))
        for (x1, y1), (x2, y2) in zip(*(lambda p: (p, p[1:]))(mitre(pts))):
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

    # ---- text -------------------------------------------------------------
    for layer, x, y, text, rot in TEXTS + [FAB_NOTE]:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetLayer({"F.SilkS": pcbnew.F_SilkS, "B.SilkS": pcbnew.B_SilkS,
                    "Cmts.User": pcbnew.Cmts_User}[layer])
        t.SetPosition(vec(x, y))
        t.SetTextSize(pcbnew.VECTOR2I(mm(SILK_H), mm(SILK_H)))
        t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(rot)
        if layer.startswith("B."):
            t.SetMirrored(True)
        board.Add(t)

    return board, placed, nets, netmap


def report(board, placed, nets):
    """Nets on every pad; every footprint, and every text, inside the outline."""
    print(f"placed {len(placed)} footprints")
    bad = 0
    for ref, fp in placed.items():
        for pad in fp.Pads():
            want = nets.get((ref, pad.GetNumber()))
            got = pad.GetNetname()
            if want and got != want:
                print(f"  NET {ref}.{pad.GetNumber()} want {want} got {got or '<none>'}")
                bad += 1

    def outside(bb, margin):
        l, rr = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetRight())
        t, b = pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetBottom())
        if l < margin or rr > BW - margin or t < margin or b > BH - margin:
            return f"{l:.2f}..{rr:.2f} x {t:.2f}..{b:.2f}"
        return None

    for ref, fp in placed.items():
        o = outside(fp.GetBoundingBox(False, False), 0)
        if o and ref not in OVERHANG:
            print(f"  OUTSIDE {ref} bbox {o}")
            bad += 1
        for txt in (fp.Reference(), fp.Value()):
            if txt.IsVisible():
                o = outside(txt.GetBoundingBox(), EDGE_MARGIN)
                if o:
                    print(f"  OUTSIDE {ref} {pcbnew.LayerName(txt.GetLayer())} text {o}")
                    bad += 1
    # No pad inside an enclosure boss. The boss is a printed cylinder round each
    # mounting hole that takes a brass insert: anything under it cannot be
    # soldered and cannot be reached. The footprint's keep-out says "pads
    # allowed" because the hole's own NPTH pad sits in the middle of it and
    # would report itself, so the test is here instead. This is what put E3, E4
    # and E10 (Pico pins 1, 2 and 3) 3.3 mm from H4's centre for three sessions.
    for hx, hy in ((x, y) for _r, x, y in HOLES):
        for ref, fp in placed.items():
            if ref.startswith("H"):
                continue
            for pad in fp.Pads():
                pp = pad.GetPosition()
                px, py = pcbnew.ToMM(pp.x), pcbnew.ToMM(pp.y)
                sz = pad.GetSize()
                r = max(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)) / 2
                d = ((px - hx) ** 2 + (py - hy) ** 2) ** 0.5
                if d - r < BOSS_R + 0.25:
                    print(f"  IN BOSS {ref}.{pad.GetNumber()} at {px:.2f},{py:.2f} is "
                          f"{d - r:.2f} mm from the hole at {hx},{hy} (boss radius {BOSS_R})")
                    bad += 1

    for d in board.GetDrawings():
        if d.Type() == pcbnew.PCB_TEXT_T and d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            o = outside(d.GetBoundingBox(), EDGE_MARGIN)
            if o:
                print(f"  OUTSIDE text {d.GetText()!r} {o}")
                bad += 1
    return bad


def shorts(board):
    """Independent of DRC: every pad, track, via and filled pour on each copper
    layer against every other item on a different net, with CLEAR between
    them. Returns the number of pairs that fail."""
    clr = mm(CLEAR)
    items = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    for fp in board.GetFootprints():
        for p in fp.Pads():
            for L in items:
                if p.IsOnLayer(L):
                    items[L].append((f"{fp.GetReference()}.{p.GetNumber()}", p.GetNetname(),
                                     p.GetEffectiveShape(L)))
    for t in board.GetTracks():
        s = t.GetStart()
        kind = "via" if t.Type() == pcbnew.PCB_VIA_T else "track"
        for L in items:
            if t.IsOnLayer(L):
                items[L].append((f"{kind}({pcbnew.ToMM(s.x):.2f},{pcbnew.ToMM(s.y):.2f})",
                                 t.GetNetname(), t.GetEffectiveShape(L)))
    bad = 0
    for L, its in items.items():
        for (a, na, sa), (b, nb, sb) in itertools.combinations(its, 2):
            if na != nb and sa.Collide(sb, clr):
                print(f"  SHORT {pcbnew.LayerName(L)}: {a} [{na}] vs {b} [{nb}]")
                bad += 1
        for z in board.Zones():
            if z.GetIsRuleArea() or not z.IsOnLayer(L):
                continue
            poly = z.GetFilledPolysList(L)
            for a, na, sa in its:
                if na != z.GetNetname() and poly.Collide(sa, clr):
                    print(f"  SHORT {pcbnew.LayerName(L)}: {z.GetZoneName()} vs {a} [{na}]")
                    bad += 1
    n = sum(len(v) for v in items.values())
    print(f"shorts: {bad} pair(s) under {CLEAR} mm among {n} copper items")
    return bad


def summary(board):
    for z in board.Zones():
        if not z.GetIsRuleArea():
            print(f"  {z.GetZoneName()}: {z.GetFilledArea() / 1e12:.1f} mm2")
    vias = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
    drills = sorted({pcbnew.ToMM(t.GetDrillValue()) for t in vias})
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetDrillSize().x:
                drills.append(pcbnew.ToMM(p.GetDrillSize().x))
    widths = sorted({pcbnew.ToMM(t.GetWidth()) for t in board.GetTracks()
                     if t.Type() == pcbnew.PCB_TRACE_T})
    print(f"  {len(vias)} vias; drills {sorted(set(round(d, 2) for d in drills))}; "
          f"track widths {widths}")
    drill_table(board)


def drill_table(board):
    """The fab's tool list: every distinct hole, how many, plated or not, and
    what asks for it. Written to build/drills.md and echoed here, because a
    board hands over with a drill table or it does not hand over."""
    from collections import defaultdict
    tools = defaultdict(lambda: {"n": 0, "plated": set(), "who": set()})
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            e = tools[round(pcbnew.ToMM(t.GetDrillValue()), 2)]
            e["n"] += 1
            e["plated"].add("PTH")
            e["who"].add("via")
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            d = pcbnew.ToMM(pad.GetDrillSize().x)
            if not d:
                continue
            e = tools[round(d, 2)]
            e["n"] += 1
            e["plated"].add("NPTH" if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH else "PTH")
            e["who"].add(fp.GetReference())
    lines = ["| Drill (mm) | Holes | Plating | Used by |", "|---|---|---|---|"]
    for d in sorted(tools):
        e = tools[d]
        who = sorted(e["who"], key=lambda s: (s[0].isdigit(), s))
        if len(who) > 6:
            who = who[:6] + [f"+{len(e['who']) - 6} more"]
        lines.append(f"| {d:.2f} | {e['n']} | {'/'.join(sorted(e['plated']))} | {', '.join(who)} |")
    (BUILD / "drills.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  {len(tools)} drill sizes, {sum(e['n'] for e in tools.values())} holes "
          f"-> build/drills.md")


def plots():
    """Gerbers, drill files and their map, into build/plot - what PCBWay is
    actually sent. Regenerated every run so the fab pack can never be stale."""
    out = BUILD / "plot"
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("*"):
        f.unlink()
    layers = ("F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,"
              "Edge.Cuts,F.Fab,B.Fab,Cmts.User")
    r = run_cli("pcb", "export", "gerbers", "--layers", layers,
                "--no-protel-ext", "--subtract-soldermask", "-o", str(out), str(PCB))
    if r.returncode:
        print("  gerber export failed:", r.stdout.strip() or r.stderr.strip())
        return 1
    r = run_cli("pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th",
                "--generate-map", "--map-format", "gerberx2", "-o", str(out) + "/", str(PCB))
    if r.returncode:
        print("  drill export failed:", r.stdout.strip() or r.stderr.strip())
        return 1
    n = len(list(out.glob("*")))
    print(f"  fab pack: {n} files in build/plot")
    return 0


def write_drc_rules():
    """handoff.kicad_dru - where DRC has to be told about the third dimension,
    which it does not model, plus the two conventions it does not enforce
    unless asked: no right-angle corners, no stub segments."""
    bare = sorted(BREAKOUT) + [f"TP{i}" for i in range(1, 20)]
    # Either side may be a bare pad - a test pad genuinely can sit inside a
    # switch's courtyard - but NEITHER may be a mounting hole. Written without
    # that second half the rule also excused a breakout pad overlapping H4's
    # 8 mm boss, which is how E3, E4 and E10 sat on it undetected.
    holes = [r for r, _x, _y in HOLES]
    es = "({}) && ({})".format(
        " || ".join(f"A.Reference == '{r}' || B.Reference == '{r}'" for r in bare),
        " && ".join(f"A.Reference != '{h}' && B.Reference != '{h}'" for h in holes))
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

# E1-E10 and the test pads are bare through-hole pads with nothing mounted on
# them. They inherit a TestPoint footprint's 2.59 mm courtyard, which is larger
# than the 2.54 mm pin pitch the breakout pads are placed on, so adjacent ones
# "overlap" bodies that do not exist. Copper clearance is unaffected and still
# checked.
(rule "Bare pads have no body"
	(constraint courtyard_clearance (min -5mm))
	(condition "{es}"))

# Layout convention, enforced: every corner is 45 or straight (a right angle
# is two connected segments at 90), and no segment is a stub.
(rule "No right-angle corners"
	(constraint track_angle (min 134)))
(rule "No stub segments"
	(constraint track_segment_length (min 0.15mm)))
"""
    (HW / "handoff.kicad_dru").write_text(text, encoding="utf-8")


def run_cli(*args):
    return subprocess.run([str(KICAD_CLI), *args], capture_output=True, text=True)


def drc():
    """The board's oracle, the way check() is the schematic's: DRC plus parity
    against the sheet. Prints a one-line summary that must read 0 errors."""
    out = BUILD / "pcb_drc.json"
    out.unlink(missing_ok=True)      # never read a stale report
    r = run_cli("pcb", "drc", "--format", "json", "--schematic-parity",
                "-o", str(out), str(PCB))
    if r.returncode or not out.exists():
        raise SystemExit(f"kicad-cli drc failed: {r.stdout} {r.stderr}")
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
    fails += shorts(board)
    summary(board)
    board.Save(str(PCB))
    apply_board_settings(PRO)      # Save() just rewrote it from a blank project
    print(f"wrote {PCB}")
    if "--no-check" in sys.argv:
        return fails
    fails += max(0, drc())
    fails += plots()
    return fails


if __name__ == "__main__":
    sys.exit(main())
