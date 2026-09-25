# Generates docs/element14-blog/27-which-tone.svg and .png
# The same four bits through a firm grip and a light one, under two-tone FSK.
# Every chip is one tone or the other and the pad is never quiet, so the
# receiver never asks "how loud is that?" — it asks "which of the two bins is
# bigger, in this same window?". Both grips read right, and there is no line
# anywhere in the picture to draw. Tone A is nine cycles a chip and tone B ten,
# which is the real 180:200 ratio. Red is the signal that crossed the bodies.

from figlib import Fig, solve, tw, need, INK, RED, GREY

BITS = [1, 0, 1, 1]
GRIPS = [("Firm grip", 0.80), ("Light grip", 0.28)]
READ = "1 0 1 1"
HEAD = "Manchester: a 1 is tone B then tone A. A 0 is the other way about."
RESULT = "Which tone was louder"

W, M, G = 1600, 36, 26
CYC = {"A": 9, "B": 10}          # cycles drawn in one chip: the real 180:200


def burst(f, x0, x1, cy, amp, cycles):
    n = cycles * 2
    pts = [(x0 + (x1 - x0) * i / n, cy + (amp if i % 2 else -amp)) for i in range(n + 1)]
    f.line(pts, RED[1], sw=3)


def build(fs):
    lw = max(tw(g[0], fs, True) for g in GRIPS) + 20
    rw = max(tw(RESULT, fs, True), tw(READ, fs, True) + 40) + 30
    cells_w = W - 2 * M - lw - rw - 2 * G
    if cells_w < 700:
        return None
    cw = cells_w / len(BITS)
    x_cells = M + lw + G
    x_res = x_cells + cells_w + G
    rh = need(READ, "Right.", rw, fs)
    if rh is None or tw(HEAD, fs, True) > W - 2 * M:
        return None
    row_h = max(rh, fs * 5.0)
    head_h = fs * 3.4
    H = M + head_h + len(GRIPS) * row_h + (len(GRIPS) - 1) * G + M
    f = Fig(W, fs)

    f.text(M, M + fs, HEAD, INK, bold=True, anchor="start")
    for i, b in enumerate(BITS):
        f.text(x_cells + cw * (i + 0.5), M + head_h - fs * 0.6, str(b), INK, bold=True)
    f.text(x_res + rw / 2, M + head_h - fs * 0.6, RESULT, INK, bold=True)

    y = M + head_h
    for name, amp in GRIPS:
        f.text(M, y + row_h / 2 + fs * 0.35, name, INK, bold=True, anchor="start")
        f.rect(x_cells, y, cells_w, row_h, "#ffffff", "#cfcfcf", sw=2)
        for i in range(1, len(BITS)):
            x = x_cells + cw * i
            f.line([(x, y), (x, y + row_h)], "#cfcfcf", sw=2)
        cy = y + row_h / 2 - fs * 0.5
        full = (row_h - fs * 2.2) / 2 - 8
        for i, b in enumerate(BITS):
            left = x_cells + cw * i
            mid = x_cells + cw * (i + 0.5)
            right = x_cells + cw * (i + 1)
            f.line([(mid, y + 6), (mid, y + row_h - 6)], "#e2e2e2", sw=2, dash="4 6")
            halves = ("B", "A") if b else ("A", "B")
            for (x0, x1), tone in zip(((left + 10, mid - 10), (mid + 10, right - 10)), halves):
                burst(f, x0, x1, cy, full * amp, CYC[tone])
                f.text((x0 + x1) / 2, y + row_h - fs * 0.55, tone, GREY[2], bold=True)
        y_mid = y
        f.box(x_res, y_mid, rw, row_h, READ, "Right.", GREY)
        y += row_h + G
    return f, H


fig, H = solve(build, W)
fig.save("27-which-tone", H)
