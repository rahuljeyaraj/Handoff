"""Render the board to build/pcb_<face>.png for eyeballing placement.

Not part of the build: gen_pcb.py proves the board with DRC and its own
checks, this is only so a human (or a session) can look at it.
"""
import subprocess, sys, os
from pathlib import Path
HW = Path(__file__).resolve().parent.parent
KICAD = Path(os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0"))
CLI = KICAD / "bin" / "kicad-cli.exe"
BUILD = HW / "build"

FACES = {
    "top": "F.Cu,F.SilkS,F.Fab,Edge.Cuts,User.Drawings",
    "bot": "B.Cu,B.SilkS,B.Fab,Edge.Cuts,User.Drawings",
}
face = sys.argv[1] if len(sys.argv) > 1 else "bot"
svg = BUILD / f"pcb_{face}.svg"
r = subprocess.run([str(CLI), "pcb", "export", "svg", "--layers", FACES[face],
                    "--page-size-mode", "2", "--exclude-drawing-sheet",
                    "--black-and-white" if "--bw" in sys.argv else "--drill-shape-opt", "2",
                    "-o", str(svg), str(HW / "handoff.kicad_pcb")],
                   capture_output=True, text=True)
print(r.stdout.strip() or r.stderr.strip())
import pymupdf
doc = pymupdf.open(str(svg))
page = doc[0]
zoom = float(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].replace(".", "").isdigit() else 14.0
pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom))
png = BUILD / f"pcb_{face}.png"
pix.save(png)
print(png, pix.width, pix.height)
