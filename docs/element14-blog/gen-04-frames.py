# Generates docs/element14-blog/04-frames.svg and .png
# Rohit's card cut by importance, and what three lengths of handshake deliver.
# Top: the frames, in the order they are sent. Below: one row per handshake.

from figlib import Fig, need, solve, tw, BLUE, GREY, GREEN, INK

LABEL = ("#ffffff", "#ffffff", INK)   # an invisible box, so a row label can wrap

FRAMES = [("Frame 1", "Name and phone number"),
          ("Frame 2", "Email"),
          ("Frame 3", "Everything else")]
RESULT_HEAD = "What Savithri gets"

ARRIVED = ("Arrived", "", BLUE)
PARTED = ("", "Hands parted", GREY)
CUT = ("Cut off", "Fails its checksum. Dropped.", GREY)

# shortest handshake first, so what gets through grows down the page
ROWS = [
    ("Too short", [CUT, PARTED, PARTED], ("Nothing. Never something wrong.", GREY)),
    ("A brief touch", [ARRIVED, PARTED, PARTED], ("Rohit, and his number.", GREEN)),
    ("A quick handshake", [ARRIVED, ARRIVED, PARTED], ("Rohit, his number and his email.", GREEN)),
    ("A proper handshake", [ARRIVED, ARRIVED, ARRIVED], ("The whole card.", GREEN)),
]

W, M, G, VG = 1700, 36, 36, 22
LW_SHARE, RW_SHARE = 1.0, 1.25     # label and result columns, against a frame column


def build(fs):
    unit = (W - 2 * M - 4 * G) / (LW_SHARE + 3 + RW_SHARE)
    lw, fw, rw = unit * LW_SHARE, unit, unit * RW_SHARE
    xs = [M, M + lw + G] + [M + lw + G + i * (fw + G) for i in (1, 2)]
    xr = M + lw + G + 3 * (fw + G)

    hh = [need(t, b, fw, fs) for t, b in FRAMES]
    if None in hh or tw(RESULT_HEAD, fs, True) > rw:
        return None
    hrows = []
    for label, cells, (res, _) in ROWS:
        hs = [need("", label, lw, fs, bold=True), need("", res, rw, fs)]
        hs += [need(t, b, fw, fs) for t, b, _ in cells]
        if None in hs:
            return None
        hrows.append(max(hs))
    head_h = max(hh)
    H = M + head_h + VG * 2 + sum(hrows) + VG * (len(ROWS) - 1) + M

    f = Fig(W, fs)
    y = M
    for i, (t, b) in enumerate(FRAMES):
        f.box(xs[i + 1], y, fw, head_h, t, b, BLUE)
        if i < 2:
            f.arrow([(xs[i + 1] + fw + 6, y + head_h / 2), (xs[i + 2] - 6, y + head_h / 2)],
                    BLUE[1])
    f.text(xr + rw / 2, y + head_h / 2 + fs * 0.35, RESULT_HEAD, INK, bold=True)
    y += head_h + VG * 2
    for (label, cells, (res, rst)), h in zip(ROWS, hrows):
        f.box(M, y, lw, h, "", label, LABEL, bold=True)
        for i, (t, b, st) in enumerate(cells):
            f.box(xs[i + 1], y, fw, h, t, b, st, dash="10 8" if st is GREY else None)
        f.box(xr, y, rw, h, "", res, rst)
        y += h + VG
    return f, H


fig, H = solve(build, W)
fig.save("04-frames", H)
