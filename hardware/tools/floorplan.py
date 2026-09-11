"""Floor plan for the wristband board: emits hardware/floorplan.svg (and
build/floorplan.png via pymupdf). Not a layout - major parts only, in mm.

Frame: origin = elbow-end / thumb-side corner of the PCB, x toward the pinky
side, y toward the hand. Top view, lid off. The thumb-side wall is the one
that faces the sky during a right-hand shake.
"""
from pathlib import Path
import pymupdf

HW = Path(__file__).resolve().parent.parent
W, L = 54.0, 65.0                       # board, mm
BAT = (5.0, 5.0, 44.0, 55.0)            # KP384455 under the board, 5 mm ring around it
PAD = (14.5, 20.0, 25.0, 25.0)          # skin pad, centred under the cell
HOLES = [(3.5, 3.5), (W - 3.5, 3.5), (3.5, L - 3.5), (W - 3.5, L - 3.5)]
BOSS_R = 3.5                            # 7 mm boss / M3 head clearance
PICO = (1.7, 37.0, 51.0, 21.0)          # x, y, w, h ; USB end at +x (pinky wall)
PIN_Y_NEAR, PIN_Y_FAR = 37.0 + 1.61, 58.0 - 1.61


def pin_x(n):
    """Pin 1 at the USB end on the far row; pin 21 at the antenna end on the near row."""
    x0 = PICO[0] + 1.37
    return x0 + (n - 21) * 2.54 if n >= 21 else x0 + (20 - n) * 2.54


out = []


def Y(y):
    return L - y                        # flip: SVG y grows downward


def rect(x, y, w, h, style, r=0):
    out.append(f'<rect x="{x:.2f}" y="{Y(y + h):.2f}" width="{w:.2f}" height="{h:.2f}" rx="{r}" style="{style}"/>')


def circ(x, y, r, style):
    out.append(f'<circle cx="{x:.2f}" cy="{Y(y):.2f}" r="{r:.2f}" style="{style}"/>')


def text(x, y, s, size=1.6, anchor="middle", weight="normal", rot=0, fill="#222"):
    t = f' transform="rotate({rot} {x:.2f} {Y(y):.2f})"' if rot else ""
    out.append(f'<text x="{x:.2f}" y="{Y(y):.2f}" font-size="{size}" text-anchor="{anchor}" '
               f'font-weight="{weight}" fill="{fill}" dominant-baseline="middle"{t}>{s}</text>')


def part(x, y, w, h, name, note="", style="fill:#e8eef7;stroke:#2b4c7e;stroke-width:0.3", size=1.5):
    rect(x, y, w, h, style, r=0.4)
    text(x + w / 2, y + h / 2 + (0.9 if note else 0), name, size, weight="bold")
    if note:
        text(x + w / 2, y + h / 2 - 1.0, note, 1.1)


S_BOARD = "fill:#f6f3ea;stroke:#333;stroke-width:0.4"
S_UNDER = "fill:none;stroke:#888;stroke-width:0.3;stroke-dasharray:1.2,0.8"
S_PAD = "fill:none;stroke:#b8860b;stroke-width:0.35;stroke-dasharray:0.5,0.5"
S_HOLE = "fill:#fff;stroke:#333;stroke-width:0.3"
S_BOSS = "fill:#ddd;fill-opacity:0.6;stroke:#999;stroke-width:0.2;stroke-dasharray:0.6,0.4"
S_PICO = "fill:#dfe9df;stroke:#2f6f3f;stroke-width:0.35"
S_KEEP = "fill:#f3d9d5;stroke:#c0392b;stroke-width:0.3;stroke-dasharray:0.8,0.5"
S_CONN = "fill:#f4e6c8;stroke:#7a5a1e;stroke-width:0.3"
S_AFE = "fill:#fbe9e9;stroke:#a33;stroke-width:0.3;stroke-dasharray:1,0.6"
S_WALL = "fill:#cfc6b5;stroke:#6b5f4a;stroke-width:0.25"
S_CUT = "fill:#fff;stroke:#c0392b;stroke-width:0.3"
S_PIN = "fill:#fc3;stroke:#444;stroke-width:0.2"

# ---------- plan ----------
out.append('<g transform="translate(14,14)">')
G = 0.4                                  # board-to-wall gap
rect(-G - 1.8, -G - 1.8, W + 2 * G + 3.6, L + 2 * G + 3.6, S_WALL, r=2)   # 1.8 mm wall
rect(-G, -G, W + 2 * G, L + 2 * G, "fill:#fff;stroke:none", r=1)
rect(W + G - 0.1, 41.5, 2.0, 12.0, S_CUT)                     # USB cut-out, pinky wall
text(W + 5.5, 47.5, "USB cut-out", 1.4, rot=90)
rect(-G - 1.9, 12.5, 2.0, 8.0, S_CUT)                          # SW1 slot, thumb wall
rect(-G - 1.9, 27.5, 2.0, 5.5, S_CUT)                          # LED hole, thumb wall
rect(0, 0, W, L, S_BOARD, r=2)                                 # the board

# what is underneath
rect(*BAT, S_UNDER)
text(48.3, 22.0, "cell KP384455 44 x 55 under the board", 1.1, rot=-90, fill="#777")
for i, (hx, hy) in enumerate(HOLES, 1):
    circ(hx, hy, BOSS_R, S_BOSS)
    circ(hx, hy, 1.6, S_HOLE)
    text(hx + (4.5 if hx < W / 2 else -4.5), hy, f"H{i}", 1.2, fill="#666")

# Pico
px, py, pw, ph = PICO
rect(px, py, pw, ph, S_PICO, r=1)
rect(px, py, 10.0, ph, S_KEEP)                                 # antenna keep-out
text(px + 5, py + ph / 2 + 3, "antenna", 1.2, rot=-90, fill="#c0392b")
text(px + 5, py + ph / 2 - 4.5, "keep-out", 1.2, rot=-90, fill="#c0392b")
rect(px + pw - 0.8, py + 6.5, 2.1, 8.0, "fill:#bbb;stroke:#555;stroke-width:0.25")  # USB
text(px + pw - 5, py + ph / 2, "USB", 1.2, rot=-90)
for row_y in (PIN_Y_NEAR, PIN_Y_FAR):                          # socket strips
    rect(px + 0.1, row_y - 1.27, pw - 0.2, 2.54, "fill:#333;stroke:none")
text(px + pw / 2, py + ph / 2 + 1.2, "U1  Pico 2 W  (socketed, top of stack ~10 mm)", 1.6, weight="bold")
text(px + pw / 2, py + ph / 2 - 1.6, "near row = pins 21-40  .  far row = pins 1-20", 1.2)
for n, lbl in [(21, "GP16"), (22, "GP17"), (24, "GP18"), (31, "ADC0"), (33, "AGND"), (36, "3V3"), (39, "VSYS")]:
    text(pin_x(n), PIN_Y_NEAR + 2.0, lbl, 1.0, fill="#2f6f3f")
    circ(pin_x(n), PIN_Y_NEAR, 0.45, "fill:#fc3;stroke:none")
for n, lbl in [(4, "GP2"), (19, "GP14"), (20, "GP15")]:
    text(pin_x(n), PIN_Y_FAR - 2.0, lbl, 1.0, fill="#2f6f3f")
    circ(pin_x(n), PIN_Y_FAR, 0.45, "fill:#fc3;stroke:none")

# hand-end strip: SW2, R15, JP8
part(24, 58.5, 6, 6, "SW2", "lid hole", size=1.4)
part(33, 59.5, 5, 3.5, "R15", size=1.2)
part(39, 59.5, 4, 3.5, "JP8", size=1.2)

# thumb wall: SW1 and the LED
sw_y = 16.5
rect(0.5, sw_y - 4.35, 5.5, 8.7, S_CONN, r=0.3)                # body
rect(-5.5, sw_y - 1.0, 6.0, 2.0, "fill:#999;stroke:#444;stroke-width:0.25")  # handle through the wall
for dy in (-3, 0, 3):
    circ(9.3, sw_y + dy, 0.5, S_PIN)
for dy in (-6.05, 6.05):
    circ(9.3, sw_y + dy, 0.6, S_HOLE)
text(3.3, sw_y, "SW1", 1.4, weight="bold", rot=-90)
text(12.5, sw_y + 0.9, "ON/OFF, handle", 1.1, anchor="start")
text(12.5, sw_y - 0.9, "through the wall", 1.1, anchor="start")
led_y = 30.0
part(0.6, led_y - 6.25, 5.75, 12.5, "J3", size=1.3)
for dy in (-3.75, -1.25, 1.25, 3.75):
    circ(3.5, led_y + dy, 0.45, "fill:#fc3;stroke:none")
circ(-4.0, led_y, 2.5, "fill:#e88;stroke:#a33;stroke-width:0.3")
text(-4.0, led_y, "D2", 1.2)
text(8.5, led_y + 1.0, "RGB LED in J3, legs bent 90°,", 1.1, anchor="start")
text(8.5, led_y - 0.8, "lens through the thumb wall", 1.1, anchor="start")

# elbow edge: cell wires, charger, D1, pad connector
for x, lbl in ((10.5, "BAT-"), (13.0, "BAT+")):
    circ(x, 3.0, 0.9, S_PIN)
    text(x, 5.5, lbl, 0.9)
text(11.75, 1.0, "J1 cell wires", 0.9)
part(15.5, 0.6, 7.5, 5.75, "J5", "CHG only", size=1.2)
rect(13.5, 8.2, 8.2, 2.6, "fill:#444;stroke:none")            # D1 body
for x in (12.5, 22.7):
    circ(x, 9.5, 0.6, S_PIN)
text(17.6, 9.5, "D1", 1.2, fill="#fff", weight="bold")
part(24.0, 8.0, 4.0, 3.0, "JP5", size=1.1)
part(30.0, 0.6, 7.5, 5.75, "J2", "PAD", size=1.2)
circ(39.5, 2.2, 1.5, S_HOLE)
text(39.5, 4.9, "wire", 0.9)
text(39.5, 3.9, "pass", 0.9)

# analogue front end
rect(24.0, 12.0, 22.0, 24.0, S_AFE, r=1)
text(35.0, 34.0, "analogue front end", 1.4, weight="bold", fill="#a33")
text(35.0, 32.2, "R1-R11 C1-C3 C6 JP1-4 JP6 JP7 TP1-7 TP12", 1.0, fill="#a33")
part(31.5, 17.0, 3.5, 3.5, "U2", size=1.2)
circ(29.6, 18.75, 0.9, "fill:#fff;stroke:#a33;stroke-width:0.35;stroke-dasharray:0.4,0.3")
text(28.6, 15.4, "10 MΩ island", 1.0, fill="#a33", anchor="start")
text(28.6, 14.2, "R2 R3 at pin 3", 1.0, fill="#a33", anchor="start")
circ(42.5, 16.5, 2.5, S_CONN)
text(42.5, 16.5, "C5", 1.1)
circ(42.5, 23.0, 2.5, S_CONN)
text(42.5, 23.0, "C4", 1.1)
part(25.0, 26.0, 9.0, 4.5, "R9 JP1 JP3", "TP2-4 TP13", size=1.0)

# expansion header on the pinky wall
rect(50.1, 8.5, 2.54, 25.4, "fill:#333;stroke:none")
text(51.4, 21.2, "J4  1x10", 1.2, rot=-90, fill="#fff", weight="bold")

# skin pad, drawn last so its outline stays visible through everything above it
rect(*PAD, S_PAD)
text(10.0, 23.0, "skin pad 25 x 25 under the cell (dotted)", 1.1, anchor="start", fill="#b8860b")

# orientation labels
text(W / 2, L + 6.0, "HAND END", 1.8, weight="bold")
text(W / 2, -4.6, "ELBOW END", 1.8, weight="bold")
text(-8.5, L / 2, "THUMB WALL  -  faces UP in a handshake", 1.8, weight="bold", rot=-90)
text(W + 9.0, L / 2 - 14, "PINKY WALL  -  faces down", 1.8, weight="bold", rot=90)
text(W / 2, L + 3.6, "Pico antenna toward the thumb wall, USB out the pinky wall", 1.2, fill="#555")
out.append('</g>')

# ---------- section ----------
out.append('<g transform="translate(84,16)">')
text(20, L + 4.2, "STACK  (section, z not to scale)", 1.8, weight="bold")
layers = [  # name, drawn thickness, colour, note
    ("lid  (3D print)", 2.0, "#cfc6b5", "hole for SW2; LED hole + SW1 slot in the thumb wall"),
    ("Pico on 8.5 mm sockets", 10.0, "#dfe9df", "sets the cavity: ~11 mm clear above the PCB"),
    ("PCB  1.6 mm", 2.0, "#f6f3ea", "top pour = GROUND-PLANE electrode, facing the room"),
    ("cell  KP384455  ~4.5 mm", 4.5, "#e8eef7", "spacer between the two electrodes"),
    ("skin PAD  copper-clad + tape", 2.0, "#f4e6c8", "copper up, tape on the skin side (mandatory)"),
    ("bottom  (3D print)", 2.0, "#cfc6b5", "holds the pad by its edges, open to the skin"),
]
y = L - 2
for name, t, col, note in layers:
    y -= t + 0.8
    rect(0, y, 40, t, f"fill:{col};stroke:#555;stroke-width:0.3")
    text(20, y + t / 2, name, 1.3, weight="bold")
    text(42, y + t / 2, note, 1.1, anchor="start", fill="#555")
text(20, y - 2.5, "≈ 22 mm overall  .  ≈ 58 x 69 outside the walls", 1.2, fill="#555")
y -= 8
for i, s in enumerate([
    "Cell and pad wires rise through the board inside the 5 mm ring",
    "around the cell (J1 holes, J2 pass-through). Pad wire under 5 cm.",
    "",
    "Right-hand shake, thumb up: the box rolls 90° and the thumb wall",
    "faces the sky: LED and SW1 there. SW2 through the lid. J5 inside.",
    "",
    "Bosses (7 mm) sit in the ring at the corners; nothing else within",
    "3.5 mm of a hole. Op-amp in the elbow half, 20 mm from the Pico,",
    "as far from the antenna as the board allows.",
]):
    text(0, y - 2.2 * i, s, 1.2, anchor="start")
out.append('</g>')

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 175 96" width="1750" height="960"
 font-family="Segoe UI, Helvetica, Arial, sans-serif">
<rect width="175" height="96" fill="#fff"/>
<text x="14" y="5" font-size="2.6" font-weight="bold">Handoff wristband - floor plan (top view, lid off, mm)</text>
{chr(10).join(out)}
</svg>'''
(HW / "floorplan.svg").write_text(svg, encoding="utf-8")
doc = pymupdf.open(HW / "floorplan.svg")
pix = doc[0].get_pixmap(matrix=pymupdf.Matrix(1.2, 1.2))
(HW / "build").mkdir(exist_ok=True)
pix.save(HW / "build" / "floorplan.png")
print("wrote floorplan.svg,", pix.width, "x", pix.height)
