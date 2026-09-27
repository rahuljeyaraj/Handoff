# Generates docs/element14-blog/06-hearer-sends.svg and .png
# Figure 5.12 zoomed out: the same two lanes, the same colours, solid for a
# band that is talking. The strip is a WINDOW onto a longer timeline: the
# boxes at either edge are cut square, because both bands were running before
# this and are still going after it. The lanes deliberately do not line up -
# each band acts on what it HEARS, so a receiver's box starts a little after
# the sender's. One frame per turn, which is link_sm.c's frames_per_turn = 1.
# Green is Savithri's lane, blue is Rohit's.

from figlib import Fig, balanced, inner, need, solve, BLUE, GREEN, INK, LH

LANES = "Savithri's band", "Rohit's band"

# (start, width, title, body, style, solid, cut), in units along the lane.
# cut is "L" or "R" for a box the window cuts through.
HER = [
    (0.0, 1.6, "", "", GREEN, False, "L"),            # her own cycle, before this
    (1.6, 3.0, "Beacons", "", GREEN, True, None),
    (4.6, 1.4, "", "", GREEN, False, None),           # deaf, then listening
    (6.0, 5.2, "Receives", "his frame 1.", GREEN, False, None),
    (11.2, 0.5, "", "", GREEN, False, None),          # waits for his amplifier
    (11.7, 5.2, "Sends", "her frame 1.", GREEN, True, None),
    (16.9, 1.3, "", "", GREEN, False, None),
    (18.2, 5.2, "Receives", "his frame 2.", GREEN, False, "R"),
]
HIS = [
    (0.0, 2.7, "", "", BLUE, False, "L"),             # already listening
    (2.7, 3.0, "Sees", "her beacon.", BLUE, False, None),
    (5.7, 5.2, "Sends", "his frame 1.", BLUE, True, None),
    (10.9, 1.4, "", "", BLUE, False, None),
    (12.3, 5.2, "Receives", "her frame 1.", BLUE, False, None),
    (17.5, 0.5, "", "", BLUE, False, None),
    (18.0, 5.2, "Sends", "his frame 2.", BLUE, True, "R"),
]

W, M = 1600, 36
UNITS = 21.4                     # where the window closes, mid-frame


def draw(f, x, y, w, h, title, body, style, solid, cut):
    """figlib's box. A box the window cuts through loses the edge it is cut
    on, so it reads as part of a box that carries on past the picture."""
    fill, stroke, tcol = style
    bcol = INK
    if solid:
        fill, tcol, bcol = stroke, "#ffffff", "#ffffff"
    if cut is None:
        f.rect(x, y, w, h, fill, stroke)
    else:
        # Three sides, rounded like every other box; the cut side is left open.
        r, x2, y2 = 10.0, x + w, y + h
        if cut == "L":
            d = (f"M{x:.1f} {y:.1f} L{x2 - r:.1f} {y:.1f} Q{x2:.1f} {y:.1f} "
                 f"{x2:.1f} {y + r:.1f} L{x2:.1f} {y2 - r:.1f} "
                 f"Q{x2:.1f} {y2:.1f} {x2 - r:.1f} {y2:.1f} L{x:.1f} {y2:.1f}")
        else:
            d = (f"M{x2:.1f} {y:.1f} L{x + r:.1f} {y:.1f} Q{x:.1f} {y:.1f} "
                 f"{x:.1f} {y + r:.1f} L{x:.1f} {y2 - r:.1f} "
                 f"Q{x:.1f} {y2:.1f} {x + r:.1f} {y2:.1f} L{x2:.1f} {y2:.1f}")
        f.o.append(f'  <path d="{d} Z" fill="{fill}" stroke="none"/>')
        f.o.append(f'  <path d="{d}" fill="none" stroke="{stroke}" '
                   f'stroke-width="2.5" stroke-linejoin="round"/>')
    rows = ([(title, tcol)] if title else []) + \
           [(ln, bcol) for ln in balanced(body, inner(w), f.fs)]
    top = y + (h - len(rows) * f.fs * LH) / 2
    for i, (s, col) in enumerate(rows):
        f.text(x + w / 2, top + f.fs * (LH / 2 + 0.35) + i * f.fs * LH, s, col,
               bold=(i == 0 and bool(title)))


def build(fs):
    sw = W - 2 * M
    unit = sw / UNITS

    def lay(rows):
        out = []
        for a, wu, _, _, _, _, cut in rows:
            x, w = M + a * unit, wu * unit
            if cut == "R":
                w = W - M - x            # the window closes here
            out.append((x, w))
            if w <= 0:
                return None
        return out

    hs = []
    for rows in (HER, HIS):
        for (x, w), (_, _, t, b, _, _, _) in zip(lay(rows), rows):
            h = need(t, b, w, fs)
            if h is None:
                return None
            hs.append(h)
    lane_h = max(hs)
    lab_h = fs * 1.7

    y1 = M + lab_h
    y2 = y1 + lane_h + fs * 1.2 + lab_h
    H = y2 + lane_h + M

    f = Fig(W, fs)
    for lane, rows, y, col in ((LANES[0], HER, y1, GREEN[1]),
                               (LANES[1], HIS, y2, BLUE[1])):
        f.text(M, y - lab_h + fs, lane, col, bold=True, anchor="start")
        for (x, w), (_, _, t, b, st, solid, cut) in zip(lay(rows), rows):
            draw(f, x, y, w, lane_h, t, b, st, solid, cut)
    return f, H


fig, H = solve(build, W, hi=42)
fig.save("06-hearer-sends", H)
