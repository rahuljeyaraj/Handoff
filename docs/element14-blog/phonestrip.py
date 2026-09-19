# Shared kit for the app-screenshot strips, figures 14 onward (chapters 2-4).
#
# A strip is the stages of one section side by side, left to right in order:
# each phone screenshot cropped the same (no status bar, no gesture bar), a
# number and a short caption under it, an arrow between phones where the order
# matters, and a ring round the thing tapped next. Blue is Rohit (the taps are
# his); the phone frames are grey. One font size per strip, solved to fit the
# captions, and never above MAX_FS so every strip reads the same.

import base64
import io
import os

from PIL import Image

from figlib import Fig, balanced, tw, INK, BLUE, GREY

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")

SRC_W, SRC_H = 1080, 2412     # the bench phone's screen
TOP, BOTTOM = 120, 2350       # crop: status bar above, gesture bar below
EMBED_W = 720                 # pixels kept per phone inside the SVG

W, M = 1600, 36               # every strip is 1600 wide, like figures 01-12
GAP = 84                      # between phones: room for an arrow
ROWGAP = 44
PER_ROW = 3                   # the phone width is set by three to a row
PW = (W - 2 * M - (PER_ROW - 1) * GAP) / PER_ROW
PH = PW * (BOTTOM - TOP) / SRC_W
MAX_FS, MIN_FS = 38, 18
RING = BLUE[1]
FRAME = GREY[1]


def shot_uri(name):
    """The cropped screenshot as a data URI, so the SVG stands alone."""
    im = Image.open(os.path.join(SHOTS, name + ".png")).convert("RGB")
    im = im.crop((0, TOP, SRC_W, BOTTOM))
    im = im.resize((EMBED_W, round(EMBED_W * im.height / im.width)), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def caption_lines(text, fs, numw):
    lines = balanced(text, PW - numw, fs)
    if any(tw(ln, fs) > PW - numw for ln in lines):
        return None
    return lines


def draw(name, stages, rows=None, arrows=None):
    """stages: list of dicts with shot, caption and optional ring.
    ring = (cx, cy, w, h) in the phone's own pixels (1080x2412), drawn as a
    rounded rectangle; a square ring is drawn as a circle.
    rows: phones per row, e.g. [3, 3]; default one row.
    arrows: set of stage indexes i that get an arrow to stage i+1 (same row);
    default every neighbour in a row."""
    rows = rows or [len(stages)]
    assert sum(rows) == len(stages) and max(rows) <= PER_ROW
    if arrows is None:
        arrows, i = set(), 0
        for n in rows:
            arrows |= set(range(i, i + n - 1))
            i += n

    for fs in range(MAX_FS, MIN_FS - 1, -1):
        numw = fs * 1.7
        caps = [caption_lines(s["caption"], fs, numw) for s in stages]
        if None not in caps and max(len(c) for c in caps) <= 2:
            break
    else:
        raise SystemExit("captions do not fit")

    cap_h = fs * 0.7 + max(len(c) for c in caps) * fs * 1.32
    row_h = PH + cap_h
    H = M + len(rows) * row_h + (len(rows) - 1) * ROWGAP + M * 0.6
    f = Fig(W, fs)
    sx = PW / SRC_W
    f.o.append(f'  <defs><clipPath id="scr"><rect width="{PW:.1f}" height="{PH:.1f}" rx="18"/>'
               f'</clipPath></defs>')

    k = 0
    for r, n in enumerate(rows):
        y = M + r * (row_h + ROWGAP)
        x0 = (W - n * PW - (n - 1) * GAP) / 2
        for j in range(n):
            s = stages[k]
            x = x0 + j * (PW + GAP)
            f.o.append(f'  <g transform="translate({x:.1f} {y:.1f})">'
                       f'<image width="{PW:.1f}" height="{PH:.1f}" clip-path="url(#scr)" '
                       f'preserveAspectRatio="none" href="{shot_uri(s["shot"])}"/></g>')
            f.rect(x, y, PW, PH, "none", FRAME, sw=3, rx=18)
            if s.get("ring"):
                cx, cy, rw, rh = s["ring"]
                rx_ = min(rw, rh) * sx / 2
                f.rect(x + (cx - rw / 2) * sx, y + (cy - TOP - rh / 2) * sx,
                       rw * sx, rh * sx, "none", RING, sw=5, rx=round(rx_, 1))
            # the number in a blue disc, the caption beside it, centred as a block
            lines = caps[k]
            numw = fs * 1.7
            block = numw + max(tw(ln, fs) for ln in lines)
            bx = x + (PW - block) / 2
            ty = y + PH + fs * 0.7
            f.o.append(f'  <circle cx="{bx + fs * 0.68:.1f}" cy="{ty + fs * 0.62:.1f}" '
                       f'r="{fs * 0.68:.1f}" fill="{RING}"/>')
            f.text(bx + fs * 0.68, ty + fs * 0.62 + fs * 0.35, str(k + 1), "#ffffff", bold=True)
            for i, ln in enumerate(lines):
                f.text(bx + numw, ty + fs * 0.97 + i * fs * 1.32, ln, INK, anchor="start")
            if k in arrows:
                ay = y + PH / 2
                f.arrow([(x + PW + 14, ay), (x + PW + GAP - 14, ay)], RING, sw=5, head=18)
            k += 1
    f.save(name, H)
