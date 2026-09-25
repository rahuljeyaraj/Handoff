# Generates docs/element14-blog/29-the-floor.svg and .png
# Why the first radio was scrapped, in one picture. Left: v1 asked whether the
# tone was louder than usual, so it had to remember what usual was — and the
# remembered floor only ever climbed, because a frame it had already gone deaf
# to was averaged in as quiet. The numbers are the last measurement of the
# 22 Sep session (board 379E: tone 133-150, floor 61-83, gate 228, deaf).
# Right: v2 asks whether the tone is louder than the room in the same 50 us.
# The two bars are two real captures from the 25 Sep plate bench.

import math

from figlib import Fig, solve, tw, wrap, INK, RED, GREY, LH

TITLES = ("v1: louder than usual?", "v2: louder than the room, right now?")
NOTES = ("Usual has to be remembered. Every frame it had already gone deaf to was "
         "averaged in as quiet, so the floor only ever climbed.",
         "Signal and room are measured in the same 50 µs, in the same amplifier, "
         "through the same body. Grip cancels.")

# left panel, as shares of the plot height
SIG = 0.55
FLOOR0, FLOOR1 = 0.06, 0.30      # the floor, climbing across the panel
GATE = 3.0                       # the gate was three times the floor
DEAF = "Deaf from here"
LEFT_LABELS = (("the tone, unchanged", RED), ("the floor it remembered", GREY),
               ("the gate", GREY))

# right panel: (label, signal, room), two real captures, both heard
BARS = (("a firm grip", 716, 74), ("a lighter grip", 410, 80))
TOP = 800.0
HEARD = "Heard"

W, M, G, PH = 1600, 36, 64, 330


def build(fs):
    pw = (W - 2 * M - G) / 2
    note_lines = [wrap(n, pw, fs) for n in NOTES]
    if any(len(ls) > 3 for ls in note_lines):
        return None
    if any(tw(t, fs, True) > pw for t in TITLES):
        return None
    nh = max(len(ls) for ls in note_lines) * fs * LH
    head_h = fs * 2.0
    H = M + head_h + PH + fs * 1.2 + nh + M
    f = Fig(W, fs)

    for i, (x0, title) in enumerate(((M, TITLES[0]), (M + pw + G, TITLES[1]))):
        f.text(x0, M + fs, title, INK, bold=True, anchor="start")
        f.rect(x0, M + head_h, pw, PH, "#ffffff", "#cfcfcf", sw=2, rx=8)
        for j, ln in enumerate(note_lines[i]):
            f.text(x0, M + head_h + PH + fs * (1.5 + j * LH), ln, GREY[2], anchor="start")

    # ---- left: the floor climbing into the signal
    x0, base, top = M, M + head_h + PH, M + head_h
    a, b = x0 + 26, x0 + pw - 26

    def yv(v):
        return base - 14 - (PH - 40) * v

    cross = (SIG / GATE - FLOOR0) / (FLOOR1 - FLOOR0)
    f.rect(a + (b - a) * cross, top + 2, b - (a + (b - a) * cross) - 2, PH - 4,
           "#f6f6f6", "#f6f6f6", sw=0, rx=0)
    n = 90
    f.line([(a + (b - a) * i / n,
             yv(SIG + 0.035 * math.sin(i * 1.7) * math.cos(i * 0.6)))
            for i in range(n + 1)], RED[1], sw=4)
    f.line([(a, yv(FLOOR0)), (b, yv(FLOOR1))], GREY[1], sw=4, dash="12 9")
    f.line([(a, yv(GATE * FLOOR0)), (b, yv(min(GATE * FLOOR1, 0.97)))], INK, sw=4)
    f.text(a + 8, yv(SIG) - fs * 0.7, LEFT_LABELS[0][0], RED[2], bold=True, anchor="start")
    f.text(b - 8, yv(FLOOR1) + fs * 1.15, LEFT_LABELS[1][0], GREY[2], anchor="end")
    f.text(b - 8, yv(min(GATE * FLOOR1, 0.97)) - fs * 0.6, LEFT_LABELS[2][0], INK,
           anchor="end")
    f.text((a + (b - a) * cross + b) / 2, yv(0.40), DEAF, GREY[2], bold=True)

    # ---- right: two captures, both above the room
    x0 = M + pw + G
    gw = pw / 2
    bw = gw * 0.26
    for i, (label, sig, room) in enumerate(BARS):
        cx = x0 + gw * (i + 0.5)
        for dx, v, st in ((-bw * 0.62, sig, RED), (bw * 0.62, room, GREY)):
            h = (PH - 110) * v / TOP
            f.rect(cx + dx - bw / 2, base - 96 - h, bw, h, st[0], st[1], sw=2.5, rx=6)
            f.text(cx + dx, base - 104 - h, str(v), st[2], bold=True)
        f.text(cx, base - 62, label, INK)
        f.text(cx, base - 18, HEARD, RED[2], bold=True)
    return f, H


fig, H = solve(build, W)
fig.save("29-the-floor", H)
