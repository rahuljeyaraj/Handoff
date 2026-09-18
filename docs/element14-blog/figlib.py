# Shared drawing kit for the element14 blog figures, 02 onward.
# Figure 01 keeps its own copy; the text metrics here match it.
#
# Every figure solves for ONE font size: the largest at which every box fits
# and the whole figure stays inside a 16:9 frame. Rows are only as tall as
# their wordiest box.

import io
import os
import subprocess

FONT = "Segoe UI, Helvetica, Arial, sans-serif"
MONO = "Consolas, Menlo, monospace"

# (fill, stroke, title colour). One meaning each, in every figure, taken from
# figure 01: BLUE is Rohit, GREEN is Savithri, RED is the handshake itself (the
# two bodies), GREY is everything else. Colour says whose a thing is, never
# how important it is.
BLUE = ("#e9f1fc", "#2f5fae", "#2f5fae")
GREEN = ("#e9f4ec", "#2e7d32", "#2e7d32")
RED = ("#fdeceb", "#c0392b", "#c0392b")
GREY = ("#f1f1f1", "#9a9a9a", "#555555")
INK = "#2b2b2b"
MUTED = "#707070"

PADX, PADY = 22, 18
LH = 1.32

NARROW = "iljtfIr.,;:'!|()[]“”‘’"
WIDE = "mwMW@×"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def tw(s, fs, bold=False, mono=False):
    if mono:
        return len(s) * 0.55 * fs
    total = 0.0
    for ch in s:
        if ch == " ":
            total += 0.27
        elif ch in NARROW:
            total += 0.30
        elif ch in WIDE:
            total += 0.86
        elif ch.isupper():
            total += 0.63
        elif ch.isdigit():
            total += 0.55
        else:
            total += 0.52
    return total * fs * (1.06 if bold else 1.0)


def wrap(text, maxw, fs):
    """Break on plain spaces only, so a non-breaking space keeps a phrase whole."""
    lines, cur = [], ""
    for wd in [w for w in text.split(" ") if w]:
        trial = wd if not cur else cur + " " + wd
        if tw(trial, fs) <= maxw or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = wd
    if cur:
        lines.append(cur)
    return lines


def balanced(text, maxw, fs):
    """Same line count, narrowest block, so a paragraph does not go ragged."""
    if not text:
        return []
    n = len(wrap(text, maxw, fs))
    lo, hi = maxw * 0.35, maxw
    for _ in range(18):
        mid = (lo + hi) / 2
        if len(wrap(text, mid, fs)) <= n:
            hi = mid
        else:
            lo = mid
    return wrap(text, hi, fs)


def inner(w):
    return (w - 2 * PADX) * 0.97


def need(title, body, w, fs, bold=False):
    """Height a box of width w needs at size fs, or None if a word cannot fit.
    bold=True sets the body in bold, for a label that has to wrap."""
    if title and tw(title, fs, True) > inner(w):
        return None
    lines = balanced(body, inner(w) / (1.06 if bold else 1), fs)
    if any(tw(ln, fs, bold) > inner(w) for ln in lines):
        return None
    n = (1 if title else 0) + len(lines)
    return n * fs * LH + 2 * PADY


class Fig:
    def __init__(self, w, fs):
        self.w, self.fs, self.o = w, fs, []

    def rect(self, x, y, w, h, fill, stroke, sw=2.5, rx=10, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.o.append(f'  <rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                      f'rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, fill=INK, bold=False, italic=False, anchor="middle",
             fs=None, mono=False):
        fs = fs or self.fs
        b = ' font-weight="700"' if bold else ""
        i = ' font-style="italic"' if italic else ""
        f = f' font-family="{MONO}"' if mono else ""
        self.o.append(f'  <text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
                      f'font-size="{fs}" fill="{fill}"{b}{i}{f}>{esc(s)}</text>')

    def box(self, x, y, w, h, title, body, style, solid=False, dash=None, bold=False):
        fill, stroke, tcol = style
        bcol = tcol if bold else INK
        if solid:
            fill, tcol, bcol = stroke, "#ffffff", "#ffffff"
        self.rect(x, y, w, h, fill, stroke, dash=dash)
        lines = balanced(body, inner(w) / (1.06 if bold else 1), self.fs)
        rows = ([(title, tcol, True)] if title else []) + [(ln, bcol, bold) for ln in lines]
        top = y + (h - len(rows) * self.fs * LH) / 2
        for i, (s, col, bold) in enumerate(rows):
            self.text(x + w / 2, top + self.fs * (LH / 2 + 0.35) + i * self.fs * LH,
                      s, col, bold=bold)

    def line(self, pts, colour, sw=4, dash=None):
        d = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts)
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.o.append(f'  <path d="{d}" stroke="{colour}" stroke-width="{sw}" '
                      f'fill="none" stroke-linecap="round" stroke-linejoin="round"{da}/>')

    def arrow(self, pts, colour, sw=4, head=16, dash=None):
        """Polyline with a head on the last point."""
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        dx, dy = x2 - x1, y2 - y1
        n = (dx * dx + dy * dy) ** 0.5
        ux, uy = dx / n, dy / n
        bx, by = x2 - ux * head, y2 - uy * head
        self.line(pts[:-1] + [(bx, by)], colour, sw, dash)
        px, py = -uy * head * 0.62, ux * head * 0.62
        self.o.append(f'  <path d="M{bx + px:.1f} {by + py:.1f} L{bx - px:.1f} {by - py:.1f} '
                      f'L{x2:.1f} {y2:.1f} Z" fill="{colour}"/>')

    def save(self, name, h):
        here = os.path.dirname(os.path.abspath(__file__))
        svg = os.path.join(here, name + ".svg")
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{h:.0f}" '
                f'viewBox="0 0 {self.w} {h:.0f}" font-family="{FONT}">\n'
                f'  <rect width="{self.w}" height="{h:.0f}" fill="#ffffff"/>\n')
        with io.open(svg, "w", encoding="utf-8") as f:
            f.write(head + "\n".join(self.o) + "\n</svg>\n")
        png = svg[:-4] + ".png"
        subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=1.6", f"--window-size={self.w},{h:.0f}",
                        "--default-background-color=ffffff", f"--screenshot={png}",
                        "file:///" + svg.replace("\\", "/")],
                       check=True, capture_output=True)
        print(f"wrote {name}  font {self.fs}px  {self.w}x{h:.0f}")


def solve(build, w, hi=56, lo=14):
    """Largest font at which build() fits a 16:9 frame of width w."""
    for fs in range(hi, lo - 1, -1):
        r = build(fs)
        if r is not None and r[1] <= w * 9 / 16:
            return r
    raise SystemExit("no font size fits")
