# Generates docs/element14-blog/07-off-is-not-low.svg and .png
# What a band's own amplifier does when the tone is off: held low, as first
# designed, against let go, as built. Grey dashed is the retired way, blue is
# Rohit's band as it works now. Arrows run left to right, cause to effect.

from figlib import Fig, need, solve, INK, BLUE, GREY

LABEL = ("#ffffff", "#ffffff", INK)   # an invisible box, so a label can wrap

HEADS = ["When the tone is off, the pin is", "So the band's own amplifier is",
         "When the frame ends, it is"]

ROWS = [
    (("Held low", "as first designed"), GREY, [
        ("Held at 0 V", "Every time the tone is off."),
        ("Pulled down", "To the bottom of its range."),
        ("Deaf for 17 ms", "The budget is 1 ms."),
    ]),
    (("Let go", "as built"), BLUE, [
        ("Released", "The pin lets go."),
        ("Left alone", "It stays in the middle."),
        ("Ready at once", "Well inside 1 ms."),
    ]),
]

W, M, G, VG = 1600, 36, 44, 30
LW_SHARE = 0.85


def build(fs):
    unit = (W - 2 * M - 3 * G) / (LW_SHARE + 3)
    lw = unit * LW_SHARE
    xs = [M + lw + G + i * (unit + G) for i in range(3)]

    hh = [need("", h, unit, fs, bold=True) for h in HEADS]
    if None in hh:
        return None
    head_h = max(hh)
    hrows = []
    for (lt, lb), _, cells in ROWS:
        hs = [need(lt, lb, lw, fs)] + [need(t, b, unit, fs) for t, b in cells]
        if None in hs:
            return None
        hrows.append(max(hs))
    H = M + head_h + VG + sum(hrows) + VG * (len(ROWS) - 1) + M

    f = Fig(W, fs)
    y = M
    for x, h in zip(xs, HEADS):
        f.box(x, y, unit, head_h, "", h, LABEL, bold=True)
    y += head_h + VG
    for ((lt, lb), st, cells), h in zip(ROWS, hrows):
        f.box(M, y, lw, h, lt, lb, LABEL)
        dash = "10 8" if st is GREY else None
        for i, (x, (t, b)) in enumerate(zip(xs, cells)):
            f.box(x, y, unit, h, t, b, st, dash=dash)
            if i < 2:
                f.arrow([(x + unit + 6, y + h / 2), (xs[i + 1] - 6, y + h / 2)], st[1])
        y += h + VG
    return f, H


fig, H = solve(build, W)
fig.save("07-off-is-not-low", H)
