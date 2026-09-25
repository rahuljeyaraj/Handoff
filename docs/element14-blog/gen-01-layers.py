# Generates docs/element14-blog/01-layers.svg
# One card's journey, Rohit -> Savithri, in three colour-coded legs.
#
# Every box uses the SAME font size. The size is solved for: the largest one
# at which all six rows, each as tall as its wordiest box needs, still fit the
# target stack height. Row heights then follow from the wrapping.

W = 2200
STACK_MAX = 950          # budget for the six rows plus their gaps
GAP = 30
PADX, PADY = 20, 16

LEG_C = {1: "#2f5fae", 2: "#c0392b", 3: "#2e7d32"}
LEG_F = {1: "#e9f1fc", 2: "#fdeceb", 3: "#e9f4ec"}
HINGE_F, HINGE_S, HINGE_T = "#f7f7f7", "#6a6a6a", "#333333"
GREY_F, GREY_S, GREY_T = "#f1f1f1", "#9a9a9a", "#707070"

out = []
A = out.append


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# ---- text metrics ------------------------------------------------------
NARROW = "iljtfIr.,;:'!|()[]“”"
WIDE = "mwMW@"


def tw(s, fs, bold=False):
    total = 0.0
    for ch in s:
        if ch == " ":
            total += 0.27
        elif ch in NARROW:
            total += 0.30
        elif ch in WIDE:
            total += 0.86
        elif ch.isupper():
            total += 0.63
        elif ch.isdigit():
            total += 0.55
        else:
            total += 0.52
    return total * fs * (1.06 if bold else 1.0)


def wrap(text, maxw, fs):
    lines, cur = [], ""
    for wd in text.split():
        trial = wd if not cur else cur + " " + wd
        if tw(trial, fs) <= maxw or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = wd
    if cur:
        lines.append(cur)
    return lines


def balanced(text, maxw, fs):
    """Same line count, narrowest block — keeps the paragraph from going ragged."""
    if not text:
        return []
    n = len(wrap(text, maxw, fs))
    lo, hi = maxw * 0.35, maxw
    for _ in range(18):
        mid = (lo + hi) / 2
        if len(wrap(text, mid, fs)) <= n:
            hi = mid
        else:
            lo = mid
    return wrap(text, hi, fs)


def usable(w):
    return (w - 2 * PADX) * 0.97          # margin for font fallback


def block_h(lines, fs):
    return fs * 1.16 + len(lines) * fs * 1.34


# ---- content -----------------------------------------------------------
S = {
    1:  ("app", "Rohit's card, as he typed it."),
    2:  ("presentation", "Written out as vCard text."),
    3:  ("transport", "Cut into numbered Bluetooth messages."),
    4:  ("transport", "The messages stitched back into the vCard."),
    5:  ("presentation", "Details read out of the vCard."),
    6:  ("app", "Kept on the band until a hand is shaken."),
    7:  ("presentation", "Everything every card has in common is left behind."),
    8:  ("transport", "Cut into frames, most important first."),
    9:  ("MAC", "Read the other band's beacon, name and all. "
                "Waits for silence, then sends the card."),
    10: ("link", "Every frame gets a start marker and a checksum."),
    11: ("physical", "Bytes become two tones on the skin, one per half bit."),
    12: ("physical", "The two tones are weighed against each other, and read back as bytes."),
    13: ("link", "Start marker found, checksum checked. Damaged frames dropped."),
    14: ("MAC", "Sent its own beacon. Listens, and the card arrives."),
    15: ("transport", "Frames collected, the packed card rebuilt."),
    16: ("presentation", "Details unpacked from the bytes."),
    17: ("app", "Received. Held until the phone is ready for it."),
    18: ("presentation", "The common parts are put back. A real vCard again."),
    19: ("transport", "Cut into numbered Bluetooth messages."),
    20: ("transport", "The messages stitched back into the vCard."),
    21: ("presentation", "Details read out of the vCard."),
    22: ("app", "Rohit appears in Savithri's list."),
}

BT_NAMES = {"mac": "Bluetooth MAC", "link": "Bluetooth link", "phys": "Bluetooth physical"}

# ---- geometry ----------------------------------------------------------
RP_X, PH_W = 40, 340
RB_X, SUB = 440, 310
SB_X, SP_X = 1140, 1820
BAND_W = SUB * 2

rp_c = RP_X + PH_W / 2
rb_ph_c, rb_bd_c = RB_X + SUB / 2, RB_X + SUB * 1.5
sb_bd_c, sb_ph_c = SB_X + SUB / 2, SB_X + SUB * 1.5
sp_c = SP_X + PH_W / 2

ROW_KEYS = ["app", "pres", "trans", "mac", "link", "phys"]

# which boxes sit in each row: (x, width, step or grey name, leg)
LAYOUT = {
    "app":   [(RP_X, PH_W, 1, 1), (RB_X, BAND_W, 6, 0), (SB_X, BAND_W, 17, 0),
              (SP_X, PH_W, 22, 3)],
    "pres":  [(RP_X, PH_W, 2, 1), (RB_X, SUB, 5, 1), (RB_X + SUB, SUB, 7, 2),
              (SB_X, SUB, 16, 2), (SB_X + SUB, SUB, 18, 3), (SP_X, PH_W, 21, 3)],
    "trans": [(RP_X, PH_W, 3, 1), (RB_X, SUB, 4, 1), (RB_X + SUB, SUB, 8, 2),
              (SB_X, SUB, 15, 2), (SB_X + SUB, SUB, 19, 3), (SP_X, PH_W, 20, 3)],
    "mac":   [(RP_X, PH_W, None, 1), (RB_X, SUB, None, 1), (RB_X + SUB, SUB, 9, 2),
              (SB_X, SUB, 14, 2), (SB_X + SUB, SUB, None, 3), (SP_X, PH_W, None, 3)],
    "link":  [(RP_X, PH_W, None, 1), (RB_X, SUB, None, 1), (RB_X + SUB, SUB, 10, 2),
              (SB_X, SUB, 13, 2), (SB_X + SUB, SUB, None, 3), (SP_X, PH_W, None, 3)],
    "phys":  [(RP_X, PH_W, None, 1), (RB_X, SUB, None, 1), (RB_X + SUB, SUB, 11, 2),
              (SB_X, SUB, 12, 2), (SB_X + SUB, SUB, None, 3), (SP_X, PH_W, None, 3)],
}


def solve_font():
    """Largest uniform size whose six rows still fit the stack budget."""
    for fs in range(26, 12, -1):
        heights, ok = {}, True
        for key in ROW_KEYS:
            tallest = 0
            for _x, w, step, _leg in LAYOUT[key]:
                title, text = S[step] if step else (BT_NAMES[key], "")
                if tw(title, fs, True) > usable(w):
                    ok = False
                    break
                lines = balanced(text, usable(w), fs)
                if lines and max(tw(ln, fs) for ln in lines) > usable(w):
                    ok = False
                    break
                tallest = max(tallest, block_h(lines, fs))
            if not ok:
                break
            heights[key] = round(tallest + 2 * PADY)
        if ok and sum(heights.values()) + 5 * GAP <= STACK_MAX:
            return fs, heights
    raise SystemExit("no font size fits")


FS, ROW_H = solve_font()

TOP = 150
ROW_Y, _y = {}, TOP
for k in ROW_KEYS:
    ROW_Y[k] = _y
    _y += ROW_H[k] + GAP
BASE = _y - GAP
WIRE = BASE + 55
H = WIRE + 135


# ---- primitives --------------------------------------------------------
def rect(x, y, w, h, fill, stroke, sw=2.5):
    A(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{fill}" '
      f'stroke="{stroke}" stroke-width="{sw}"/>')


def label(cx, y, s, fs, fill, bold=False, italic=False, ls=0):
    b = ' font-weight="700"' if bold else ""
    i = ' font-style="italic"' if italic else ""
    l = f' letter-spacing="{ls}"' if ls else ""
    A(f'  <text x="{cx}" y="{y:.0f}" text-anchor="middle" font-size="{fs}" '
      f'fill="{fill}"{b}{i}{l}>{esc(s)}</text>')


def badge(x, y, n, colour):
    A(f'  <circle cx="{x}" cy="{y}" r="16" fill="{colour}"/>')
    A(f'  <text x="{x}" y="{y + 6}" text-anchor="middle" font-size="18" '
      f'font-weight="700" fill="#ffffff">{n}</text>')


def draw_box(x, y, w, h, title, text, fill, stroke, tcol, num, badge_c):
    rect(x, y, w, h, fill, stroke)
    lines = balanced(text, usable(w), FS)
    top = y + (h - block_h(lines, FS)) / 2
    cx = x + w / 2
    label(cx, top + FS, title, FS, tcol, bold=True)
    ly = top + FS * 1.16 + FS
    for ln in lines:
        label(cx, ly, ln, FS, "#2b2b2b")
        ly += FS * 1.34
    if num is not None:
        badge(x + 24, y + 24, num, badge_c)


def arrow(cx, y1, y2, colour, head=14, sw=4):
    d = 1 if y2 > y1 else -1
    se = y2 - d * head
    A(f'  <path d="M{cx} {y1} V{se}" stroke="{colour}" stroke-width="{sw}" '
      f'stroke-linecap="round" fill="none"/>')
    A(f'  <path d="M{cx - 9} {se} L{cx + 9} {se} L{cx} {y2} Z" fill="{colour}"/>')


# ---- draw --------------------------------------------------------------
A(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
  f'viewBox="0 0 {W} {H}" font-family="Segoe UI, Helvetica, Arial, sans-serif">')
A(f'  <rect width="{W}" height="{H}" fill="#ffffff"/>')

for x1, x2, leg, txt in ((40, 746, 1, "1.  AT THE DESK, ONCE"),
                         (754, 1446, 2, "2.  THE HANDSHAKE, ABOUT A SECOND"),
                         (1454, 2160, 3, "3.  MOMENTS LATER")):
    rect(x1, 16, x2 - x1, 54, LEG_C[leg], LEG_C[leg], 0)
    label((x1 + x2) / 2, 54, txt, 25, "#ffffff", bold=True, ls=2)

for cx, t in ((rp_c, "ROHIT'S PHONE"), (RB_X + SUB, "ROHIT'S BAND"),
              (SB_X + SUB, "SAVITHRI'S BAND"), (sp_c, "SAVITHRI'S PHONE")):
    label(cx, 108, t, 24, "#1a1a1a", bold=True, ls=1.5)
for cx, t in ((rb_ph_c, "phone side"), (rb_bd_c, "body side"),
              (sb_bd_c, "body side"), (sb_ph_c, "phone side")):
    label(cx, 136, t, 18, "#6a6a6a", italic=True)

HINGE_BADGE = {6: LEG_C[1], 17: LEG_C[2]}

for key in ROW_KEYS:
    y, h = ROW_Y[key], ROW_H[key]
    for x, w, step, leg in LAYOUT[key]:
        if step is None:
            draw_box(x, y, w, h, BT_NAMES[key], "", GREY_F, GREY_S, GREY_T, None, None)
        elif leg == 0:
            draw_box(x, y, w, h, *S[step], HINGE_F, HINGE_S, HINGE_T,
                     step, HINGE_BADGE[step])
        else:
            draw_box(x, y, w, h, *S[step], LEG_F[leg], LEG_C[leg], LEG_C[leg],
                     step, LEG_C[leg])

# ---- arrows ------------------------------------------------------------
def chain(cx, colour, down=True):
    seq = ROW_KEYS if down else list(reversed(ROW_KEYS))
    for a, b in zip(seq, seq[1:]):
        y1 = (ROW_Y[a] + ROW_H[a] + 4) if down else (ROW_Y[a] - 4)
        y2 = (ROW_Y[b] - 3) if down else (ROW_Y[b] + ROW_H[b] + 3)
        arrow(cx, y1, y2, colour)


chain(rp_c, LEG_C[1], True)
chain(rb_ph_c, LEG_C[1], False)
chain(rb_bd_c, LEG_C[2], True)
chain(sb_bd_c, LEG_C[2], False)
chain(sb_ph_c, LEG_C[3], True)
chain(sp_c, LEG_C[3], False)

# ---- the three media ---------------------------------------------------
for a, b, leg, t, sub in ((rp_c, rb_ph_c, 1, "radio", ""),
                          (rb_bd_c, sb_bd_c, 2, "the handshake",
                           "two bodies, coupled through skin"),
                          (sb_ph_c, sp_c, 3, "radio", "")):
    c = LEG_C[leg]
    A(f'  <path d="M{a} {BASE + 4} V{WIRE} H{b}" stroke="{c}" stroke-width="4" '
      f'fill="none" stroke-linecap="round"/>')
    arrow(b, WIRE, BASE + 4, c)
    label((a + b) / 2, WIRE + 53, t, 23, c, bold=True)
    if sub:
        label((a + b) / 2, WIRE + 81, sub, 19, "#6a6a6a")

A('</svg>')

import io
import os
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "01-layers.svg")
with io.open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
print(f"wrote {path}  font {FS}px  {W}x{H}")
