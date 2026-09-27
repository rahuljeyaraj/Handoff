# Generates docs/element14-blog/28-beacon.svg and .png
# The rendezvous, to scale in time. Every band runs the same free-running
# cycle: beacon, settle, listen. It is deaf for the first two, so the band that
# READS a beacon is by construction the one that had not started its own - and
# its own beacon then never goes out. Green is Savithri's band, blue is
# Rohit's, red is what crossed the two bodies, grey is waiting.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN, GREY

MS = 150.0                       # milliseconds drawn across the strip
LANES = "Savithri's band", "Rohit's band"

# (start ms, length ms, title, body, style, solid, dashed)
HER = [
    (0, 28, "Beacons", "A nonce, under a checksum.", GREEN, True, False),
    (28, 5, "", "", GREY, False, False),
    (33, 117, "Listens", "A random period, drawn fresh every cycle.", GREEN, False, False),
]
HIS = [
    (0, 52, "Listens",
     "Reads the beacon. Checksum good, nonce not its own: the hands have met.",
     BLUE, False, False),
    (52, 28, "", "The beacon it would have sent, now never sent", GREY, False, True),
    (80, 70, "Sends", "the first frame of his card.", BLUE, True, False),
]
DEAF = "Deaf while it beacons, and for a moment after"

W, M, BR = 1600, 36, 16


def build(fs):
    sw = W - 2 * M
    px = sw / MS

    def widths(rows):
        return [(M + a * px, l * px) for a, l, _, _, _, _, _ in rows]

    hs = []
    for rows in (HER, HIS):
        for (x, w), (_, _, t, b, _, _, _) in zip(widths(rows), rows):
            h = need(t, b, w, fs)
            if h is None:
                return None
            hs.append(h)
    lane_h = max(hs)
    lab_h = fs * 1.7

    # The gap between the lanes is a stack of single lines, so nothing in it
    # can land on a box edge or on the arrow.
    y1 = M + lab_h
    after1 = y1 + lane_h
    br_top = after1 + 10
    deaf_y = br_top + BR + fs * 0.95
    y2 = deaf_y + fs * 0.9 + lab_h
    H = y2 + lane_h + M

    if tw(DEAF, fs, True) > W - 2 * M:
        return None

    f = Fig(W, fs)
    f.text(M, M + fs, LANES[0], GREEN[1], bold=True, anchor="start")
    for (x, w), (_, _, t, b, st, solid, dashed) in zip(widths(HER), HER):
        f.box(x, y1, w, lane_h, t, b, st, solid=solid, dash="10 8" if dashed else None)

    # the deaf window, under her beacon and settle
    a, b_ = M, M + 33 * px
    f.line([(a, br_top), (a, br_top + BR), (b_, br_top + BR), (b_, br_top)], GREY[1], sw=4)
    f.text(M, deaf_y, DEAF, GREY[2], bold=True, anchor="start")

    f.text(M, y2 - lab_h + fs, LANES[1], BLUE[1], bold=True, anchor="start")
    for (x, w), (_, _, t, b, st, solid, dashed) in zip(widths(HIS), HIS):
        f.box(x, y2, w, lane_h, t, b, st, solid=solid, dash="10 8" if dashed else None)

    return f, H


fig, H = solve(build, W)
fig.save("28-beacon", H)
