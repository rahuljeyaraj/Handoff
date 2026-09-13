#!/usr/bin/env python3
"""
The QR label for the underside of a band (android-app-decisions.md §2a).

The label carries the band's identity as the radio advertises it: the four
hex digits after "Handoff " in its BLE name, which ble.c derives from the last
two bytes of pico_get_unique_board_id(). The app scans it, builds a scan
filter for exactly that band, and Android's confirmation shows one device.

For the demo, read the code off the band's USB console — the boot banner
prints a line like

    name "Handoff 7A3C", label HANDOFF:7A3C

— and pass it here:

    python tools/band_label.py 7A3C                 # -> band-7A3C.png
    python tools/band_label.py HANDOFF:7A3C -o x.png
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
        return f"HANDOFF:{t}:{m}"
    return f"HANDOFF:{t}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("code", help="the four hex digits, or HANDOFF:XXXX")
    ap.add_argument("--mac", default=None, help="the band's BLE address, if known")
    ap.add_argument("-o", "--out", default=None, help="output file (.png or .svg)")
    ap.add_argument("--box", type=int, default=12, help="pixels per QR module")
    a = ap.parse_args()

    code = band_code(a.code, a.mac)
    suffix = code.split(":")[1]
    out = a.out or f"band-{suffix}.png"

    try:
        import qrcode
    except ImportError:
        sys.exit("band_label: pip install \"qrcode[pil]\"")

    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_Q,
                       box_size=a.box, border=2)
    qr.add_data(code)
    qr.make(fit=True)

    if out.lower().endswith(".svg"):
        import qrcode.image.svg
        img = qr.make_image(image_factory=qrcode.image.svg.SvgPathImage)
        img.save(out)
    else:
        img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
        # The four digits under the code, for the "enter it by hand" fallback.
        try:
            from PIL import ImageDraw, ImageFont
            w, h = img.size
            strip = 3 * a.box
            from PIL import Image
            canvas = Image.new("RGB", (w, h + strip), "white")
            canvas.paste(img, (0, 0))
            draw = ImageDraw.Draw(canvas)
            try:
                font = ImageFont.truetype("DejaVuSansMono.ttf", 2 * a.box)
            except OSError:
                font = ImageFont.load_default()
            tw = draw.textlength(suffix, font=font)
            draw.text(((w - tw) / 2, h), suffix, fill="black", font=font)
            img = canvas
        except ImportError:
            pass
        img.save(out)

    print(f"{code} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
