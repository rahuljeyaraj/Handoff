#!/usr/bin/env python3
"""
The QR label for the underside of a band (android-app-decisions.md §2a).

The label carries the band's identity as the radio advertises it: the four
hex digits after "Handoff band " in its BLE name, which ble.c derives from the
last two bytes of pico_get_unique_board_id(). The app scans it, builds a scan
filter for exactly that band, and Android's confirmation shows one device.

The QR holds just the four digits. The sticker is small, so the code is kept
to the smallest QR version at the highest error-correction level — four
alphanumeric characters fit version 1 at level H, where the earlier
"HANDOFF:" prefix (still read by the app) needed version 2 — and the same
four digits are printed big underneath, since they are what a person types
when the sticker will not scan.

For the demo, read the code off the band's USB console — the boot banner
prints a line like

    name "Handoff band 7A3C", label 7A3C

— and pass it here:

    python tools/band_label.py 7A3C                 # -> band-7A3C.png
    python tools/band_label.py 7A3C -o x.svg
    python tools/band_label.py 7A3C --mac 28:CD:C1:0A:1B:2C

The MAC is optional; a production label from a programming fixture can
carry it so the filter can match on both. Needs the `qrcode` package (with
Pillow for PNG output): pip install "qrcode[pil]".
"""

from __future__ import annotations

import argparse
import re
import sys

SUFFIX = re.compile(r"^[0-9A-F]{4}$")
MAC = re.compile(r"^([0-9A-F]{2}:){5}[0-9A-F]{2}$")


def band_code(text: str, mac: str | None) -> str:
    t = text.strip().upper()
    if t.startswith("HANDOFF:"):
        t = t[len("HANDOFF:"):]
    if not SUFFIX.match(t):
        sys.exit(f"band_label: {text!r} is not a four-hex-digit band code")
    if mac is not None:
        m = mac.strip().upper()
        if not MAC.match(m):
            sys.exit(f"band_label: {mac!r} is not a MAC address")
        return f"{t}:{m}"
    return t


def bold_font(size: int):
    """A heavy sans at `size` px from whatever the machine has; the first of
    these that loads. Falls back to Pillow's bitmap font, which is tiny."""
    from PIL import ImageFont
    for name in ("arialbd.ttf", "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf",
                 "Arial Bold.ttf", "Helvetica-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("code", help="the four hex digits")
    ap.add_argument("--mac", default=None, help="the band's BLE address, if known")
    ap.add_argument("-o", "--out", default=None, help="output file (.png or .svg)")
    ap.add_argument("--box", type=int, default=12, help="pixels per QR module")
    a = ap.parse_args()

    code = band_code(a.code, a.mac)
    suffix = code[:4]
    out = a.out or f"band-{suffix}.png"

    try:
        import qrcode
    except ImportError:
        sys.exit("band_label: pip install \"qrcode[pil]\"")

    # Level H: a sticker on the underside of a wristband gets scuffed, and
    # four characters fit version 1 (21 modules) at H regardless.
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_H,
                       box_size=a.box, border=2)
    qr.add_data(code)
    qr.make(fit=True)

    if out.lower().endswith(".svg"):
        import qrcode.image.svg
        img = qr.make_image(image_factory=qrcode.image.svg.SvgPathImage)
        img.save(out)
    else:
        img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
        # The four digits under the code, for the "enter it by hand"
        # fallback. Big — the whole sticker is a centimetre or two across,
        # so the text is sized to the code, not to a page: cap height
        # about a third of the code's width.
        try:
            from PIL import Image, ImageDraw, ImageFont
            w, h = img.size
            font = bold_font(int(w * 0.36))
            draw = ImageDraw.Draw(img)
            box = draw.textbbox((0, 0), suffix, font=font)
            tw, th = box[2] - box[0], box[3] - box[1]
            gap = a.box
            canvas = Image.new("RGB", (w, h + th + 2 * gap), "white")
            canvas.paste(img, (0, 0))
            ImageDraw.Draw(canvas).text(((w - tw) / 2 - box[0], h - box[1]),
                                        suffix, fill="black", font=font)
            img = canvas
        except ImportError:
            pass
        img.save(out)

    print(f"{code} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
