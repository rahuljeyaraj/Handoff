# Generates docs/element14-blog/06-hearer-sends.svg and .png
# 09:14 at the front desk, as a timeline. Savithri's band shouted first, so
# Rohit's band heard it and sends first; hers receives. Then the turn passes.
# Green is Savithri's lane, blue is Rohit's, grey is waiting. Solid is a band
# talking, light is a band listening. Each arrow is the colour of its sender.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN, GREY

LABEL = ("#ffffff", "#ffffff", INK)   # an invisible box, so a lane label can wrap

LANES = [
    (("Savithri's band", "Shouted, so it receives first."),
     [("Shouts", "“I am here”.", GREEN, True),
      ("Recovers", "then listens.", GREY, False),
      ("Receives", "Rohit's card arrives.", GREEN, False),
      ("Sends", "her own card.", GREEN, True)]),
    (("Rohit's band", "Heard, so it sends first."),
     [("Hears it", "The hands have met.", BLUE, False),
      ("Waits", "for her shout to end.", GREY, False),
      ("Sends", "his card.", BLUE, True),
      ("Receives", "Savithri's card arrives.", BLUE, False)]),
]
TOP = "Hands meet"
# per step: which way something crosses between the lanes (1 down, -1 up, 0 none)
# and whose colour it is
CROSS = [(1, GREEN), (0, None), (-1, BLUE), (1, GREEN)]

W, M, SG, VG = 1600, 36, 14, 110
LW_SHARE = 1.1


def build(fs):
    unit = (W - 2 * M - 30 - 3 * SG) / (LW_SHARE + 4)
    lw = unit * LW_SHARE
    xs = [M + lw + 30 + i * (unit + SG) for i in range(4)]
    hs = []
    for (lt, lb), steps in LANES:
        h = [need(lt, lb, lw, fs)] + [need(t, b, unit, fs) for t, b, _, _ in steps]
        if None in h:
            return None
        hs.append(max(h))
    top_h = fs * 1.9
    H = M + top_h + hs[0] + VG + hs[1] + M
    f = Fig(W, fs)

    # the moment the hands meet, and time running on from it
    f.text(xs[0], M + fs, TOP, INK, bold=True, anchor="start")
    ax = xs[0] + tw(TOP, fs, True) + 30
    f.arrow([(ax, M + fs * 0.65), (W - M, M + fs * 0.65)], "#9a9a9a")

    ys = [M + top_h, M + top_h + hs[0] + VG]
    for ((lt, lb), steps), y, h in zip(LANES, ys, hs):
        f.box(M, y, lw, h, lt, lb, LABEL)
        for x, (t, b, st, solid) in zip(xs, steps):
            f.box(x, y, unit, h, t, b, st, solid=solid)
    for x, (d, st) in zip(xs, CROSS):
        if d == 0:
            continue
        cx = x + unit / 2
        a, b = ys[0] + hs[0] + 8, ys[1] - 8
        f.arrow([(cx, a), (cx, b)] if d > 0 else [(cx, b), (cx, a)], st[1])
    return f, H


fig, H = solve(build, W)
fig.save("06-hearer-sends", H)
