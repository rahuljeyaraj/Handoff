# Generates docs/element14-blog/35-schematic.png -- section 2, the gallery.
# The whole schematic on one A3 sheet, straight out of KiCad, so the figure in
# the post and the board that was fabbed can never drift apart.
#
# Source: hardware/build/handoff.pdf, which hardware/tools/gen_schematic.py
# writes. Run that first if the sheet has changed.

import os
import subprocess

import pymupdf
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(HERE, "..", "..", "hardware")
PDF = os.path.join(HW, "build", "handoff.pdf")
OUT = os.path.join(HERE, "35-schematic.png")

WIDTH = 2600                  # wide: the reader will want to zoom into it


def main():
    if not os.path.exists(PDF):
        subprocess.run(["python", os.path.join(HW, "tools", "gen_schematic.py")],
                       check=True)
    page = pymupdf.open(PDF)[0]
    zoom = WIDTH / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom))
    im = Image.frombytes("RGB", (pix.width, pix.height), pix.samples)
    im.save(OUT)
    print(OUT, im.width, im.height)


if __name__ == "__main__":
    main()
