# Figure 30: every light the band can show, and every buzz, at its real speed.
#
# The rows are not invented for the picture. Every colour and every duration
# below is copied from the pattern table in firmware/lib/ui/ui.c, and the
# evaluator here is that file's pat_at() and motor_at() written in Python: a
# background is a pattern with period_ms > 0 that repeats and rests dark, a
# foreground is a one-shot that plays and ends.
#
# The loop is 10 s at 20 frames a second, because 10 s is the longest period
# in the table (the low battery blip) and 50 ms is the shortest step (the
# rendezvous blink). The one alias is the fault: its 125 ms steps are not a
# whole number of frames, so it samples 150/100. The full cycle is still
# 250 ms, so the 4 Hz the table asks for is what you see.
#
# The one-shots are replayed on a cadence that divides the 10 s loop. The
# cadence is this picture's, not the firmware's; the shape and the timing
# inside each flash, and inside each buzz, are the firmware's.

import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "30-status-light.gif")

# ---- the palette, from ui.c ------------------------------------------

OFF = (0, 0, 0)
WHITE = (255, 255, 255)
BLUE = (0, 0, 255)
GREEN = (0, 255, 0)
AMBER = (255, 120, 0)
RED = (255, 0, 0)
PURPLE = (160, 0, 255)
MAGENTA = (255, 0, 255)

# ---- the motor, from ui.c's k_m_* table ------------------------------
# Durations alternate on, off, on... starting on. A coin ERM takes ~50 ms to
# spin up, which is why the shortest pulse in the table is 100 ms.

M_TAP = ([100], "with a tap")
M_DOUBLE = ([100, 120, 100], "with two taps")
M_TRIPLE = ([100, 120, 100, 120, 100], "with three taps")
M_BUZZ = ([250], "with a buzz")
M_LONG = ([600], "with a long buzz")

# ---- the table, from ui.c --------------------------------------------
# kind "bg": (steps, period_ms), period 0 means solid and held.
# kind "fg": (steps, 0) plus a replay cadence for this picture.
# motor_every: for the two battery rows, whose buzz fires once at the
# crossing and not on every blip, so the picture needs a cadence of its own.

SECTIONS = [
    (
        "What the band shows on its own",
        "a background, picked by priority, for as long as it is true",
        [
            dict(name="Listening", d1="Idle, waiting for a touch",
                 d2="Off, always",
                 kind="bg", pat=([], 1000)),
            dict(name="No owner", d1="Advertising, no phone yet",
                 d2="Blue double flash every 2 s",
                 kind="bg", pat=([(BLUE, 80), (OFF, 80), (BLUE, 80)], 2000)),
            dict(name="Holding a card", d1="A card kept for a phone",
                 d2="Magenta blip every 5 s",
                 kind="bg", pat=([(MAGENTA, 100)], 5000)),
            dict(name="Handshake", d1="Talking over the skin",
                 d2="White, solid",
                 kind="bg", pat=([(WHITE, 0)], 0)),

            dict(name="Rendezvous", d1="A peer answered",
                 d2="White, 10 times a second",
                 kind="bg", pat=([(WHITE, 50), (OFF, 50)], 100)),
            dict(name="Battery low", d1="Under the low mark",
                 d2="Red blip every 10 s",
                 kind="bg", pat=([(RED, 100)], 10000),
                 motor=M_BUZZ, motor_every=10000),
            dict(name="Battery critical", d1="Nearly flat",
                 d2="Red triple every 5 s",
                 kind="bg", pat=([(RED, 100), (OFF, 100), (RED, 100),
                                  (OFF, 100), (RED, 100)], 5000),
                 motor=M_LONG, motor_every=5000),
            dict(name="Fault", d1="Wrong, until a reboot",
                 d2="Red and blue, 4 times a second",
                 kind="bg", pat=([(RED, 125), (BLUE, 125)], 250)),
        ],
    ),
    (
        "What it shows when something happens",
        "a one-shot over the background, replayed here so you can see it",
        [
            dict(name="Boot", d1="Power on", d2="White, 200 ms",
                 kind="fg", pat=([(WHITE, 200)], 0), repeat=1000,
                 motor=M_TAP),
            dict(name="Phone connected", d1="The app is on the link",
                 d2="Blue, 2 s",
                 kind="fg", pat=([(BLUE, 2000)], 0), repeat=2500),
            dict(name="Card written", d1="The phone wrote your card",
                 d2="Green double flash",
                 kind="fg", pat=([(GREEN, 100), (OFF, 100), (GREEN, 100)], 0),
                 repeat=1000, motor=M_TAP),
            dict(name="Card received", d1="A handshake completed",
                 d2="Green, 2 s",
                 kind="fg", pat=([(GREEN, 2000)], 0), repeat=2500,
                 motor=M_DOUBLE),

            dict(name="Card forwarded", d1="The phone took the card",
                 d2="Green 2 s, then a blip",
                 kind="fg",
                 pat=([(GREEN, 2000), (OFF, 200), (GREEN, 100)], 0),
                 repeat=5000),
            dict(name="Card collected", d1="A kept card handed over",
                 d2="One green blip, 100 ms",
                 kind="fg", pat=([(GREEN, 100)], 0), repeat=1000),
            dict(name="Nothing to give", d1="A handshake with no card",
                 d2="Amber double flash",
                 kind="fg", pat=([(AMBER, 100), (OFF, 100), (AMBER, 100)], 0),
                 repeat=1000),
            dict(name="Handshake aborted", d1="The exchange broke off",
                 d2="Amber triple flash",
                 kind="fg",
                 pat=([(AMBER, 100), (OFF, 100), (AMBER, 100),
                       (OFF, 100), (AMBER, 100)], 0),
                 repeat=1250, motor=M_LONG),

            dict(name="New wearer", d1="The bond was cleared",
                 d2="Purple, four flashes",
                 kind="fg",
                 pat=([(PURPLE, 100), (OFF, 100), (PURPLE, 100), (OFF, 100),
                       (PURPLE, 100), (OFF, 100), (PURPLE, 100)], 0),
                 repeat=2000, motor=M_BUZZ),
            dict(name="Identify", d1="Find my band, from the app",
                 d2="White, three fast flashes",
                 kind="fg",
                 pat=([(WHITE, 100), (OFF, 100), (WHITE, 100),
                       (OFF, 100), (WHITE, 100)], 0),
                 repeat=1250, motor=M_TRIPLE),
            dict(name="Button held", d1="Five seconds on the button",
                 d2="Purple, solid while held",
                 kind="fg", pat=([(PURPLE, 5000)], 0), repeat=10000,
                 motor=M_TAP),
        ],
    ),
    (
        "What the button answers",
        "one press, and the band shows the charge for 300 ms",
        [
            dict(name="Good", d1="Plenty left", d2="Green, 300 ms",
                 kind="fg", pat=([(GREEN, 300)], 0), repeat=1000),
            dict(name="Half", d1="Past halfway", d2="Amber, 300 ms",
                 kind="fg", pat=([(AMBER, 300)], 0), repeat=1000),
            dict(name="Low", d1="Charge it", d2="Red, 300 ms",
                 kind="fg", pat=([(RED, 300)], 0), repeat=1000),
            dict(name="No reading", d1="Nothing measured yet",
                 d2="White, 300 ms",
                 kind="fg", pat=([(WHITE, 300)], 0), repeat=1000),
        ],
    ),
]

LOOP_MS = 10000
STEP_MS = 50

# ---- ui.c's pat_at and motor_at ---------------------------------------


def colour_at(row, t):
    steps, period = row["pat"]
    if row["kind"] == "bg":
        if period == 0:
            return steps[0][0]          # a step of 0 ms is held
        tt = t % period
    else:
        tt = t % row["repeat"]
    for c, ms in steps:
        if tt < ms:
            return c
        tt -= ms
    return OFF                          # a background rests dark, a one-shot ends


def motor_at(row, t):
    m = row.get("motor")
    if not m:
        return False
    tt = t % row.get("motor_every", row.get("repeat", LOOP_MS))
    for i, ms in enumerate(m[0]):
        if tt < ms:
            return i % 2 == 0
        tt -= ms
    return False


# ---- the look ---------------------------------------------------------

BG = (250, 250, 250)
INK = (20, 32, 43)
MUTED = (62, 72, 84)
BUZZ_INK = (33, 46, 60)
RULE = (226, 230, 234)

DIA = 84
SPRITE = 220
SS = 4

RING_DARK = (16, 22, 30)
RING_PALE = (176, 186, 196)
BODY_OFF = (23, 28, 35)
RING_OFF = (44, 51, 60)

COL_W = 280
PAD_TOP = 26
PAD_BOT = 30
HEAD_H = 58
ROW_H = 232


def font(names, size):
    for f in names:
        p = os.path.join(r"C:\Windows\Fonts", f)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


F_NAME = font(["seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"], 21)
F_DESC = font(["segoeui.ttf", "arial.ttf"], 17)
F_BUZZ = font(["seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"], 16)
F_HEAD = font(["seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"], 18)
F_SUB = font(["segoeui.ttf", "arial.ttf"], 16)


def sprite(colour):
    """One LED, drawn once and pasted every frame it is showing."""
    lit = colour != OFF
    body = colour if lit else BODY_OFF
    ring = RING_PALE if colour == WHITE else (RING_DARK if lit else RING_OFF)

    img = Image.new("RGBA", (SPRITE, SPRITE), (0, 0, 0, 0))
    c = SPRITE / 2

    if lit:
        mask = Image.new("L", (SPRITE, SPRITE), 0)
        d = ImageDraw.Draw(mask)
        r = DIA / 2 + 5
        d.ellipse([c - r, c - r, c + r, c + r], fill=255)
        mask = mask.filter(ImageFilter.GaussianBlur(15))
        mask = mask.point(lambda v: int(v * 0.72))
        glow = Image.new("RGBA", (SPRITE, SPRITE), colour + (0,))
        glow.putalpha(mask)
        img = Image.alpha_composite(img, glow)

    disc = Image.new("RGBA", (SPRITE * SS, SPRITE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(disc)
    cc = SPRITE * SS / 2
    r = DIA * SS / 2
    d.ellipse([cc - r, cc - r, cc + r, cc + r], fill=body + (255,),
              outline=ring + (255,), width=int(4.5 * SS))
    disc = disc.resize((SPRITE, SPRITE), Image.LANCZOS)
    img = Image.alpha_composite(img, disc)

    # the bead's own shine, so the lit ones look like glass and not paint
    hl = Image.new("RGBA", (SPRITE, SPRITE), (0, 0, 0, 0))
    d = ImageDraw.Draw(hl)
    d.ellipse([c - DIA * 0.28, c - DIA * 0.36, c + DIA * 0.28, c - DIA * 0.02],
              fill=(255, 255, 255, 46 if lit else 16))
    hl = hl.filter(ImageFilter.GaussianBlur(7))
    return Image.alpha_composite(img, hl)


def buzz_sprite():
    """The motor running: two arcs either side of the bead, shaken outward."""
    img = Image.new("RGBA", (SPRITE * SS, SPRITE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = SPRITE * SS / 2
    for i, (r, ext) in enumerate(((DIA * 0.72, 30), (DIA * 0.95, 22))):
        r *= SS
        for a0 in (-ext, 180 - ext):
            d.arc([c - r, c - r, c + r, c + r], a0, a0 + 2 * ext,
                  fill=BUZZ_INK + (255,), width=int((4 - i) * SS))
    return img.resize((SPRITE, SPRITE), Image.LANCZOS)


SPRITES = {c: sprite(c) for c in
           (OFF, WHITE, BLUE, GREEN, AMBER, RED, PURPLE, MAGENTA)}
BUZZ = buzz_sprite()

# ---- layout -----------------------------------------------------------

W = COL_W * 4
places = []          # (row dict, cx, cy)
heads = []           # (title, sub, y)
y = PAD_TOP

for title, sub, rows in SECTIONS:
    heads.append((title, sub, y))
    y += HEAD_H
    for i in range(0, len(rows), 4):
        band = rows[i:i + 4]
        left = (W - len(band) * COL_W) / 2
        for j, row in enumerate(band):
            places.append((row, left + COL_W * (j + 0.5), y + 6 + DIA / 2))
        y += ROW_H
H = int(y + PAD_BOT)


def centred(d, text, cx, top, f, fill):
    w = d.textlength(text, font=f)
    if w > COL_W - 20:
        raise SystemExit("too wide for its column: %r (%d px)" % (text, w))
    d.text((cx - w / 2, top), text, font=f, fill=fill)


def base_frame():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    for title, sub, hy in heads:
        if hy > PAD_TOP:
            d.line([28, hy - 8, W - 28, hy - 8], fill=RULE, width=1)
        d.text((36, hy + 8), title, font=F_HEAD, fill=INK)
        x = 36 + d.textlength(title, font=F_HEAD)
        d.text((x + 10, hy + 10), "\u00b7  " + sub, font=F_SUB, fill=MUTED)
    for row, cx, cy in places:
        top = cy + DIA / 2 + 20
        centred(d, row["name"], cx, top, F_NAME, INK)
        centred(d, row["d1"], cx, top + 31, F_DESC, MUTED)
        centred(d, row["d2"], cx, top + 57, F_DESC, MUTED)
        if row.get("motor"):
            centred(d, row["motor"][1], cx, top + 84, F_BUZZ, BUZZ_INK)
    return im


BASE = base_frame()


def render(t):
    im = BASE.copy()
    for row, cx, cy in places:
        x, yy = int(cx - SPRITE / 2), int(cy - SPRITE / 2)
        if motor_at(row, t):
            im.paste(BUZZ, (x, yy), BUZZ)
        s = SPRITES[colour_at(row, t)]
        im.paste(s, (x, yy), s)
    return im


# ---- frames, then a shared palette, then a transparent delta ----------

rgb = [render(t) for t in range(0, LOOP_MS, STEP_MS)]

atlas = Image.new("RGB", (SPRITE * (len(SPRITES) + 1), SPRITE + 44), BG)
ad = ImageDraw.Draw(atlas)
ad.text((4, SPRITE + 6), "Ag" * 60, font=F_DESC, fill=MUTED)
ad.text((4, SPRITE + 22), "Ag" * 60, font=F_NAME, fill=INK)
for i, s in enumerate(SPRITES.values()):
    atlas.paste(s, (i * SPRITE, 0), s)
atlas.paste(BUZZ, (len(SPRITES) * SPRITE, 0), BUZZ)
COLORS = int(os.environ.get("FIG30_COLORS", "255"))
pal = atlas.quantize(colors=COLORS, method=Image.MEDIANCUT, dither=Image.NONE)

frames = [f.quantize(palette=pal, dither=Image.NONE) for f in rgb]

# Index 255 is free, so every pixel that did not change since the frame
# before becomes transparent and the decoder leaves the old one standing.
TRANS = COLORS
out = [frames[0]]
prev = frames[0].tobytes()
for f in frames[1:]:
    cur = f.tobytes()
    dd = bytearray(cur)
    for i in range(len(cur)):
        if cur[i] == prev[i]:
            dd[i] = TRANS
    delta = Image.frombytes("P", f.size, bytes(dd))
    delta.putpalette(f.getpalette())
    delta.info["transparency"] = TRANS
    out.append(delta)
    prev = cur

out[0].save(OUT, save_all=True, append_images=out[1:], duration=STEP_MS,
            loop=0, disposal=1, transparency=TRANS, optimize=False)
print("%s  %d x %d  %d frames  %.1f s  %.0f kB"
      % (os.path.basename(OUT), W, H, len(out), LOOP_MS / 1000.0,
         os.path.getsize(OUT) / 1024.0))
