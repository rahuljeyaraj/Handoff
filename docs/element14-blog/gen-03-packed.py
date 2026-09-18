# Generates docs/element14-blog/03-packed.svg and .png
# Rohit's card as vCard text, beside what his band actually stores.
# The details are made up for the figure; edit ROWS to change them.
#
# Left: each vCard line, the common parts in grey and Rohit's own details in
# blue, his colour in every figure. Right: the same line packed, one small box per tag or byte, or "left
# behind" when the whole line is common parts.

from figlib import Fig, tw, BLUE, GREY, INK, MUTED

# (left segments as (text, own?), right cells as (text, kind) or None)
ROWS = [
    ([("BEGIN:VCARD", False)], None),
    ([("VERSION:3.0", False)], None),
    ([("FN:", False), ("Rohit Menon", True)],
     [("name", "tag"), ("Rohit Menon", "text")]),
    ([("TEL;TYPE=CELL:", False), ("+91 98765 43210", True)],
     [("phone", "tag"), ("mobile", "byte"), ("+91", "byte"), ("98", "byte"),
      ("76", "byte"), ("54", "byte"), ("32", "byte"), ("10", "byte")]),
    ([("EMAIL:", False), ("rohit.menon@gmail.com", True)],
     [("email", "tag"), ("rohit.menon", "text"), ("gmail.com", "byte")]),
    ([("END:VCARD", False)], None),
]
HEAD = ("As text, from his phone", "Packed, on his band")
GONE = "left behind"
KEY = (("the common parts", GREY), ("Rohit's details", BLUE))

FS = 34
M, PAD, GAPC, CELLGAP = 36, 26, 150, 10


def cell_w(s, fs):
    return tw(s, fs, True) * 1.08 + 30


def build(fs):
    rh = fs * 1.9
    left_w = max(tw("".join(t for t, _ in segs), fs, mono=True) for segs, _ in ROWS) + 2 * PAD
    right_cells = [sum(cell_w(t, fs) if k != "text" else tw(t, fs, True) * 1.08 + 16 for t, k in cells)
                   + CELLGAP * (len(cells) - 1) for _, cells in ROWS if cells]
    right_w = max(right_cells + [tw(h, fs, True) for h in HEAD]) + 2 * PAD
    left_w = max(left_w, tw(HEAD[0], fs, True) + 2 * PAD)
    W = round(M + left_w + GAPC + right_w + M)

    f = Fig(W, fs)
    lx, rx = M, M + left_w + GAPC
    y = M
    f.text(lx + left_w / 2, y + fs, HEAD[0], INK, bold=True)
    f.text(rx + right_w / 2, y + fs, HEAD[1], INK, bold=True)
    y += fs * 1.9
    top = y
    f.rect(lx, top, left_w, rh * len(ROWS) + PAD, "#fafafa", GREY[1])
    f.rect(rx, top, right_w, rh * len(ROWS) + PAD, "#fafafa", GREY[1])
    y += PAD / 2
    for segs, cells in ROWS:
        base = y + rh / 2 + fs * 0.35
        x = lx + PAD
        for t, own in segs:
            f.text(x, base, t, BLUE[1] if own else "#9a9a9a", bold=own,
                   anchor="start", mono=True)
            x += tw(t, fs, mono=True)
        ay = y + rh / 2
        if cells is None:
            f.arrow([(lx + left_w + 14, ay), (rx - 14, ay)], "#b0b0b0", dash="10 9")
            f.text(rx + PAD, base, GONE, MUTED, italic=True, anchor="start")
        else:
            f.arrow([(lx + left_w + 14, ay), (rx - 14, ay)], BLUE[1])
            cx = rx + PAD
            ch = fs * 1.45
            for t, k in cells:
                if k == "text":
                    f.text(cx + 5, base, t, BLUE[1], bold=True, anchor="start")
                    cx += tw(t, fs, True) * 1.08 + 16 + CELLGAP
                    continue
                w = cell_w(t, fs)
                if k == "tag":
                    f.rect(cx, ay - ch / 2, w, ch, "#555555", "#555555", rx=6)
                    f.text(cx + w / 2, base, t, "#ffffff", bold=True)
                else:
                    f.rect(cx, ay - ch / 2, w, ch, BLUE[0], BLUE[1], rx=6)
                    f.text(cx + w / 2, base, t, BLUE[1], bold=True)
                cx += w + CELLGAP
        y += rh
    y = top + rh * len(ROWS) + PAD + fs * 1.1
    kx = M
    for label, st in KEY:
        f.rect(kx, y, fs * 1.1, fs * 1.1, st[1] if st is BLUE else "#b0b0b0",
               st[1] if st is BLUE else "#b0b0b0", rx=5)
        f.text(kx + fs * 1.5, y + fs * 0.9, label, INK, anchor="start")
        kx += fs * 1.5 + tw(label, fs) + fs * 1.6
    f.rect(kx, y, fs * 1.8, fs * 1.1, "#555555", "#555555", rx=5)
    f.text(kx + fs * 2.2, y + fs * 0.9, "a one-byte tag for what the line is",
           INK, anchor="start")
    H = y + fs * 1.1 + M
    return f, W, H


fig, W, H = build(FS)
fig.save("03-packed", H)
