#!/usr/bin/env python3
"""
Generate handoff.pretty/SW_Slide_SS-12F23G5.kicad_mod.

Geometry measured on a physical part, 11 Sep 2026 (the vendor drawing's views
were misread the first time round: the body does NOT stand behind the pin row,
it lies on the board in front of it, with the handle sticking out sideways).

Mounted on the board, pins pointing down through it:

              +y  (handle points this way, off the board edge)
                        ____________
        handle ->      |   3.0 sq   |   tip at y = +11.3
                    ___|____________|___
                   |                    |  front face of body, y = +6.3
                   |    metal body      |  8.7 wide, 5.5 tall (z)
    pin row, y=0   |____.___.___.____   |
                   |____________________|  back of body, y = -0.8
     ears at x = +-6.05, pins at x = -3, 0, +3

Measured:  A = pins to front face      6.3 mm
           B = pins to handle tip     11.3 mm  (= A + 5, the "G5" handle)
           C = body height             5.5 mm  (above the board)
           D = body behind the pins    0.8 mm  (6.9 - 6.1 on the drawing)
From the vendor drawing: 12.9 mm overall across the ears, 8.7 mm body,
3.0 mm pin pitch, 3.0 mm square handle, 3.5 mm travel, 0.8 x 0.45 mm legs.

**Place the board edge at y = +6.3** so the whole 5 mm handle is outside the
board and the metal body sits on it, per the owner's decision.
"""
from pathlib import Path
import uuid

HW = Path(__file__).resolve().parent.parent
OUT = HW / "handoff.pretty" / "SW_Slide_SS-12F23G5.kicad_mod"

# --- geometry, mm -----------------------------------------------------------
A = 6.3        # pin row -> front face of the body
B = 11.3       # pin row -> tip of the handle
D = 0.8        # body behind the pin row
BODY_W = 8.7   # across, body only
EARS_W = 12.9  # across, over the mounting ears
PIN_P = 3.0    # pin pitch
EAR_X = 6.05   # ear hole centres
HANDLE = 3.0   # handle, square
TRAVEL = 3.5   # handle travel, along x

SILK = 0.11    # silk outside the body outline
CRTYD = 0.25   # courtyard beyond everything

bw, ew = BODY_W / 2, EARS_W / 2
hh, tw = HANDLE / 2, (HANDLE + TRAVEL) / 2

# pads: (x, y, size, drill) — silk must keep clear of these
PADS = [("1", -PIN_P, 1.7, 1.0), ("2", 0.0, 1.7, 1.0), ("3", PIN_P, 1.7, 1.0),
        ("", -EAR_X, 1.9, 1.15), ("", EAR_X, 1.9, 1.15)]


def uid():
    return str(uuid.uuid4())


def line(x1, y1, x2, y2, layer, width=0.1):
    return f"""	(fp_line
		(start {x1:g} {y1:g})
		(end {x2:g} {y2:g})
		(stroke
			(width {width:g})
			(type solid)
		)
		(layer "{layer}")
		(uuid "{uid()}")
	)"""


def rect(x0, y0, x1, y1, layer, width=0.1):
    return "\n".join([line(x0, y0, x1, y0, layer, width),
                      line(x1, y0, x1, y1, layer, width),
                      line(x1, y1, x0, y1, layer, width),
                      line(x0, y1, x0, y0, layer, width)])


def silk_gaps(y, x_from, x_to, clearance=0.2):
    """The back silk line runs along the pin row: break it around every pad."""
    blocked = sorted((x - s / 2 - clearance, x + s / 2 + clearance) for _, x, s, _ in PADS)
    segs, cur = [], x_from
    for lo, hi in blocked:
        if hi < x_from or lo > x_to:
            continue
        if lo > cur:
            segs.append((cur, min(lo, x_to)))
        cur = max(cur, hi)
    if cur < x_to:
        segs.append((cur, x_to))
    return [line(a, y, b, y, "F.SilkS") for a, b in segs if b - a > 0.15]


body = [
    # --- F.Fab: the part as it really is ---
    rect(-bw, -D, bw, A, "F.Fab", 0.1),                       # body
    rect(-ew, -D, -bw, 0.8, "F.Fab", 0.1),                    # left mounting ear
    rect(bw, -D, ew, 0.8, "F.Fab", 0.1),                      # right ear
    rect(-hh, A, hh, B, "F.Fab", 0.1),                        # handle, centred
    rect(-tw, A, tw, B, "F.Fab", 0.05),                       # handle travel envelope
    # --- F.SilkS: body outline, broken around the pads ---
    # the front face sits ON the board edge, so that line goes just inside it,
    # not outside, where the fab house would clip the silk away
    line(-bw - SILK, -D - SILK, -bw - SILK, A - SILK, "F.SilkS"),
    line(bw + SILK, -D - SILK, bw + SILK, A - SILK, "F.SilkS"),
    line(-bw - SILK, A - SILK, bw + SILK, A - SILK, "F.SilkS"),
    "\n".join(silk_gaps(-D - SILK, -bw - SILK, bw + SILK)),
    # handle on Dwgs.User, not silk: it hangs past the board edge, where silk would be clipped
    line(-hh, A, -hh, B, "Dwgs.User"),
    line(hh, A, hh, B, "Dwgs.User"),
    line(-hh, B, hh, B, "Dwgs.User"),
    # pin 1 marker
    line(-PIN_P - 1.3, -1.6, -PIN_P + 1.3, -1.6, "F.SilkS", 0.15),
    # --- Dwgs.User: where the board edge must go ---
    line(-ew - 2, A, ew + 2, A, "Dwgs.User", 0.15),
    # --- F.CrtYd ---
    rect(-ew - CRTYD, -D - CRTYD, ew + CRTYD, B + CRTYD, "F.CrtYd", 0.05),
]

pads = []
for num, x, size, drill in PADS:
    shape = "rect" if num == "1" else "circle"
    pads.append(f"""	(pad "{num}" thru_hole {shape}
		(at {x:g} 0)
		(size {size:g} {size:g})
		(drill {drill:g})
		(layers "*.Cu" "*.Mask")
		(remove_unused_layers no)
		(uuid "{uid()}")
	)""")

descr = ("SS-12F23G5 1P2T slide switch, right-angle, 5 mm handle. Measured on a real part: "
         f"body {BODY_W} x {A + D} mm lying on the board, {D} mm of it behind the pin row and "
         f"{A} mm in front, {round(5.5, 1)} mm tall; {HANDLE} mm square handle reaching {B} mm from the pin row "
         f"with {TRAVEL} mm of travel; 3 terminals at {PIN_P} mm pitch plus two mounting ears, "
         f"{EARS_W} mm overall. PUT THE BOARD EDGE AT y = +{A} (the line on Dwgs.User): body on the "
         "board, handle outside it.")

text = f"""(footprint "SW_Slide_SS-12F23G5"
	(version 20260206)
	(generator "gen_sw_footprint.py")
	(generator_version "10.0")
	(layer "F.Cu")
	(descr "{descr}")
	(tags "switch SPDT slide SS12F23")
	(property "Reference" "REF**"
		(at 0 -2.9 0)
		(layer "F.SilkS")
		(uuid "{uid()}")
		(effects
			(font
				(size 1 1)
				(thickness 0.15)
			)
		)
	)
	(property "Value" "SW_Slide_SS-12F23G5"
		(at 0 {B + 1.2:g} 0)
		(layer "F.Fab")
		(uuid "{uid()}")
		(effects
			(font
				(size 1 1)
				(thickness 0.15)
			)
		)
	)
	(property "Datasheet" ""
		(at 0 0 0)
		(unlocked yes)
		(layer "F.Fab")
		(hide yes)
		(uuid "{uid()}")
		(effects
			(font
				(size 1.27 1.27)
				(thickness 0.15)
			)
		)
	)
	(property "Description" ""
		(at 0 0 0)
		(unlocked yes)
		(layer "F.Fab")
		(hide yes)
		(uuid "{uid()}")
		(effects
			(font
				(size 1.27 1.27)
				(thickness 0.15)
			)
		)
	)
	(attr through_hole)
	(duplicate_pad_numbers_are_jumpers no)
	(fp_text user "board edge"
		(at 0 {A + 0.8:g} 0)
		(layer "Dwgs.User")
		(uuid "{uid()}")
		(effects
			(font
				(size 0.8 0.8)
				(thickness 0.12)
			)
		)
	)
{chr(10).join(body)}
{chr(10).join(pads)}
)
"""

OUT.write_text(text, encoding="utf-8")
print(f"wrote {OUT}")
print(f"  body  x {-bw:g}..{bw:g}  y {-D:g}..{A:g}   ears to +-{ew:g}   handle tip y {B:g}")
print(f"  board edge at y = +{A:g}; handle protrudes {B - A:g} mm past it")
