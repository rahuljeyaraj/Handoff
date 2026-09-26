# Generates docs/element14-blog/02-the-loop.svg and .png
# The circuit a handshake makes: out through two bodies, back through the room.
# Two columns, three rows, and the arrows go round as one ring: the top row is
# Rohit's, the bottom row is Savithri's, the middle row is the handshake and
# the room. Blue is Rohit's, green is Savithri's, red is the two bodies, grey
# the room. Every box carries one short line, so every box is the same size.

from figlib import Fig, need, solve, BLUE, GREEN, RED, GREY

S = {
    "rb":   ("Rohit's band", "Switches a pin between two pitches.", BLUE),
    "p1":   ("His coated plate", "Puts the tone onto his skin.", BLUE),
    "room": ("The room", "Floor and walls carry the return.", GREY),
    "wire": ("The wire", "His arm, the handshake, her arm.", RED),
    "sb":   ("Savithri's band", "Scores both pitches, takes the louder.", GREEN),
    "p2":   ("Her coated plate", "Picks the tone up off her skin.", GREEN),
}

ROWS = (("rb", "p1"), ("room", "wire"), ("sb", "p2"))

W, M, GX, VG = 1600, 36, 150, 92


def build(fs):
    cw = (W - 2 * M - GX) / 2
    hs = [need(*S[k][:2], cw, fs) for row in ROWS for k in row]
    if None in hs:
        return None
    bh = max(hs)                      # one height for all six
    H = M + 3 * bh + 2 * VG + M
    f = Fig(W, fs)
    x = [M, M + cw + GX]
    y = [M + i * (bh + VG) for i in range(3)]

    for r, row in enumerate(ROWS):
        for c, k in enumerate(row):
            f.box(x[c], y[r], cw, bh, *S[k])

    # each arrow takes the colour of the box it leaves
    def across(r, colour, right=True):
        m = y[r] + bh / 2
        pts = [(x[0] + cw + 8, m), (x[1] - 8, m)]
        f.arrow(pts if right else pts[::-1], colour)

    def down(c, r, colour):
        cx = x[c] + cw / 2
        f.arrow([(cx, y[r] + bh + 8), (cx, y[r + 1] - 8)], colour)

    def up(c, r, colour):
        cx = x[c] + cw / 2
        f.arrow([(cx, y[r] - 8), (cx, y[r - 1] + bh + 8)], colour)

    across(0, BLUE[1])                # Rohit's band -> his plate
    down(1, 0, BLUE[1])               # his plate    -> the wire
    down(1, 1, RED[1])                # the wire     -> her plate
    across(2, GREEN[1], right=False)  # her plate    -> Savithri's band
    up(0, 2, GREEN[1])                # her band     -> the room
    up(0, 1, GREY[1])                 # the room     -> Rohit's band
    return f, H


fig, H = solve(build, W)
fig.save("02-the-loop", H)
