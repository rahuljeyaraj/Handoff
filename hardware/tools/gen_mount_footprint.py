#!/usr/bin/env python3
"""
Generate handoff.pretty/MountingHole_3.4mm_M3_Boss8mm.kicad_mod.

Why a project footprint instead of a stock one: KiCad 10 ships MountingHole
3.2, 3.5 and 3.7 mm and nothing at 3.4, and none of them carry a keep-out.

3.4 mm is the ISO 273 "medium" clearance for M3 (3.2 is "close" / fine fit).
The board is screwed into a printed boss that takes a 5 mm brass insert, so the
screw is never a locating feature - the 0.2 mm extra swallows the boss's
position tolerance instead of fighting it.

The 8 mm keep-out is the boss's own footprint. It rides in the footprint rather
than being drawn on the board so DRC enforces it wherever the hole is placed,
and so it cannot be forgotten on the fourth corner.

Unplated, no copper, no annular ring: a metal screw or standoff must not become
a second ground-plane connection (design 8.4 - exactly one).
"""
from pathlib import Path
import math
import uuid

HW = Path(__file__).resolve().parent.parent
OUT = HW / "handoff.pretty" / "MountingHole_3.4mm_M3_Boss8mm.kicad_mod"

DRILL = 3.4        # M3 clearance, ISO 273 medium
BOSS_D = 8.0       # printed boss / brass insert outer diameter
SILK_D = 4.0       # silk ring just outside the hole
CRTYD = BOSS_D + 0.5
SEGS = 48          # polygon approximation of the keep-out circle

CU_LAYERS = '"F.Cu" "B.Cu"'


def uid():
    return str(uuid.uuid4())


def circle(d, layer, width=0.12):
    return f"""	(fp_circle
		(center 0 0)
		(end {d / 2:g} 0)
		(stroke
			(width {width:g})
			(type solid)
		)
		(fill none)
		(layer "{layer}")
		(uuid "{uid()}")
	)"""


def keepout_polygon():
    pts = []
    for i in range(SEGS):
        a = 2 * math.pi * i / SEGS
        pts.append(f"(xy {BOSS_D / 2 * math.cos(a):.4f} {BOSS_D / 2 * math.sin(a):.4f})")
    rows = ["\t\t\t\t" + " ".join(pts[i:i + 6]) for i in range(0, len(pts), 6)]
    return "\n".join(rows)


descr = (f"M3 mounting hole, {DRILL} mm drill (ISO 273 medium clearance), unplated, no copper. "
         f"Carries a {BOSS_D} mm keep-out for the enclosure boss that takes a 5 mm brass insert. "
         "Deliberately not tied to GND: exactly one ground-plane connection (design 8.4).")

text = f"""(footprint "MountingHole_3.4mm_M3_Boss8mm"
	(version 20260206)
	(generator "gen_mount_footprint.py")
	(generator_version "10.0")
	(layer "F.Cu")
	(descr "{descr}")
	(tags "mounting hole 3.4mm M3 npth keepout boss")
	(property "Reference" "REF**"
		(at 0 -{BOSS_D / 2 + 1:g} 0)
		(layer "F.SilkS")
		(uuid "{uid()}")
		(effects
			(font
				(size 1 1)
				(thickness 0.15)
			)
		)
	)
	(property "Value" "MountingHole_3.4mm_M3_Boss8mm"
		(at 0 {BOSS_D / 2 + 1:g} 0)
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
	(attr exclude_from_pos_files exclude_from_bom allow_missing_courtyard)
	(duplicate_pad_numbers_are_jumpers no)
{circle(SILK_D, "F.SilkS")}
{circle(DRILL, "F.Fab", 0.1)}
{circle(BOSS_D, "Dwgs.User", 0.1)}
{circle(CRTYD, "F.CrtYd", 0.05)}
	(fp_text user "boss {BOSS_D:g}"
		(at 0 -{BOSS_D / 2 + 2.2:g} 0)
		(layer "Dwgs.User")
		(uuid "{uid()}")
		(effects
			(font
				(size 0.6 0.6)
				(thickness 0.1)
			)
		)
	)
	(pad "" np_thru_hole circle
		(at 0 0)
		(size {DRILL:g} {DRILL:g})
		(drill {DRILL:g})
		(layers "F&B.Cu" "*.Mask")
		(remove_unused_layers no)
		(uuid "{uid()}")
	)
	(zone
		(layers {CU_LAYERS})
		(uuid "{uid()}")
		(name "Boss Keep Out")
		(hatch full 0.5)
		(connect_pads
			(clearance 0)
		)
		(min_thickness 0.254)
		(keepout
			(tracks not_allowed)
			(vias not_allowed)
			(pads not_allowed)
			(copperpour not_allowed)
			(footprints allowed)
		)
		(placement
			(enabled no)
			(sheetname "")
		)
		(fill
			(thermal_gap 0.508)
			(thermal_bridge_width 0.508)
			(island_removal_mode 0)
		)
		(polygon
			(pts
{keepout_polygon()}
			)
		)
	)
)
"""

OUT.write_text(text, encoding="utf-8")
print(f"wrote {OUT}")
print(f"  drill {DRILL:g} mm unplated, no copper; {BOSS_D:g} mm keep-out (tracks/vias/pads/pour)")
