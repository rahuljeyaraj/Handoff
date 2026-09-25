# Generates docs/element14-blog/09-inside.svg and .png
# What is inside one band, around the Pico. This figure colours by direction,
# not by whose (the one figure that does, at the user's request): purple feeds
# the Pico, orange is driven by it, teal is the Pico itself. The plate is both,
# half and half. Grey is not part of the band: the phone and the skin, one at
# each end of the top row. Inputs sit on the left, outputs on the right, so the
# plate's purple half faces the amplifiers and its orange half faces the skin.

from figlib import Fig, need, solve, GREY, INK

IN = ("#f1ebf8", "#6a3d9a", "#6a3d9a")      # into the Pico
OUT = ("#fdf0e2", "#c26100", "#c26100")     # driven by the Pico
PICO_C = ("#e3f3f4", "#00838f", "#00838f")  # the Pico
HALF = ("url(#half)", "url(#half-edge)", INK)

TOP = [
    ("The phone", "Over Bluetooth. The wearer's card in, other cards out.", GREY),
    ("Two amplifiers", "×11 each. The tone arrives tiny.", IN),
    ("Coated plate", "Sends the tone, and hears the other band's.", HALF),
    ("Skin", "Carries the tone both ways.", GREY),
]
PICO = ("Raspberry Pi Pico 2 W",
        "Makes both tones, 180 and 200 kHz, weighs five pitches in what comes back, "
        "and runs Bluetooth. The rest is software.", PICO_C)
BOTTOM = [
    ("Button", "Press for the battery. Hold 5 s to reset the band.", IN),
    ("Battery", "1500 mAh Li-ion, with an on/off switch.", IN),
    ("RGB LED", "Blue to pair, white for a handshake, green when done.", OUT),
    ("Motor", "A buzz when a card is shared or received.", OUT),
]

W, M, GX, VG = 1600, 36, 70, 90
TWIN = 34     # half the spacing of the phone's two arrows

DEFS = f"""  <defs>
    <linearGradient id="half" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0.5" stop-color="{IN[0]}"/><stop offset="0.5" stop-color="{OUT[0]}"/>
    </linearGradient>
    <linearGradient id="half-edge" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0.5" stop-color="{IN[1]}"/><stop offset="0.5" stop-color="{OUT[1]}"/>
    </linearGradient>
  </defs>"""


def build(fs):
    cw = (W - 2 * M - 3 * GX) / 4
    pw = W - 2 * M
    h1 = [need(t, b, cw, fs) for t, b, _ in TOP]
    h2 = [need(t, b, cw, fs) for t, b, _ in BOTTOM]
    hp = need(PICO[0], PICO[1], pw, fs)
    if None in h1 + h2 or hp is None:
        return None
    ha, hb = max(h1), max(h2)
    H = M + ha + VG + hp + VG + hb + M

    f = Fig(W, fs)
    f.o.append(DEFS)
    x = [M + i * (cw + GX) for i in range(4)]
    cx = [xi + cw / 2 for xi in x]
    y1 = M
    yp = y1 + ha + VG
    y2 = yp + hp + VG

    for xi, (t, b, st) in zip(x, TOP):
        f.box(xi, y1, cw, ha, t, b, st)
    f.box(M, yp, pw, hp, *PICO)
    for xi, (t, b, st) in zip(x, BOTTOM):
        f.box(xi, y2, cw, hb, t, b, st)

    pin, pout, grey = IN[1], OUT[1], GREY[1]
    mid1 = y1 + ha / 2
    top_out, top_in = y1 + ha + 8, yp - 8        # between the top row and the Pico
    bot_out, bot_in = yp + hp + 8, y2 - 8        # between the Pico and the bottom row

    # the phone: both ways over Bluetooth
    f.arrow([(cx[0] - TWIN, top_out), (cx[0] - TWIN, top_in)], grey)
    f.arrow([(cx[0] + TWIN, top_in), (cx[0] + TWIN, top_out)], grey)
    # the loop: the Pico drives the plate's orange half, the purple half feeds
    # the amplifiers, the amplifiers feed the Pico
    f.arrow([(x[2] + cw * 0.75, top_in), (x[2] + cw * 0.75, top_out)], pout)
    f.arrow([(x[2] - 8, mid1), (x[1] + cw + 8, mid1)], pin)
    f.arrow([(cx[1], top_out), (cx[1], top_in)], pin)
    # the plate and the skin: the tone goes both ways through the coating
    gm = x[2] + cw + GX / 2
    f.arrow([(gm, mid1), (x[3] - 8, mid1)], grey)
    f.arrow([(gm, mid1), (x[2] + cw + 8, mid1)], grey)
    # the button and the battery feed the Pico, the Pico drives the light and
    # the motor
    f.arrow([(cx[0], bot_in), (cx[0], bot_out)], pin)
    f.arrow([(cx[1], bot_in), (cx[1], bot_out)], pin)
    f.arrow([(cx[2], bot_out), (cx[2], bot_in)], pout)
    f.arrow([(cx[3], bot_out), (cx[3], bot_in)], pout)
    return f, H


fig, H = solve(build, W)
fig.save("09-inside", H)
