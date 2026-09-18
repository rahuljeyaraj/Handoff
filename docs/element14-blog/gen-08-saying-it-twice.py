# Generates docs/element14-blog/08-saying-it-twice.svg and .png
# What the channel carries in a one-second handshake: six frames, the two bands
# taking turns, Rohit's first. Top strip: frame 1 twice, as first designed, so
# frame 3 never goes. Bottom strip: round robin, so both cards arrive whole.
# Blue is Rohit's band, green is Savithri's. Dashed is a frame the far end
# already has. The heading over each strip carries its result.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN

TOP = "Hands meet"
END = "One second"

R, S = ("Rohit's", BLUE), ("Savithri's", GREEN)

# (heading, six frames on the channel as (number, whose, repeated?))
STRIPS = [
    ("As first designed, frame 1 twice: frame 3 never goes.",
     [(1, R, False), (1, S, False), (2, R, False), (2, S, False), (1, R, True), (1, S, True)]),
    ("As built, round robin: both cards arrive whole.",
     [(1, R, False), (1, S, False), (2, R, False), (2, S, False), (3, R, False), (3, S, False)]),
]

W, M, SG, BG = 1600, 36, 14, 44


def body(who, rep):
    return who + (", again" if rep else "")


def build(fs):
    unit = (W - 2 * M - 5 * SG) / 6
    xs = [M + i * (unit + SG) for i in range(6)]
    if any(tw(h, fs, True) > W - 2 * M for h, _ in STRIPS):
        return None
    hs = [need(f"Frame {n}", body(who, rep), unit, fs)
          for _, cells in STRIPS for n, (who, _), rep in cells]
    if None in hs:
        return None
    box_h = max(hs)
    top_h = fs * 2.2
    head_h = fs * 1.8
    H = M + top_h + len(STRIPS) * (head_h + box_h) + BG * (len(STRIPS) - 1) + M
    f = Fig(W, fs)

    # time runs from the hands meeting to one second later
    f.text(M, M + fs, TOP, INK, bold=True, anchor="start")
    f.text(W - M, M + fs, END, INK, bold=True, anchor="end")
    ax0 = M + tw(TOP, fs, True) + 24
    ax1 = W - M - tw(END, fs, True) - 24
    f.arrow([(ax0, M + fs * 0.65), (ax1, M + fs * 0.65)], "#9a9a9a")

    y = M + top_h
    for head, cells in STRIPS:
        f.text(M, y + fs, head, INK, bold=True, anchor="start")
        y += head_h
        for x, (n, (who, st), rep) in zip(xs, cells):
            f.box(x, y, unit, box_h, f"Frame {n}", body(who, rep), st,
                  dash="10 8" if rep else None)
        y += box_h + BG
    return f, H


fig, H = solve(build, W)
fig.save("08-saying-it-twice", H)
