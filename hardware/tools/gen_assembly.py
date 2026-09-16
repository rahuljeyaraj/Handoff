"""
Assembly drawings for docs/hardware-bringup.md: each face with its fab-layer
references, silk, pads (with numbers) and the outline, the bottom mirrored the
way it is seen when the board is flipped. Written to docs/hardware-bringup/
as SVG and PNG, and committed - the bring-up procedure is read off them.

    python hardware/tools/gen_assembly.py
"""
import os
import subprocess
from pathlib import Path

import pymupdf

HW = Path(__file__).resolve().parent.parent
OUT = HW.parent / "docs" / "hardware-bringup"
KICAD = Path(os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0"))
KICAD_CLI = KICAD / "bin" / "kicad-cli.exe"
PCB = HW / "handoff.kicad_pcb"

FACES = {
    "top":    ("F.Fab,F.SilkS,Edge.Cuts,F.Mask", []),
    "bottom": ("B.Fab,B.SilkS,Edge.Cuts,B.Mask", ["--mirror"]),
}

OUT.mkdir(parents=True, exist_ok=True)
for name, (layers, extra) in FACES.items():
    svg = OUT / (name + ".svg")
    subprocess.run([str(KICAD_CLI), "pcb", "export", "svg", "--mode-single",
                    "--page-size-mode", "2", "--exclude-drawing-sheet",
                    "--sketch-pads-on-fab-layers",
                    "--crossout-DNP-footprints-on-fab-layers",
                    "--layers", layers, *extra, "-o", str(svg), str(PCB)],
                   check=True)
    page = pymupdf.open(str(svg))[0]
    pix = page.get_pixmap(matrix=pymupdf.Matrix(6, 6))
    pix.save(str(svg.with_suffix(".png")))
    print(name, pix.width, "x", pix.height)
