# Generates docs/element14-blog/28-beacon.svg and .png
# The rendezvous, to scale in time. Every band runs the same free-running
# cycle: beacon, settle, listen. It is deaf for the first two, so the band that
# READS a beacon is by construction the one that had not started its own — and
# its own beacon then never goes out. Green is Savithri's band, blue is
# Rohit's, red is what crossed the two bodies, grey is waiting.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN, RED, GREY

MS = 150.0                       # milliseconds drawn across the strip
LANES = "Savithri's band", "Rohit's band"

# (start ms, length ms, title, body, style, solid, dashed)
HER = [
    (0, 28, "Beacon", "28 ms. Its own name, under a checksum.", GREEN, True, False),
    (28, 5, "", "", GREY, False, False),
    (33, 117, "Listen", "51 to 107 ms, drawn fresh every cycle", GREEN, False, False),
]
HIS = [
    (0, 28, "Listening", "Its ears were already open.", BLUE, False, False),
    (28, 24, "Reads it", "", BLUE, False, False),
    (52, 28, "", "The beacon it would have sent, now never sent", GREY, False, True),
    (80, 70, "Sends", "his card.", BLUE, True, False),
]
DEAF = "Deaf while it beacons, and 5 ms after"
CROSS = "Checksum good, and the name is not its own. So the hands have met."
NOTE = ("Every beacon carries a name drawn at random. A band that reads its own name is "
        "hearing itself, and throws it away. Two bands that drew the same name both stand "
        "down and draw again.")

W, M, VG, BR = 1600, 36, 92, 16


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
    nh = need("", NOTE, sw, fs)
    if nh is None or tw(DEAF, fs, True) > sw or tw(CROSS, fs, True) > sw:
        return None
    H = (M + lab_h + lane_h + BR + fs * 1.5 + VG + lab_h + lane_h
         + fs * 1.6 + nh + M)

    f = Fig(W, fs)
    y1 = M + lab_h
    f.text(M, M + fs, LANES[0], GREEN[1], bold=True, anchor="start")
    for (x, w), (_, _, t, b, st, solid, dashed) in zip(widths(HER), HER):
        f.box(x, y1, w, lane_h, t, b, st, solid=solid, dash="10 8" if dashed else None)

    # the deaf window, under her beacon and settle
    dy = y1 + lane_h + 8
    a, b_ = M, M + 33 * px
    f.line([(a, dy), (a, dy + BR), (b_, dy + BR), (b_, dy)], GREY[1], sw=4)
    f.text(b_ + 14, dy + BR - fs * 0.1, DEAF, GREY[2], bold=True, anchor="start")

    y2 = y1 + lane_h + BR + fs * 1.5 + VG + lab_h
    f.text(M, y2 - lab_h + fs, LANES[1], BLUE[1], bold=True, anchor="start")
    for (x, w), (_, _, t, b, st, solid, dashed) in zip(widths(HIS), HIS):
        f.box(x, y2, w, lane_h, t, b, st, solid=solid, dash="10 8" if dashed else None)

    # her beacon crossing into his listening ear
    cx = M + 15 * px
    f.arrow([(cx, y1 + lane_h + 6), (cx, y2 - 6)], RED[1])
    f.text(cx + 20, (y1 + lane_h + y2) / 2 + fs * 0.35, CROSS, RED[2], bold=True,
           anchor="start")

    f.box(M, y2 + lane_h + fs * 1.6, sw, nh, "", NOTE, GREY)
    return f, H


fig, H = solve(build, W)
fig.save("28-beacon", H)
