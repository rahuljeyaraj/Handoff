"""Floor plan for the Handoff wristband. Diagrams only - no prose on the sheet.

Writes hardware/floorplan.svg and build/floorplan.png.

Frame: mm on the PCB, looking down at the back of the RIGHT wrist, hand up.
    +x  thumb side -> little-finger side   (across the arm, SHORT side)
    +y  hand end   -> elbow end            (along the arm, LONG side)
Strap leaves the two long walls, left and right.
"""
from pathlib import Path

HW = Path(__file__).resolve().parent.parent

BOARD_W, BOARD_H, CORNER = 40.0, 62.0, 3.0
WALL = 1.5
BOSS_R, HOLE_R = 4.0, 1.7
HOLES = [(4.5, 4.5), (35.5, 4.5), (4.5, 57.5), (35.5, 57.5)]
STRAP_Y, STRAP_H = 24.0, 15.0

PW, PH = 21.0, 51.0
PX, PY = (BOARD_W - PW) / 2, 11.0             # Pico centred across the board
ANT_H = 11.0                                   # antenna keep-out, hand end
ROW_A, ROW_B = PX + 1.6, PX + PW - 1.6         # header rows: pins 1-20 / 21-40
BRK_X_A, BRK_X_B = ROW_A + 2.54, ROW_B - 2.54  # breakout pad columns, inboard
P0 = 60.6                                      # y of pin 1 and pin 40


def pin_a(n):
    return P0 - (n - 1) * 2.54


def pin_b(n):
    return P0 - (40 - n) * 2.54


# E1-E10: one breakout pad inboard of its own Pico pin (not a connector)
BRK_A = [(1, "GP0"), (2, "GP1"), (3, "GND"), (6, "GP4"), (7, "GP5")]
BRK_B = [(26, "GP20"), (27, "GP21"), (30, "RUN"), (32, "GP27"), (36, "3V3")]
LED_PINS = [(19, "R"), (18, "K"), (17, "G"), (16, "B")]

STACK = (10.0, 16.0, 20.0, 30.0)               # cell over pad, centred, 20 x 30
AFE = (15.0, 24.0, 10.0, 36.0)                 # top face, under the Pico
JMP = (15.0, 47.0, 10.0, 13.0)                 # bottom face, under the Pico

out = []


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class View:
    def __init__(self, ox, oy, s, vs=None):
        self.ox, self.oy, self.s = ox, oy, s
        self.vs = vs if vs else s

    def x(self, v):
        return self.ox + v * self.s

    def y(self, v):
        return self.oy + v * self.vs

    def rect(self, x, y, w, h, fill, stroke="#444", sw=1.0, dash=None, rx=0):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" fill="%s" '
                   'stroke="%s" stroke-width="%s"%s/>'
                   % (self.x(x), self.y(y), w * self.s, h * self.vs, rx * self.s, fill, stroke, sw, d))

    def circ(self, x, y, r, fill, stroke="#444", sw=1.0, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s" '
                   'stroke-width="%s"%s/>' % (self.x(x), self.y(y), r * self.s, fill, stroke, sw, d))

    def line(self, x1, y1, x2, y2, stroke="#444", sw=1.0, dash=None, arrow=False):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        a = ' marker-end="url(#arr)"' if arrow else ""
        out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%s"%s%s/>'
                   % (self.x(x1), self.y(y1), self.x(x2), self.y(y2), stroke, sw, d, a))

    def text(self, x, y, s, size=8, anchor="middle", fill="#222", weight="normal", rot=0):
        px, py = self.x(x), self.y(y)
        t = ' transform="rotate(%s %.1f %.1f)"' % (rot, px, py) if rot else ""
        out.append('<text x="%.1f" y="%.1f" font-size="%s" text-anchor="%s" fill="%s" '
                   'font-weight="%s"%s>%s</text>' % (px, py, size, anchor, fill, weight, t, esc(s)))

    def hole(self, x, y, sq=False, r=0.55):
        """Every J* pin and every Pico pin is through-hole."""
        if sq:
            self.rect(x - r, y - r, 2 * r, 2 * r, "#fff", "#333", 0.8)
        else:
            self.circ(x, y, r, "#fff", "#333", 0.8)

    def shell(self):
        self.rect(-WALL, -WALL, BOARD_W + 2 * WALL, BOARD_H + 2 * WALL, "#f5f2ea", "#9b9384",
                  1.5, rx=CORNER + WALL)
        for x0 in (-WALL - 7, BOARD_W + WALL):
            self.rect(x0, STRAP_Y, 7, STRAP_H, "#e7dcc8", "#b8a88e", 1, dash="4,3")
        self.text(-WALL - 3.5, STRAP_Y + STRAP_H / 2, "strap", 8, fill="#7a6a50", rot=-90)
        self.text(BOARD_W + WALL + 3.5, STRAP_Y + STRAP_H / 2, "strap", 8, fill="#7a6a50", rot=90)

    def board(self, fill="#eef5ea"):
        self.rect(0, 0, BOARD_W, BOARD_H, fill, "#2b6b2b", 2, rx=CORNER)
        for x, y in HOLES:
            self.circ(x, y, BOSS_R, "#f0ece2", "#c0b8a6", 1, dash="3,2")
            self.circ(x, y, HOLE_R, "#fff", "#333", 1.2)

    def pico_pins(self):
        for n in range(1, 21):
            self.hole(ROW_A, pin_a(n))
            self.hole(ROW_B, pin_b(n + 20))
        for n, _ in BRK_A:
            self.hole(BRK_X_A, pin_a(n), r=0.75)
            self.line(ROW_A + 0.6, pin_a(n), BRK_X_A - 0.8, pin_a(n), "#6b3f8a", 0.7)
        for n, _ in BRK_B:
            self.hole(BRK_X_B, pin_b(n), r=0.75)
            self.line(ROW_B - 0.6, pin_b(n), BRK_X_B + 0.8, pin_b(n), "#6b3f8a", 0.7)

    def xh2(self, y, label, fill="#f0e4f6"):
        """A 2-pin JST-XH, through-hole, pins in a row along y."""
        self.rect(31.4, y - 3.8, 5.8, 7.6, fill, "#6b3f8a", 1.1)
        self.hole(33.0, y - 1.25, sq=True)
        self.hole(33.0, y + 1.25)
        self.text(36.2, y, label, 7.5, fill="#4b2a63", rot=-90)


def raw_text(x, y, s, size=10, anchor="start", fill="#222", weight="normal"):
    out.append('<text x="%s" y="%s" font-size="%s" text-anchor="%s" fill="%s" font-weight="%s">%s</text>'
               % (x, y, size, anchor, fill, weight, esc(s)))


W, H = 1760, 760
out.append('<?xml version="1.0" encoding="UTF-8"?>')
out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
           'font-family="Segoe UI, Helvetica, Arial, sans-serif">' % (W, H, W, H))
out.append('<defs><marker id="arr" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">'
           '<path d="M0,0 L8,4 L0,8 z" fill="#444"/></marker></defs>')
out.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))

# =========================================================================
# 1 - TOP SIDE
# =========================================================================
raw_text(180, 46, "TOP  -  PCB 40 x 62", 14, weight="bold")
t = View(180, 96, 8.0)
t.shell()
t.board()
t.text(BOARD_W / 2, -3.8, "HAND", 11, weight="bold", fill="#555")
t.text(BOARD_W / 2, BOARD_H + WALL + 9.0, "ELBOW", 11, weight="bold", fill="#555")
t.text(-9.5, 10.0, "THUMB - faces up", 9, weight="bold", fill="#2b7a2b", rot=-90)
t.text(49.5, 10.0, "LITTLE FINGER", 9, weight="bold", fill="#888", rot=90)

t.rect(PX, PY, PW, PH, "#eaf1fa", "#2a5d9f", 1.5, rx=0.5)
t.rect(PX, PY, PW, ANT_H, "#fbe6e6", "#c98b8b", 1.1, dash="4,3")
t.text(20.0, 17.5, "antenna", 8, fill="#a33")
t.rect(15.7, BOARD_H, 8.6, 2.4, "#8d8d8d", "#333", 1.1)
t.rect(15.0, BOARD_H + WALL - 0.3, 10.0, 2.0, "#fff", "#aaa", 0.8)
t.text(29.5, BOARD_H + 4.4, "USB out the elbow edge", 8, anchor="end", fill="#333")
t.text(20.0, 21.8, "U1  Pico 2 W, socketed", 8.5, fill="#2a5d9f")
t.pico_pins()
t.text(BOARD_W / 2, -8.6, "E1-E10 = one breakout pad inboard of its own pin", 8.5, fill="#4b2a63")

t.rect(AFE[0], AFE[1], AFE[2], AFE[3], "#fdead0", "#c26a12", 1.5, rx=1)
t.rect(15.5, 25.0, 9.0, 4.6, "#fff4e3", "#c26a12", 1.0, dash="3,2")
t.text(20.0, 28.0, "R2/R3 island", 6.5, fill="#8a4a0a")
t.rect(17.2, 31.0, 5.6, 3.4, "#fff", "#c26a12", 1.0)
t.text(20.0, 33.4, "U2", 7.5, fill="#8a4a0a")
t.text(20.0, 38.5, "AFE", 9.5, weight="bold", fill="#8a4a0a")
t.text(20.0, 42.5, "R1-R11", 7.5, fill="#8a4a0a")
t.text(20.0, 46.0, "C1 C2 C3 C6", 7.5, fill="#8a4a0a")
t.text(20.0, 51.0, "SMD, top face,", 7, fill="#8a4a0a")
t.text(20.0, 54.0, "under the Pico", 7, fill="#8a4a0a")

# thumb wall: D2 / J3 near the hand, SW1 near the elbow
t.rect(1.6, pin_a(19) - 1.4, 2.8, 10.4, "#f0e4f6", "#6b3f8a", 1.1)
for i, (n, lab) in enumerate(LED_PINS):
    y = pin_a(n)
    t.hole(3.0, y, sq=(i == 0))
    t.line(3.8, y, ROW_A - 0.6, y, "#6b3f8a", 0.7)
    t.text(6.4, y + 0.5, lab, 6, fill="#4b2a63")
t.circ(-3.6, 18.7, 2.5, "#cdf0cd", "#2b7a2b", 1.3)
t.rect(-WALL - 0.3, 16.35, 2.0, 4.7, "#fff", "#aaa", 0.8)
t.text(-6.6, 18.7, "D2", 9, weight="bold", fill="#2b7a2b", rot=-90)
t.text(8.0, 18.7, "J3", 8, weight="bold", fill="#4b2a63", rot=-90)

SW1Y = 46.0
t.rect(0.2, SW1Y - 4.35, 5.5, 8.7, "#dadada", "#333", 1.2)
for dy in (-3.0, 0.0, 3.0):
    t.hole(8.8, SW1Y + dy)
for dy in (-6.05, 6.05):
    t.circ(3.0, SW1Y + dy, 0.575, "#fff", "#333", 0.8)
t.rect(-3.2, SW1Y - 1.3, 3.4, 2.6, "#777", "#222", 0.9)
t.rect(-WALL - 0.3, SW1Y - 3.0, 2.0, 6.0, "#fff", "#aaa", 0.8)
t.text(-6.6, SW1Y, "SW1", 9, weight="bold", fill="#333", rot=-90)

# hand strip
t.rect(17.0, 2.0, 6.0, 6.0, "#dadada", "#333", 1.2)
t.circ(20.0, 5.0, 1.75, "#bcbcbc", "#333", 0.9)
t.text(20.0, 10.4, "SW2 centred", 7.5, fill="#333")
t.circ(12.0, 4.5, 2.5, "#fff", "#c26a12", 1.1)
t.text(12.0, 5.3, "C4", 7.5, fill="#8a4a0a")
t.circ(28.0, 4.5, 2.5, "#fff", "#c26a12", 1.1)
t.text(28.0, 5.3, "C5", 7.5, fill="#8a4a0a")

# little-finger strip - every one of these is through-hole
t.xh2(18.0, "J2 pad")
t.xh2(27.0, "J1 cell")
t.xh2(36.0, "J5 chg")
t.rect(34.0, 45.0, 3.0, 3.6, "#fff", "#333", 1)
t.hole(35.5, 43.0)
t.hole(35.5, 50.62)
t.text(38.2, 46.8, "D1 + JP5", 7.5, fill="#333", rot=-90)

for n, lab in ((39, "VSYS"), (33, "AGND"), (31, "GP26")):
    t.text(ROW_B - 1.2, pin_b(n) + 0.6, lab, 6, anchor="end", fill="#1d3f6e")
t.text(ROW_A + 1.2, pin_a(15) + 0.6, "GP11 TX", 6, anchor="start", fill="#1d3f6e")

# =========================================================================
# 2 - BOTTOM SIDE
# =========================================================================
raw_text(720, 46, "BOTTOM  +  what sits under it", 14, weight="bold")
b = View(720, 96, 8.0)
b.shell()
b.board("#f4f7f2")
b.rect(PX, PY, PW, PH, "none", "#c3d3e6", 1.1, dash="3,3")
b.rect(PX, PY, PW, ANT_H, "none", "#e7c6c6", 1.0, dash="3,3")
b.text(20.0, 17.5, "antenna", 7.5, fill="#d8a8a8")
b.pico_pins()

b.rect(STACK[0], STACK[1], STACK[2], STACK[3], "#efe6cd", "#8a7a55", 1.6, dash="6,3")
b.text(20.0, 27.0, "CELL 20 x 30 x 5", 9.5, weight="bold", fill="#6e6040")
b.text(20.0, 31.5, "over", 8, fill="#8a6000")
b.text(20.0, 36.0, "PAD 20 x 30", 9.5, weight="bold", fill="#8a6000")
b.text(20.0, 40.5, "centred, one pocket,", 7.5, fill="#6e6040")
b.text(20.0, 43.5, "no parts on this face", 7.5, fill="#6e6040")

b.rect(JMP[0], JMP[1], JMP[2], JMP[3], "#ede4f5", "#6b3f8a", 1.4, rx=1)
b.text(20.0, 51.0, "JP1-JP7", 8, weight="bold", fill="#4b2a63")
b.text(20.0, 54.5, "TP1-TP7", 8, fill="#4b2a63")
b.text(20.0, 58.0, "TP12 TP13", 8, fill="#4b2a63")
b.rect(1.0, 26.0, 8.0, 12.0, "#ede4f5", "#6b3f8a", 1.4, rx=1)
b.text(5.0, 30.0, "TP8", 7.5, fill="#4b2a63")
b.text(5.0, 33.5, "TP10", 7.5, fill="#4b2a63")
b.text(5.0, 37.0, "TP11", 7.5, fill="#4b2a63")
b.rect(1.0, 12.0, 8.0, 9.0, "#f0e4f6", "#6b3f8a", 1.1, dash="2,2")
b.text(5.0, 17.0, "R12-14", 7.5, fill="#4b2a63")
b.rect(24.0, 2.0, 6.0, 3.0, "#f0e4f6", "#6b3f8a", 1.1, dash="2,2")
b.text(27.0, 4.1, "R15", 7, fill="#4b2a63")
b.rect(31.5, 10.0, 7.0, 3.0, "#f0e4f6", "#6b3f8a", 1.1, dash="2,2")
b.text(35.0, 12.1, "JP8", 7, fill="#4b2a63")

# =========================================================================
# 3 - SECTION
# =========================================================================
raw_text(1260, 46, "SECTION along the arm", 14, weight="bold")
s = View(1290, 96, 12.0, 8.0)

LID, AIR, PCB, UNDER, FLOOR = 1.5, 13.0, 1.6, 8.5, 1.5
z_pcb = LID + AIR
z_und = z_pcb + PCB
z_flr = z_und + UNDER
TOTAL = z_flr + FLOOR

s.rect(0, -WALL, LID, BOARD_H + 2 * WALL, "#d2d2d2", "#555", 1.1)
s.rect(LID, -WALL, TOTAL - LID - FLOOR, WALL, "#d2d2d2", "#555", 1.1)
s.rect(LID, BOARD_H, TOTAL - LID - FLOOR, WALL, "#d2d2d2", "#555", 1.1)
s.rect(z_flr, -WALL, FLOOR, BOARD_H + 2 * WALL, "#d2d2d2", "#555", 1.1)
s.rect(z_pcb, 0, PCB, BOARD_H, "#2b6b2b", "#194b19", 1.1)

s.rect(z_pcb - 8.5, PY + 1.6, 8.5, 2.54, "#fff", "#2a5d9f", 0.9)
s.rect(z_pcb - 8.5, PY + PH - 4.14, 8.5, 2.54, "#fff", "#2a5d9f", 0.9)
s.rect(z_pcb - 9.5, PY, 1.0, PH, "#2a5d9f", "#1d3f6e", 0.9)
s.text(z_pcb - 5.0, 35.0, "Pico, 12 tall", 8.5, fill="#1d3f6e", rot=-90)
s.rect(z_pcb - 0.8, AFE[1], 0.8, AFE[3], "#fdead0", "#c26a12", 1.0)
s.text(z_pcb - 2.2, 52.0, "AFE under it", 7.5, fill="#8a4a0a", rot=-90)
s.rect(z_pcb - 3.5, BOARD_H, 3.5, 2.4, "#8d8d8d", "#333", 1.0)
s.text(z_pcb - 6.5, 63.6, "USB", 8, anchor="start", fill="#333")
s.rect(z_pcb - 11, 2.0, 11, 5.0, "#fff", "#c26a12", 0.9)
s.text(z_pcb - 5.5, 4.9, "C4/C5", 7.5, fill="#8a4a0a")

s.rect(z_und, 0, 1.5, BOARD_H, "#f0eef6", "#b9b0cc", 0.9, dash="2,2")
s.text(z_und + 1.1, 12.0, "solder", 7, fill="#8a7fa8", rot=-90)
s.rect(z_und + 1.7, STACK[1], 5.0, STACK[3], "#efe6cd", "#8a7a55", 1.2)
s.text(z_und + 4.2, 31.0, "cell 5", 8, fill="#6e6040", rot=-90)
s.rect(z_flr - 1.6, STACK[1], 1.6, STACK[3], "#fbe7bd", "#b57a00", 1.2)
s.text(z_flr - 0.8, 31.0, "pad", 8, fill="#8a6000", rot=-90)
s.line(0, BOARD_H + 6.5, TOTAL, BOARD_H + 6.5, "#444", 1.1, arrow=True)
s.text(TOTAL / 2, BOARD_H + 9.5, "26.1 thick", 10, weight="bold")

out.append('</svg>')
svg = HW / "floorplan.svg"
svg.write_text("\n".join(out), encoding="utf-8")
try:
    import pymupdf
    doc = pymupdf.open(svg)
    pix = doc[0].get_pixmap(matrix=pymupdf.Matrix(2, 2))
    png = HW / "build" / "floorplan.png"
    pix.save(png)
    print("wrote %s and %s (%dx%d)" % (svg, png, pix.width, pix.height))
except Exception as exc:
    print("wrote %s (png skipped: %s)" % (svg, exc))
