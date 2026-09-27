# Generates docs/element14-blog/36-layout.png -- section 2, the gallery.
# The two copper faces of the board side by side: top on the left, bottom on
# the right, the bottom mirrored so it reads the way it does with the board
# turned over in your hand. Both are plotted from hardware/handoff.kicad_pcb,
# so the figure cannot drift from the board that was fabbed.

import os
import subprocess

import pymupdf
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(HERE, "..", "..", "hardware")
PCB = os.path.join(HW, "handoff.kicad_pcb")
BUILD = os.path.join(HW, "build")
KICAD = os.environ.get("KICAD_ROOT", r"C:\Program Files\KiCad\10.0")
CLI = os.path.join(KICAD, "bin", "kicad-cli.exe")
OUT = os.path.join(HERE, "36-layout.png")

FACES = {
    "top": ("F.Cu,F.SilkS,Edge.Cuts", False),
    "bot": ("B.Cu,B.SilkS,Edge.Cuts", True),   # mirrored: text reads right way
}
HEIGHT = 1700                 # each face, in pixels
GAP = 90                      # white between the two faces
MARGIN = 40


def face(name):
    layers, mirror = FACES[name]
    svg = os.path.join(BUILD, "fig36_%s.svg" % name)
    cmd = [CLI, "pcb", "export", "svg", "--layers", layers,
           "--page-size-mode", "2", "--exclude-drawing-sheet",
           "--drill-shape-opt", "2"]
    if mirror:
        cmd.append("--mirror")
    cmd += ["-o", svg, PCB]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if not os.path.exists(svg):
        raise SystemExit(r.stdout.strip() or r.stderr.strip())
    page = pymupdf.open(svg)[0]
    zoom = HEIGHT / page.rect.height
    pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom), alpha=False)
    os.remove(svg)
    return Image.frombytes("RGB", (pix.width, pix.height), pix.samples)


def main():
    a, b = face("top"), face("bot")
    w = MARGIN * 2 + a.width + GAP + b.width
    h = MARGIN * 2 + max(a.height, b.height)
    out = Image.new("RGB", (w, h), "white")
    out.paste(a, (MARGIN, MARGIN + (h - 2 * MARGIN - a.height) // 2))
    out.paste(b, (MARGIN + a.width + GAP,
                  MARGIN + (h - 2 * MARGIN - b.height) // 2))
    out.save(OUT)
    print(OUT, out.width, out.height)


if __name__ == "__main__":
    main()
