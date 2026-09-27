# Generates docs/element14-blog/27-which-tone.svg and .png
# The same four bits through a firm grip and a light one, under two-tone FSK.
# Every chip is one tone or the other and the pad is never quiet, so the
# receiver never asks "how loud is that?" — it asks "which of the two bins is
# bigger, in this same window?". Both grips read right, and there is no line
# anywhere in the picture to draw. The two pitches are drawn far wider apart than
# they are -- three cycles a chip against six, where the real ratio is nine to
# ten -- because at the true ratio the two bursts are indistinguishable on the
# page, which is the one thing this figure must not be. The caption says so,
# and it says which row is the sent shape and which is the received one.
#
# The key at the top -- the two of them side by side, centred -- shows a 0 and
# a 1 as the shapes themselves, so the rows below need no letters under the
# chips and the figure needs no sentence over them: the reader matches what
# they see.
#
# One colour a tone, and neither of them red: red is the two bodies everywhere
# else in this post, and a tone belongs to no one -- both bands send both. The
# purple and the teal are figure 09's, where they mean the analogue side and
# the Pico, so neither can be read as a person.

import math

from figlib import Fig, solve, tw, need, INK, GREY

BITS = [1, 0, 1, 1]
GRIPS = [("Firm grip", 0.80), ("Light grip", 0.28)]
READ = "1 0 1 1"
KEY = [("logic 0", 0), ("logic 1", 1)]
LEAD = "Manchester"
RESULT = "Which tone was louder"

W, M, G = 1600, 36, 26
CYC = {"A": 3, "B": 6}           # drawn 1:2; the real ratio is 9:10 (caption)
COL = {"A": "#6a3d9a", "B": "#00838f"}   # one colour a tone, figure 09's


def burst(f, x0, x1, cy, amp, tone, square):
    """Square for what the pin sends -- PIO holds the pin high for half a period
    and low for the other half -- and a round wave for what arrives. The design
    is why: the input RC corners at 321 kHz and the two amplifier stages are
    3 dB down by 580 kHz, so a 200 kHz square loses its third harmonic at
    600 kHz and nearly all of the fifth. What reaches the converter is close to
    the pitch alone."""
    cycles = CYC[tone]
    pts = []
    if square:
        step = (x1 - x0) / (cycles * 2)
        for i in range(cycles * 2):
            lvl = cy + (amp if i % 2 == 0 else -amp)
            pts += [(x0 + i * step, lvl), (x0 + (i + 1) * step, lvl)]
    else:
        n = cycles * 16
        for i in range(n + 1):
            t = i / n
            pts.append((x0 + (x1 - x0) * t, cy + amp * math.sin(2 * math.pi * cycles * t)))
    f.line(pts, COL[tone], sw=3)


def pair(f, x, y, w, h, bit, amp, square=False):
    """One bit: a cell and the two chips, butted together. Nothing between them:
    the pad is never quiet, it only changes pitch."""
    f.rect(x, y, w, h, "#ffffff", "#cfcfcf", sw=2)
    mid = x + w / 2
    for (x0, x1), tone in zip(((x + 10, mid), (mid, x + w - 10)),
                              ("B", "A") if bit else ("A", "B")):
        burst(f, x0, x1, y + h / 2, amp, tone, square)


def build(fs):
    lw = max([tw(g[0], fs, True) for g in GRIPS] + [tw(LEAD, fs, True)]) + 20
    rw = max(tw(RESULT, fs, True), tw(READ, fs, True) + 40) + 30
    cells_w = W - 2 * M - lw - rw - 2 * G
    if cells_w < 700:
        return None
    cw = cells_w / len(BITS)
    x_cells = M + lw + G
    x_res = x_cells + cells_w + G
    rh = need(READ, "Right.", rw, fs)
    if rh is None:
        return None
    row_h = max(rh, fs * 4.2)
    key_h = fs * 2.6
    head_h = fs * 1.6                       # the row of bit numbers
    H = (M + head_h + key_h + G * 2 + head_h + len(GRIPS) * row_h
         + (len(GRIPS) - 1) * G + M)

    f = Fig(W, fs)

    # the key, on the same grid as the rows below: its label in the left gutter
    # where the grips are named, its cells in the first two bit columns, and
    # "logic 0" / "logic 1" over them where the bit numbers go.
    y = M + head_h
    f.text(M, y + key_h / 2 + fs * 0.35, LEAD, INK, bold=True, anchor="start")
    for i, (name, bit) in enumerate(KEY):
        x = x_cells + cw * i
        f.text(x + cw / 2, y - fs * 0.6, name, INK, bold=True)
        pair(f, x, y, cw, key_h, bit, (key_h - 16) / 2, square=True)

    # the four bits, twice
    y += key_h + G * 2 + head_h
    for i, b in enumerate(BITS):
        f.text(x_cells + cw * (i + 0.5), y - fs * 0.6, str(b), INK, bold=True)
    f.text(x_res + rw / 2, y - fs * 0.6, RESULT, INK, bold=True)

    for name, amp in GRIPS:
        f.text(M, y + row_h / 2 + fs * 0.35, name, INK, bold=True, anchor="start")
        for i, b in enumerate(BITS):
            pair(f, x_cells + cw * i, y, cw, row_h, b, (row_h - 16) / 2 * amp)
        f.box(x_res, y, rw, row_h, READ, "Right.", GREY)
        y += row_h + G
    return f, H


fig, H = solve(build, W)
fig.save("27-which-tone", H)
