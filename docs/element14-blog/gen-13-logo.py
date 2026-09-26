# Generates docs/element14-blog/13-logo.svg and .png -- section 2.1.
# The sign beside the desk: the handshake mark, and the word Handoff under it.
# The mark is the app's own launcher icon, trimmed to its ink and embedded, so
# the figure and the icon on the phone can never drift apart.

import base64
import io
import os

from PIL import Image

from figlib import Fig

HERE = os.path.dirname(os.path.abspath(__file__))
ICON = os.path.join(HERE, "..", "..", "android", "app", "src", "main", "res",
                    "mipmap-xxxhdpi", "ic_launcher_foreground.png")

INK = "#3e4e82"               # the mark's own blue
W = 340                       # far narrower than the diagrams: it is a sign,
MARK_W = 180                  # the icon is 220 px of ink, so never scale it up
GAP = 22
FS = 46
PAD = 24


def mark_uri():
    """The launcher foreground, trimmed to its ink, as a data URI."""
    im = Image.open(ICON).convert("RGBA")
    im = im.crop(im.split()[-1].getbbox())
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return im, "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


im, uri = mark_uri()
mark_h = MARK_W * im.height / im.width
h = PAD + mark_h + GAP + FS + PAD

f = Fig(W, FS)
f.o.append(f'  <image x="{(W - MARK_W) / 2:.1f}" y="{PAD:.1f}" '
           f'width="{MARK_W:.1f}" height="{mark_h:.1f}" href="{uri}"/>')
f.text(W / 2, PAD + mark_h + GAP + FS * 0.78, "Handoff", INK, bold=True)
f.save("13-logo", h)
