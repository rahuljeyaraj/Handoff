"""Render build/handoff.pdf to build/sheet.png (and optional crops) for eyeballing."""
import sys, pymupdf
from pathlib import Path
HW = Path(__file__).resolve().parent.parent
doc = pymupdf.open(HW / "build" / "handoff.pdf")
page = doc[0]
zoom = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom))
pix.save(HW / "build" / "sheet.png")
print(pix.width, pix.height)
