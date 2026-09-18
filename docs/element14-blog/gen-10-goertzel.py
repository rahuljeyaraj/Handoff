# Generates docs/element14-blog/10-goertzel.svg and .png
# What the Goertzel filter replaces. Top row: the usual receiver, which shifts
# the tone down with an oscillator and mixers, then filters and amplifies it
# again (design §4.3). Bottom row: the band's, where the converter samples the
# tone itself and a Goertzel filter in software measures it. The parts the band
# does not have are dashed. Colours follow figure 09: purple feeds the Pico,
# teal is the Pico (the converter and the software in it).

from figlib import Fig, need, solve, INK

IN = ("#f1ebf8", "#6a3d9a", "#6a3d9a")      # into the Pico, as figure 09
PICO_C = ("#e3f3f4", "#00838f", "#00838f")  # the Pico, as figure 09
GONE = ("#ffffff", "#9a9a9a", "#707070")    # a part the band does not have

AMPS = ("Two amplifiers", "The tone arrives tiny.", IN)

# (heading, five slots of (title, body, style, dashed) or None)
ROWS = [
    ("The usual receiver",
     [AMPS + (False,),
      ("Oscillator, mixers", "Shift the tone down. Two, as the clocks are not in step.", GONE, True),
      ("Filter", "Keeps only the shifted tone.", GONE, True),
      ("Amplifier", "Boosts it again.", GONE, True),
      ("Converter", "Samples the slow, shifted result.", PICO_C, False)]),
    ("The band's receiver: four parts fewer",
     [AMPS + (False,),
      None, None,
      ("Converter", "Fast enough to sample the tone itself.", PICO_C, False),
      ("Goertzel filter", "In software. Every 50 µs: how much 200 kHz?", PICO_C, False)]),
]

W, M, SG, BG = 1600, 36, 34, 50


def build(fs):
    unit = (W - 2 * M - 4 * SG) / 5
    xs = [M + i * (unit + SG) for i in range(5)]
    hs = [need(s[0], s[1], unit, fs) for _, slots in ROWS for s in slots if s]
    if None in hs:
        return None
    box_h = max(hs)
    head_h = fs * 1.8
    H = M + len(ROWS) * (head_h + box_h) + BG * (len(ROWS) - 1) + M
    f = Fig(W, fs)

    y = M
    for head, slots in ROWS:
        f.text(M, y + fs, head, INK, bold=True, anchor="start")
        y += head_h
        placed = [(x, s) for x, s in zip(xs, slots) if s]
        for x, (t, b, st, dashed) in placed:
            f.box(x, y, unit, box_h, t, b, st, dash="10 8" if dashed else None)
        for (x0, _), (x1, _) in zip(placed, placed[1:]):
            cy = y + box_h / 2
            f.arrow([(x0 + unit + 4, cy), (x1 - 4, cy)], "#9a9a9a", head=14)
        y += box_h + BG
    return f, H


fig, H = solve(build, W)
fig.save("10-goertzel", H)
