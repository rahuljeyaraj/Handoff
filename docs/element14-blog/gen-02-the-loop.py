# Generates docs/element14-blog/02-the-loop.svg and .png
# The circuit a handshake makes: out through two bodies, back through the room.
# Top row left to right, bottom row right to left, so the picture is a loop.
# Blue is Rohit's, green is Savithri's, red is the two bodies, grey the room.

from figlib import Fig, need, solve, BLUE, GREEN, RED, GREY

S = {
    "rb":   ("Rohit's band", "Puts the tone out.", BLUE),
    "p1":   ("His coated plate", "Half a capacitor. Never touches his skin.", BLUE),
    "wire": ("The wire", "His arm, the handshake, her arm.", RED),
    "p2":   ("Her coated plate", "Picks the tone up off her skin.", GREEN),
    "sb":   ("Savithri's band", "Hears the tone.", GREEN),
    "room": ("The room", "Both bands' outer electrodes, and both bodies, couple to the floor and walls. The loop closes here.", GREY),
}

W, M, GX, VG = 1600, 36, 100, 110


def build(fs):
    cw = (W - 2 * M - 2 * GX) / 3
    h1 = [need(*S[k][:2], cw, fs) for k in ("rb", "p1", "wire")]
    h2 = [need(*S[k][:2], cw, fs) for k in ("room", "sb", "p2")]
    if None in h1 + h2:
        return None
    ha, hb = max(h1), max(h2)
    H = M + ha + VG + hb + M
    f = Fig(W, fs)
    x = [M + i * (cw + GX) for i in range(3)]
    y1, y2 = M, M + ha + VG

    for i, k in enumerate(("rb", "p1", "wire")):
        f.box(x[i], y1, cw, ha, *S[k])
    for i, k in enumerate(("room", "sb", "p2")):
        f.box(x[i], y2, cw, hb, *S[k])

    # each arrow takes the colour of the box it leaves
    mid1, mid2 = y1 + ha / 2, y2 + hb / 2
    f.arrow([(x[0] + cw + 8, mid1), (x[1] - 8, mid1)], BLUE[1])
    f.arrow([(x[1] + cw + 8, mid1), (x[2] - 8, mid1)], BLUE[1])
    cx2 = x[2] + cw / 2
    f.arrow([(cx2, y1 + ha + 8), (cx2, y2 - 8)], RED[1])
    f.arrow([(x[2] - 8, mid2), (x[1] + cw + 8, mid2)], GREEN[1])
    f.arrow([(x[1] - 8, mid2), (x[0] + cw + 8, mid2)], GREEN[1])
    cx0 = x[0] + cw / 2
    f.arrow([(cx0, y2 - 8), (cx0, y1 + ha + 8)], GREY[1])
    return f, H


fig, H = solve(build, W)
fig.save("02-the-loop", H)
