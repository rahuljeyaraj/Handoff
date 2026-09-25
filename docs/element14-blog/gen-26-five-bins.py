# Generates docs/element14-blog/26-five-bins.svg and .png
# The five pitches the band weighs in every window (link v2 §4). Two of them
# are transmitted — the louder of the pair is the chip — and three are never
# transmitted at all: they are the noise reference, read in the same 50 us as
# the signal. Red is the signal that crossed the two bodies, as everywhere in
# this post; grey is the room.

from figlib import Fig, need, solve, tw, INK, RED, GREY

HEAD = "Five numbers, every 50 µs. Nothing is remembered between them."

# (title, body, style, transmitted)
BINS = [
    ("140 kHz", "How loud is the room?", GREY, False),
    ("160 kHz", "How loud is the room?", GREY, False),
    ("Tone A, 180 kHz", "A 0.", RED, True),
    ("Tone B, 200 kHz", "A 1.", RED, True),
    ("220 kHz", "How loud is the room?", GREY, False),
]

TOP = "Sent. The louder of the two is the chip."
BOT = "Never sent. Three live noise meters, in the same amplifier, through the same body."

W, M, G = 1600, 36, 26
BR = 16          # bracket depth


def build(fs):
    unit = (W - 2 * M - 4 * G) / 5
    xs = [M + i * (unit + G) for i in range(5)]
    hs = [need(t, b, unit, fs) for t, b, _, _ in BINS]
    if None in hs:
        return None
    if tw(TOP, fs, True) > W - 2 * M or tw(BOT, fs, True) > W - 2 * M:
        return None
    bh = max(hs)
    head_h = fs * 2.0
    note_h = fs * 1.6 + BR + 10
    H = M + head_h + note_h + bh + note_h + M

    f = Fig(W, fs)
    f.text(M, M + fs, HEAD, INK, bold=True, anchor="start")

    y = M + head_h + note_h
    for x, (t, b, st, sent) in zip(xs, BINS):
        f.box(x, y, unit, bh, t, b, st, solid=sent)

    # the two tones: a bracket over them, with what the pair is for
    a, b = xs[2], xs[3] + unit
    ty = y - 10
    f.line([(a, ty - BR), (a, ty), (b, ty), (b, ty - BR)], RED[1], sw=4)
    f.text((a + b) / 2, ty - BR - fs * 0.55, TOP, RED[2], bold=True)

    # the three guards: a tick under each, and one line for all three
    gy = y + bh + 10
    for i in (0, 1, 4):
        f.line([(xs[i], gy), (xs[i], gy + BR), (xs[i] + unit, gy + BR), (xs[i] + unit, gy)],
               GREY[1], sw=4)
    f.text(W / 2, gy + BR + fs * 1.25, BOT, GREY[2], bold=True)
    return f, H


fig, H = solve(build, W)
fig.save("26-five-bins", H)
