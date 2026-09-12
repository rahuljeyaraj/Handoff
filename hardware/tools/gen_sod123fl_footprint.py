#!/usr/bin/env python3
"""
Generate handoff.pretty/D_SOD-123FL.kicad_mod, and prove it.

KiCad 10 has no SOD-123FL land pattern. D_SOD-123, D_SOD-123F, D_SOD-128 and
Nexperia_CFP3_SOD-123W are all the wrong size for it: the outline drawing for
the PMEG3020ER-TP (Tech Public - NOT the Nexperia part of the same number,
which is SOD-123W) puts the terminals anywhere from 1.25 to 1.95 mm from the
body centre across the tolerance band, and none of those four covers that
with a fillet.

From the outline drawing, mm:

    A  body width          1.5 - 2.0
    B  tip to tip          3.4 - 3.9
    C  terminal width      0.7 - 1.2
    D  body length         2.5 - 2.9
    H  height              0.95 - 1.35
    L  terminal length     0.35 - 0.9

Pin 1 = cathode (the marking bar), at -x, the same as every KiCad D_* footprint,
so the symbol's pin map does not change. Pin 2 = anode.

    python hardware/tools/gen_sod123fl_footprint.py              # write it
    "C:/Program Files/KiCad/10.0/bin/python.exe" .../gen_sod123fl_footprint.py --prove
                     # also put it on build/fp_test.kicad_pcb, run DRC, render
"""
import os
import subprocess
import sys
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
HW = HERE.parent
NAME = "D_SOD-123FL"
OUT = HW / "handoff.pretty" / f"{NAME}.kicad_mod"
KICAD = Path(os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0"))
KICAD_CLI = KICAD / "bin" / "kicad-cli.exe"

# --- geometry, mm -----------------------------------------------------------
PAD_X = 1.65        # pad centres, +-: inner edge 1.05, outer 2.25
PAD_L, PAD_W = 1.2, 1.6   # along the axis x across it
BODY_L, BODY_W = 2.9, 2.0  # D max x A max
TIP = 3.65 / 2      # B nominal, half
TERM_W = 0.95       # C nominal
CRTYD_L, CRTYD_W = 4.9, 2.6

SILK_W = 0.12
SILK_GAP = 0.2      # silk to copper - silk_over_copper is an error on this board
FAB_W = 0.1

bl, bw = BODY_L / 2, BODY_W / 2
cl, cw = CRTYD_L / 2, CRTYD_W / 2
pad_out = PAD_X + PAD_L / 2


def uid():
    return str(uuid.uuid4())


def line(x1, y1, x2, y2, layer, width):
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


def rect(x0, y0, x1, y1, layer, width):
    return "\n".join([line(x0, y0, x1, y0, layer, width), line(x1, y0, x1, y1, layer, width),
                      line(x1, y1, x0, y1, layer, width), line(x0, y1, x0, y0, layer, width)])


def poly(pts, layer, width):
    return "\n".join(line(*a, *b, layer, width) for a, b in zip(pts, pts[1:] + pts[:1]))


# silk: the two long sides just outside the body, and the cathode bar just
# outside pad 1's copper. The bar is what a hand-assembled diode needs.
sy = bw + SILK_GAP - SILK_W / 2 + 0.05          # 1.11: 0.25 clear of the pads' 0.8
bar_x = -(pad_out + SILK_GAP + SILK_W / 2)      # -2.51
silk_end = bl + SILK_W                          # past the anode end of the body

body = [
    # --- F.Fab: body, terminals, and the diode glyph ---
    rect(-bl, -bw, bl, bw, "F.Fab", FAB_W),
    rect(-TIP, -TERM_W / 2, -bl, TERM_W / 2, "F.Fab", FAB_W),
    rect(bl, -TERM_W / 2, TIP, TERM_W / 2, "F.Fab", FAB_W),
    line(-0.75, 0, -0.35, 0, "F.Fab", FAB_W),
    line(-0.35, -0.55, -0.35, 0.55, "F.Fab", FAB_W),          # the cathode bar of the glyph
    poly([(-0.35, 0), (0.25, -0.4), (0.25, 0.4)], "F.Fab", FAB_W),
    line(0.25, 0, 0.75, 0, "F.Fab", FAB_W),
    # --- F.SilkS ---
    line(bar_x, -sy, bar_x, sy, "F.SilkS", SILK_W),
    line(bar_x, -sy, silk_end, -sy, "F.SilkS", SILK_W),
    line(bar_x, sy, silk_end, sy, "F.SilkS", SILK_W),
    # --- F.CrtYd ---
    rect(-cl, -cw, cl, cw, "F.CrtYd", 0.05),
]

pads = []
for num, x in (("1", -PAD_X), ("2", PAD_X)):
    pads.append(f"""	(pad "{num}" smd rect
		(at {x:g} 0)
		(size {PAD_L:g} {PAD_W:g})
		(layers "F.Cu" "F.Mask" "F.Paste")
		(uuid "{uid()}")
	)""")

descr = (f"SOD-123FL, drawn for the PMEG3020ER-TP (Tech Public; not the Nexperia SOD-123W part of the "
         f"same number). Body {BODY_L} x {BODY_W} max, {TIP * 2:g} tip to tip; pads {PAD_L} x {PAD_W} at "
         f"+-{PAD_X} so the terminal lands on the pad anywhere in its 1.25-1.95 mm tolerance band. "
         "Pin 1 = cathode (bar), at -x.")

text = f"""(footprint "{NAME}"
	(version 20260206)
	(generator "gen_sod123fl_footprint.py")
	(generator_version "10.0")
	(layer "F.Cu")
	(descr "{descr}")
	(tags "diode SOD-123FL PMEG3020ER-TP")
	(property "Reference" "REF**"
		(at 0 -2.1 0)
		(layer "F.SilkS")
		(uuid "{uid()}")
		(effects
			(font
				(size 1 1)
				(thickness 0.15)
			)
		)
	)
	(property "Value" "{NAME}"
		(at 0 2.2 0)
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
	(attr smd)
	(duplicate_pad_numbers_are_jumpers no)
{chr(10).join(body)}
{chr(10).join(pads)}
)
"""


def prove():
    """Put the footprint on a small board with a track into each pad and a
    ground pour round it, run DRC, and render it, so the land pattern is
    looked at before the real board is built on it. Needs KiCad's python."""
    import pcbnew
    build = HW / "build"
    build.mkdir(exist_ok=True)
    pcb = build / "fp_test.kicad_pcb"
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    mm = pcbnew.FromMM
    W, H = 16.0, 10.0
    for (x1, y1), (x2, y2) in (((0, 0), (W, 0)), ((W, 0), (W, H)), ((W, H), (0, H)), ((0, H), (0, 0))):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        s.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)
    nets = {}
    for n in ("K", "A", "GND"):
        ni = pcbnew.NETINFO_ITEM(board, n)
        board.Add(ni)
        nets[n] = ni
    fp = pcbnew.FootprintLoad(str(HW / "handoff.pretty"), NAME)
    if fp is None:
        raise SystemExit(f"{NAME} did not load from handoff.pretty")
    board.Add(fp)
    fp.SetPosition(pcbnew.VECTOR2I(mm(W / 2), mm(H / 2)))
    fp.SetReference("D1")
    for pad in fp.Pads():
        pad.SetNet(nets["K" if pad.GetNumber() == "1" else "A"])
    for net, x0, x1 in (("K", W / 2 - PAD_X, 1.5), ("A", W / 2 + PAD_X, W - 1.5)):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(x0), mm(H / 2)))
        t.SetEnd(pcbnew.VECTOR2I(mm(x1), mm(H / 2)))
        t.SetWidth(mm(0.5))
        t.SetLayer(pcbnew.F_Cu)
        t.SetNet(nets[net])
        board.Add(t)
    # a through-hole pad on the end of each track and one in the pour, so DRC
    # has nothing to call dangling or isolated and every line it prints is
    # about the footprint
    tp = pcbnew.FOOTPRINT(board)
    tp.SetReference("TP")
    tp.Reference().SetVisible(False)
    tp.SetPosition(pcbnew.VECTOR2I(0, 0))
    for net, x, y in (("K", 1.5, H / 2), ("A", W - 1.5, H / 2), ("GND", W / 2, H - 1.5)):
        pad = pcbnew.PAD(tp)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        pad.SetSize(pcbnew.VECTOR2I(mm(1.5), mm(1.5)))
        pad.SetDrillSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7)))
        pad.SetLayerSet(pcbnew.PAD.PTHMask())
        pad.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        pad.SetNumber(net)
        tp.Add(pad)
    board.Add(tp)
    for pad in tp.Pads():
        pad.SetNet(nets[pad.GetNumber()])
    z = pcbnew.ZONE(board)
    z.SetLayer(pcbnew.F_Cu)
    z.SetNet(nets["GND"])
    z.SetLocalClearance(mm(0.2))
    z.SetMinThickness(mm(0.2))
    o = z.Outline()
    o.NewOutline()
    for px, py in ((0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)):
        o.Append(mm(px), mm(py))
    board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(str(pcb))
    rep = build / "fp_drc.json"
    rep.unlink(missing_ok=True)
    r = subprocess.run([str(KICAD_CLI), "pcb", "drc", "--format", "json", "--severity-all",
                        "-o", str(rep), str(pcb)], capture_output=True, text=True)
    if r.returncode or not rep.exists():
        raise SystemExit(f"drc failed: {r.stdout} {r.stderr}")
    import json
    d = json.loads(rep.read_text(encoding="utf-8"))
    viol = d.get("violations", []) + d.get("unconnected_items", [])
    for v in viol:
        print(f'  DRC {v["severity"]} {v["type"]}: {v["description"]}')
    print(f"fp_test DRC: {len(viol)} violation(s) -> {pcb}")
    svg = build / "fp_test.svg"
    subprocess.run([str(KICAD_CLI), "pcb", "export", "svg", "--layers",
                    "F.Cu,F.SilkS,F.Fab,F.CrtYd,Edge.Cuts", "--page-size-mode", "2",
                    "--exclude-drawing-sheet", "-o", str(svg), str(pcb)], capture_output=True)
    try:
        import pymupdf
        pix = pymupdf.open(str(svg))[0].get_pixmap(matrix=pymupdf.Matrix(40, 40))
        pix.save(build / "fp_test.png")
        print(f"  rendered {build / 'fp_test.png'} {pix.width}x{pix.height}")
    except ImportError:
        print(f"  (no pymupdf under this python: open {svg} to look at it)")
    return len(viol)


if __name__ == "__main__":
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT}")
    print(f"  pads {PAD_L} x {PAD_W} at +-{PAD_X} (copper x {PAD_X - PAD_L / 2:g}..{pad_out:g}); "
          f"body {BODY_L} x {BODY_W}; courtyard {CRTYD_L} x {CRTYD_W}; cathode bar at x {bar_x:g}")
    if "--prove" in sys.argv:
        sys.exit(prove())
