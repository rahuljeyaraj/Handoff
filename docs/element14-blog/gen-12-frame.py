# Generates docs/element14-blog/12-frame.svg and .png
# One frame on the body link, redrawn from m1-walkthrough/07-frame.svg (its
# parts A, B and C; D, the slicer, is left out). Colours here mean the part of
# the frame, the same in all three rows, at the user's request: preamble blue,
# marker purple, header green, card amber, checksum red.
#
# Row 1: the five parts. Row 2: the chips where the preamble meets the marker.
# A chip is one tone or the other; 1 is tone B, 0 is tone A. 0xF0 in Manchester
# is 10 10 10 10 01 01 01 01, so its first seven chips look
# exactly like the preamble, and the only 00 is in its middle (frame.h's hunt).
# Row 3: what the receiver does, in order.

from figlib import Fig, need, solve, tw, INK, BLUE, GREEN, RED, GREY

PURPLE = ("#f1ebf8", "#6a3d9a", "#6a3d9a")
AMBER = ("#fdf3e1", "#a86b00", "#a86b00")

HEADS = ["The five parts of a frame, in the order they are sent",
         "Where the frame starts: those same bits, as chips",
         "What the receiver does"]

PARTS = [("Preamble", "32 chips: the two tones, turn and turn about", BLUE, 1.0),
         ("Marker", "Eight bits: 11110000", PURPLE, 0.9),
         ("Header", "Which frame, of how many, whose card", GREEN, 1.15),
         ("A piece of the card", "32 bytes, always. A short piece is padded.", AMBER, 1.5),
         ("Checksum", "Covers the header and the piece", RED, 1.15)]

# row 2: the chips, as (digit, style, kind). kind: "" plain, "gap" for the
# dots, "key" for the 00, "chk" for the seven checked, "hdr" for the header.
PRE = [("1", BLUE, ""), ("0", BLUE, "")] * 3 + [("…", None, "gap")]
MARK = ([("1", PURPLE, ""), ("0", PURPLE, "")] * 3 + [("1", PURPLE, ""), ("0", PURPLE, "key")]
        + [("0", PURPLE, "key")] + [("1", PURPLE, "chk"), ("0", PURPLE, "chk")] * 3
        + [("1", PURPLE, "chk")])
HDR = [("?", GREEN, "hdr"), ("?", GREEN, "hdr")]
CELLS = PRE + MARK + HDR
NOTES = {"key": "The only 00", "chk": "These 7 must read 1010101"}

STATES = [("Hunt", "Look for the 00", BLUE),
          ("Marker", "Check the next 7", PURPLE),
          ("Read", "Tone against tone, to the end", GREY),
          ("Checksum", "Good, or the frame is dropped", RED)]
EDGES = ["found", "ok", ""]
BACK = "Wrong: a false start. Hunt again."

W, M, VG, CG = 1600, 36, 40, 6


def build(fs):
    if any(tw(h, fs, True) > W - 2 * M for h in HEADS):
        return None
    head_h = fs * 1.9

    # row 1
    sg = 14
    total = sum(p[3] for p in PARTS)
    avail = W - 2 * M - sg * (len(PARTS) - 1)
    pws = [avail * p[3] / total for p in PARTS]
    ph = [need(t, b, w, fs) for (t, b, _, _), w in zip(PARTS, pws)]
    if None in ph:
        return None
    row1 = max(ph)

    # row 2
    cw = (W - 2 * M - CG * (len(CELLS) - 1)) / len(CELLS)
    if tw("0", fs, True) > cw - 8:
        return None
    ch = fs * 1.9
    lab_h = fs * 1.5
    note_h = fs * 1.5
    row2 = lab_h + ch + 26 + note_h

    # row 3
    eg = max(tw(e, fs) for e in EDGES) + 50
    sw = (W - 2 * M - eg * (len(STATES) - 1)) / len(STATES)
    sh = [need(t, b, sw, fs) for t, b, _ in STATES]
    if None in sh or tw(BACK, fs) > 2 * sw + eg:
        return None
    row3 = max(sh) + fs * 2.6

    H = M + 3 * head_h + row1 + row2 + row3 + 2 * VG + M
    f = Fig(W, fs)
    y = M

    # row 1: the five parts
    f.text(M, y + fs, HEADS[0], INK, bold=True, anchor="start")
    y += head_h
    x = M
    for (t, b, st, _), w in zip(PARTS, pws):
        f.box(x, y, w, row1, t, b, st)
        x += w + sg
    y += row1 + VG

    # row 2: the chips
    f.text(M, y + fs, HEADS[1], INK, bold=True, anchor="start")
    y += head_h
    xs = [M + i * (cw + CG) for i in range(len(CELLS))]
    spans = {}
    for i, (d, st, kind) in enumerate(CELLS):
        if st:
            spans.setdefault(st[1], []).append(i)
    for st, name in ((BLUE, "Preamble"), (PURPLE, "Marker"), (GREEN, "Header")):
        idx = spans[st[1]]
        cx = (xs[idx[0]] + xs[idx[-1]] + cw) / 2
        f.text(cx, y + fs, name, st[2], bold=True)
    cy = y + lab_h
    for x, (d, st, kind) in zip(xs, CELLS):
        if kind == "gap":
            f.text(x + cw / 2, cy + ch / 2 + fs * 0.35, d, INK, bold=True)
            continue
        solid = kind == "key"
        fill = st[1] if solid else st[0]
        f.rect(x, cy, cw, ch, fill, st[1], sw=2, rx=6)
        f.text(x + cw / 2, cy + ch / 2 + fs * 0.35, d, "#ffffff" if solid else st[2], bold=True)
    # notes under the strip, each centred on its group, with a tick up to it
    ny = cy + ch + 26
    for kind, text in NOTES.items():
        idx = [i for i, c in enumerate(CELLS) if c[2] == kind]
        a, b = xs[idx[0]], xs[idx[-1]] + cw
        col = CELLS[idx[0]][1][1]
        f.line([(a, cy + ch + 8), (b, cy + ch + 8)], col, sw=4)
        cx = (a + b) / 2
        wtxt = tw(text, fs, True)
        cx = min(max(cx, M + wtxt / 2), W - M - wtxt / 2)
        f.text(cx, ny + fs * 0.9, text, col, bold=True)
    y += row2 + VG

    # row 3: the receiver's steps
    f.text(M, y + fs, HEADS[2], INK, bold=True, anchor="start")
    y += head_h
    bh = max(sh)
    sxs = [M + i * (sw + eg) for i in range(len(STATES))]
    for x, (t, b, st) in zip(sxs, STATES):
        f.box(x, y, sw, bh, t, b, st)
    for i, e in enumerate(EDGES):
        x0, x1 = sxs[i] + sw + 4, sxs[i + 1] - 4
        f.arrow([(x0, y + bh / 2), (x1, y + bh / 2)], "#707070", head=14)
        if e:
            f.text((x0 + x1) / 2, y + bh / 2 - 14, e, INK)
    # the false start: from Marker back to Hunt, under the boxes
    mx, hx = sxs[1] + sw / 2, sxs[0] + sw / 2
    by = y + bh + fs * 1.1
    f.line([(mx, y + bh + 4), (mx, by)], PURPLE[1], sw=3, dash="8 6")
    f.line([(mx, by), (hx, by)], PURPLE[1], sw=3, dash="8 6")
    f.arrow([(hx, by), (hx, y + bh + 6)], PURPLE[1], sw=3, head=14, dash="8 6")
    f.text((mx + hx) / 2 + sw / 2 + eg / 2, by + fs * 1.25, BACK, PURPLE[2])
    return f, H


fig, H = solve(build, W)
fig.save("12-frame", H)
