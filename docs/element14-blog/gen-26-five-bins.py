# Generates docs/element14-blog/26-five-bins.svg and .png
# The five pitches the band weighs in every window (link v2 §4), drawn as what
# they are: five heights, side by side, and one line the tone has to clear.
#
# Only one of the two tones is on the plate at a time, and step 4 measured the
# OFF tone as indistinguishable from silence, so tone B is drawn down among the
# room's three. The line is the middle of those three times the margin: the
# margin is a power ratio, so on these amplitude-like bars it is its root,
# about four. Blue is the pair a band sent, grey is the room, as in figure 02.
# Nothing here is red: red is the two bodies everywhere else in this post.

from figlib import Fig, tw, INK, MUTED, BLUE, GREY

CALL = "Louder. This 50 µs is an A."
LINE1 = "the middle of the three,"
LINE2 = "plus the margin"

# (pitch, height as a fraction of the plot, style, what it is)
BARS = [
    ("140 kHz", 0.09, GREY, "the room"),
    ("160 kHz", 0.12, GREY, "the room"),
    ("180 kHz", 1.00, BLUE, "tone A"),
    ("200 kHz", 0.13, BLUE, "tone B"),
    ("220 kHz", 0.10, GREY, "the room"),
]

GUARDS = sorted(h for _, h, st, _ in BARS if st is GREY)
CUT = GUARDS[1] * 4.1          # the middle of the three, times the margin

W, M, GAP = 1600, 36, 44
GUTTER = 560                   # room at the right for the line's label
PH = 360                       # plot height
FS = 40


def build(fs):
    x0, x1 = M, W - M - GUTTER
    bw = (x1 - x0 - 4 * GAP) / 5
    if tw(LINE1, fs, True) > GUTTER - 40:
        return None

    call_h = fs * 1.6
    base = M + call_h + PH
    H = base + fs * 3.4 + M

    f = Fig(W, fs)
    for i, (pitch, frac, st, what) in enumerate(BARS):
        x = x0 + i * (bw + GAP)
        bh = PH * frac
        f.rect(x, base - bh, bw, bh, st[0], st[1], rx=4)
        f.text(x + bw / 2, base + fs * 1.35, pitch, INK, bold=True)
        f.text(x + bw / 2, base + fs * 2.65, what, st[2])

    # the one line the tone has to clear
    y = base - PH * CUT
    f.line([(x0 - 8, y), (x1 + 16, y)], INK, sw=3, dash="13 10")
    f.text(x1 + 30, y - fs * 0.18, LINE1, INK, bold=True, anchor="start")
    f.text(x1 + 30, y + fs * 1.06, LINE2, INK, bold=True, anchor="start")

    # what the picture is of: the taller of the two tones won this window
    cx = x0 + 2 * (bw + GAP) + bw / 2
    f.text(cx, M + call_h - fs * 0.5, CALL, BLUE[2], bold=True)
    f.text(x0, base + fs * 2.65, "", MUTED, anchor="start")
    return f, H


fig, H = build(FS)
fig.save("26-five-bins", H)
