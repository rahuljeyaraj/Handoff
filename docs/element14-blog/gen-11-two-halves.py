# Generates docs/element14-blog/11-two-halves.svg and .png
# Manchester: the same four bits through a firm grip and a light one.
# Each bit is two halves, tone then silence for a 1, silence then tone for a 0.
# A fixed line (dashed) reads the firm grip right and the light grip as all
# zeros; comparing the two halves reads both right. The tone is red: it is the
# signal that crossed the two bodies.

from figlib import Fig, solve, tw, need, INK, RED, GREY

BITS = [1, 0, 1, 1]
GRIPS = [("Firm grip", 0.82, "1 0 1 1", "Right."),
         ("Light grip", 0.30, "0 0 0 0", "Wrong.")]
HALVES = "1 0 1 1"
LINE_AT = 0.5                        # the fixed line, as a share of full swing
HEADS = ("The dashed line reads", "The louder half reads")

W, M, G = 1600, 36, 26
CYCLES = 7                           # zig-zag periods drawn in one half


def burst(f, x0, x1, cy, amp, colour):
    n = CYCLES * 2
    pts = [(x0 + (x1 - x0) * i / n, cy + (amp if i % 2 else -amp)) for i in range(n + 1)]
    f.line(pts, colour, sw=3)


def build(fs):
    lw = max(tw(g[0], fs, True) for g in GRIPS) + 20
    rw = max(tw(h, fs, True) for h in HEADS) + 30
    cells_w = W - 2 * M - lw - 2 * rw - 3 * G
    if cells_w < 600:
        return None
    cw = cells_w / len(BITS)
    x_cells = M + lw + G
    x_res = [x_cells + cells_w + G, x_cells + cells_w + G + rw + G]
    rh = [need(r, b, rw, fs) for _, _, r, b in GRIPS] + [need(HALVES, "Right.", rw, fs)]
    if None in rh:
        return None
    row_h = max(max(rh), fs * 4.2)
    head_h = fs * 2.0
    H = M + head_h + len(GRIPS) * row_h + (len(GRIPS) - 1) * G + M
    f = Fig(W, fs)

    # headings: the bits over the cells, what each reading gives over the results
    for i, b in enumerate(BITS):
        f.text(x_cells + cw * (i + 0.5), M + fs, str(b), INK, bold=True)
    for x, h in zip(x_res, HEADS):
        f.text(x + rw / 2, M + fs, h, INK, bold=True)

    y = M + head_h
    for name, amp, fixed, verdict in GRIPS:
        f.text(M, y + row_h / 2 + fs * 0.35, name, INK, bold=True, anchor="start")
        f.rect(x_cells, y, cells_w, row_h, "#ffffff", "#cfcfcf", sw=2)
        for i in range(1, len(BITS)):
            x = x_cells + cw * i
            f.line([(x, y), (x, y + row_h)], "#cfcfcf", sw=2)
        cy = y + row_h / 2
        full = row_h / 2 - 10
        for i, b in enumerate(BITS):
            left, mid, right = x_cells + cw * i, x_cells + cw * (i + 0.5), x_cells + cw * (i + 1)
            f.line([(mid, y + 6), (mid, y + row_h - 6)], "#e2e2e2", sw=2, dash="4 6")
            on = (left + 12, mid - 12) if b else (mid + 12, right - 12)
            off = (mid + 12, right - 12) if b else (left + 12, mid - 12)
            burst(f, on[0], on[1], cy, full * amp, RED[1])
            f.line([(off[0], cy), (off[1], cy)], RED[1], sw=3)
        f.line([(x_cells, cy - full * LINE_AT), (x_cells + cells_w, cy - full * LINE_AT)],
               INK, sw=3, dash="14 10")
        f.box(x_res[0], y, rw, row_h, fixed, verdict, GREY,
              dash="10 8" if verdict == "Wrong." else None)
        f.box(x_res[1], y, rw, row_h, HALVES, "Right.", GREY)
        y += row_h + G
    return f, H


fig, H = solve(build, W)
fig.save("11-two-halves", H)
