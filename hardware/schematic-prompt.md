# Prompt for the next session — bring the schematic up to the floor plan, then lay out

Copy everything below the line into the new chat.

---

You are a senior PCB designer. The schematic for this board was reviewed and
signed off (`hardware/review.md`), and the floor plan has since been settled
(`hardware/floorplan.svg`). The floor plan changed decisions the schematic still
encodes. Your job is to make the schematic match, then start the layout.

Read first: `hardware/README.md`, `hardware/review.md`, `hardware/floorplan.svg`
(render it or read `hardware/tools/gen_floorplan.py`, which is its source), and
design §7, §8, §12, §15 in `docs/body-coupled-handshake-design.md`.

**The generator is the schematic.** Edit `hardware/tools/gen_schematic.py`, never
the `.kicad_sch`. After every change re-run it; it must end:

```
ERC: 0 violation(s), 0 error(s)
netlist: NN nets, 0 missing, 0 unexpected
BOM: ... README table diff: 0 difference(s)
```

Update `EXPECTED_NETS` as part of each change — it is the oracle, never delete or
weaken it. `python hardware/tools/render.py` gives you `build/sheet.png` to look
at. Same for the floor plan: edit `tools/gen_floorplan.py`, and keep that sheet
**diagrams only — no prose, no header, no footer.**

Nothing on this board has ever been tested. Every stage must still be isolatable
and probeable on the bench.

## The floor plan, in one paragraph

PCB **40 × 62 mm**, 3 mm rounded corners. Short side across the arm, long side
along it. Hand at the top of the drawing, elbow at the bottom; **the strap leaves
the two long walls, left and right** — it encircles the wrist, so it cannot leave
the hand and elbow ends. The left wall is the thumb wall, the one that points at
the sky during a right-hand handshake: **D2 near the hand end, SW1 near the
elbow**, strap lug between them. The Pico is centred across the board with its
**USB overhanging the elbow edge**, which puts the antenna keep-out at the hand
end. Cell and pad share one 20 × 30 footprint, **stacked** (cell over pad) and
centred, in one four-sided pocket in the base. J1, J2 and J5 sit together on the
little-finger strip because all their wires drop to that centre stack. SW2 is
centred at x = 20. The AFE is SMD on the **top** face in the 10 mm channel under
the Pico; the jumpers and test pads are on the **bottom** face so they stay
reachable with the Pico socketed.

## 1. Schematic changes the floor plan forces

Do these, each with its `EXPECTED_NETS` update, README edit and a re-run:

1. **Move the RGB LED to GP14 / GP13 / GP12.** J3 sits hard against the left
   edge, parallel to it, opposite row A. With R → GP14 (pin 19), K → GND
   (pin 18), G → GP13 (pin 17), B → GP12 (pin 16), J3's four pins face pins
   16–19 straight across: four ~7 mm traces, no crossings, and the LED's legs
   bend 90° straight into the header with no pigtail. R12–R14 stay 330 Ω.
2. **Re-home the two nets that displaces.** `ROLE` was GP14 and `BTN` was GP15.
   Recommended: `BTN` → GP16 (pin 21), `ROLE` → GP17 (pin 22) — both land beside
   SW2 and JP8 on the floor plan. Check the alternative (GP22 / GP19) before you
   commit; see the keep-out warning in §3.
3. **Consider moving TX off GP2.** The README already says E9 affects every
   bank-0 pin equally, so GP2 is not special. GP11 (pin 15) or GP10 (pin 14) sit
   right beside the R2/R3 island, which lets R1 butt up to the island with a
   short perpendicular entry instead of a 24 mm run alongside it. If you move it,
   every GP2 reference in the README's firmware notes moves with it.
4. **J4 stops being a connector.** It becomes ten separate through-hole breakout
   pads, one placed inboard of its own Pico pin: 3V3 (36), RUN (30), GP0 (1),
   GP1 (2), GP4 (6), GP5 (7), GP20 (26), GP21 (27), GP27 (32), GND (3). The nets
   are unchanged; the symbol and footprint are not. Decide how to represent it —
   ten `TestPoint`-style single pads is probably cleaner for the netlist oracle
   than a 1×10 symbol with a scattered footprint — and say why in the README.
   Drop the 1×10 female header from the parts table; the 1×40 strip is now only
   the Pico's two 1×20s.
5. **D1 moves to the 7.62 mm horizontal footprint.** The 10.16 mm one does not
   fit the 9.5 mm strip between J5 and the corner boss. This is now a blocking
   physical check, not a nicety — see §2.
6. **Mounting holes: M3, 3.4 mm drill** (was 3.2), unplated, no copper tie, with
   an **8 mm keep-out** at each corner for the boss that takes a 5 mm brass
   insert. Four of them, at (4.5, 4.5), (35.5, 4.5), (4.5, 57.5), (35.5, 57.5).

Also settle the two value changes the review left with the owner, since both are
value-only swaps on existing 1206 pads: **R3 10 MΩ → 1 MΩ** and
**C1 100 nF → 330 pF**. Ask once, then apply the answer.

## 2. Confirm on a physical part before you lay anything out

Carried from `review.md` §5, with one promoted:

- **1N5819 body length against the 7.62 mm footprint.** Blocking now. If the
  owner's diodes are the long DO-41 variant, D1 has to move to the hand strip and
  SW2 gives up being centred — decide that before routing, not after.
- SS-12F23G5 body-to-pin-row offset, nominally 3.3 mm, read off a small drawing.
  SW1's pin row at x = 8.8 leaves **2.3 mm to the Pico's header row** — the
  tightest clearance on the board, and the first thing that breaks if this
  dimension is wrong.
- JST-XH 2.50 mm row pitch against the 5 mm LED's 2.54 mm legs, trial-fit in a
  spare header.
- C4/C5 lead pitch 2.5 mm, body 5 mm dia.
- MCP6292 pin-1 mark on the real MSOP-8 against the footprint's chamfer.

## 3. Write these into the README before layout starts

They are layout rules the floor plan created, and three of them contradict rules
already in the README. Say so plainly rather than quietly overwriting:

- AFE SMD on the **top** face in the channel under the Pico; jumpers and test
  pads on the **bottom** face.
- **No parts on the bottom face over the cell/pad footprint** — it sits ~0.5 mm
  below the board, so through-hole protrusions there need the 1.5 mm of solder
  clearance the section drawing allows.
- **No bottom-side pour over the pad.** The TOP pour is the ground-plane
  electrode.
- **Antenna keep-out at the hand end**: no copper under it, thin escapes only for
  pins 16–24, which is where the LED and (if you take the recommendation) BTN and
  ROLE now live.
- **Design §12.3 is no longer met.** "Op-amp far from the Pico and its antenna" —
  the AFE is now directly under the Pico, because once the breakout pads are in
  that channel is the only contiguous area left. JP4 and the M7 bench-supply
  comparison are how you find out whether it costs anything. Record it as a known
  compromise, not an oversight.
- **Stacking the pad under the cell restores roughly 16 pF of pad-to-return
  shunt**, the figure design §8.2 rejects for a two-sided board. The owner chose
  the stack for a single four-sided pocket. 2–3 mm of foam between cell and pad
  is the mitigation; flag it to the owner and record the decision either way.
- The cell sits partly under the antenna keep-out. Nothing else fits there once
  the pad is centred. If BLE range disappoints, the fix is a smaller pad, not a
  smaller cell.

## 4. Deliverables

1. Regenerated schematic, generator, `EXPECTED_NETS`, README parts table,
   deviations and firmware notes — ERC clean, netlist check passing, BOM diffed
   against the README table at 0 differences.
2. `hardware/floorplan.svg` regenerated if anything moved, still diagrams only.
3. A short changelog appended to `hardware/review.md`: what changed, why, and
   what it cost. Be blunt about the three compromises in §3.
4. Then, and only then, start the layout: board outline, stackup, placement from
   the floor plan, pour, route.

Housekeeping: `hardware/floorplan.svg` and `hardware/tools/gen_floorplan.py` are
untracked — commit them. `hardware/tools/floorplan.py` is a stray earlier draft
with a different coordinate frame; delete it or keep it deliberately, but do not
leave two floor-plan generators in the tree.
