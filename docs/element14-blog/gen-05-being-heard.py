# Generates docs/element14-blog/05-being-heard.svg and .png
# Why a band cannot wait to be told, and why being heard is the touch.
# Three rows, each a small experiment with the same two bands:
#   both wait to hear first  -> nobody ever speaks
#   one beacons, apart        -> nothing arrives
#   one beacons, hand in hand -> heard, so the hands have met
# Green is Savithri's, blue is Rohit's, red is the two bodies, grey is nothing.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN, RED, GREY

LABEL = ("#ffffff", "#ffffff", INK)   # an invisible box, so a row label can wrap

HEADS = ("Savithri's band", "Between them", "Rohit's band")
HEAD_COL = (GREEN[1], INK, BLUE[1])

SHOUT = ("Sends a beacon.", GREEN)
# (row label, [three cells as (text, style, dashed)], arrows as (from, to, colour, dashed))
ROWS = [
    ("If both wait to hear first",
     [("Listens, waiting to hear someone.", GREEN, False),
      ("Hands meet. Silence. Nobody ever speaks.", GREY, True),
      ("Listens, waiting to hear someone.", BLUE, False)],
     []),
    ("If one beacons while apart",
     [(*SHOUT, False),
      ("The beacon never arrives.", GREY, True),
      ("Hears nothing. Is anyone there?", BLUE, False)],
     [(0, 1, GREY[1], True)]),
    ("If one beacons while hand in hand",
     [(*SHOUT, False),
      ("The beacon crosses both bodies.", RED, False),
      ("Reads it. So the hands have met.", BLUE, False)],
     [(0, 1, GREEN[1], False), (1, 2, RED[1], False)]),
]

W, M, GX, VG = 1600, 36, 70, 34
LW_SHARE = 1.3


def build(fs):
    unit = (W - 2 * M - 3 * GX) / (LW_SHARE + 3)
    lw = unit * LW_SHARE
    xs = [M + lw + GX + i * (unit + GX) for i in range(3)]
    if any(tw(h, fs, True) > unit for h in HEADS):
        return None
    hs = []
    for label, cells, _ in ROWS:
        h = [need("", label, lw, fs, bold=True)] + [need("", t, unit, fs) for t, _, _ in cells]
        if None in h:
            return None
        hs.append(max(h))
    rh = max(hs)                      # every box the same height, so the rows line up
    head_h = fs * 1.8
    H = M + head_h + rh * len(ROWS) + VG * (len(ROWS) - 1) + M
    f = Fig(W, fs)
    for x, h, c in zip(xs, HEADS, HEAD_COL):
        f.text(x + unit / 2, M + fs, h, c, bold=True)
    y = M + head_h
    for label, cells, arrows in ROWS:
        f.box(M, y, lw, rh, "", label, LABEL, bold=True)
        for x, (t, st, dashed) in zip(xs, cells):
            f.box(x, y, unit, rh, "", t, st, dash="12 9" if dashed else None)
        my = y + rh / 2
        for a, b, c, dashed in arrows:
            f.arrow([(xs[a] + unit + 8, my), (xs[b] - 8, my)], c,
                    dash="10 9" if dashed else None)
        y += rh + VG
    return f, H


fig, H = solve(build, W)
fig.save("05-being-heard", H)
