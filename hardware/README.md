# Handoff — PCB

KiCad 10 project for the wristband board. Companion to
[docs/body-coupled-handshake-design.md](../docs/body-coupled-handshake-design.md);
the schematic implements its §6/§7 netlist with the changes listed under
*Deviations* below.

**Sponsor:** [PCBWay](https://www.pcbway.com/) is fabricating this board.

| File | What |
|---|---|
| `handoff.kicad_pro` / `handoff.kicad_sch` | the project and its single schematic sheet |
| `handoff.kicad_sym` | project symbol library: Pico 2 W and MCP6292 (the stock library has neither) |
| `handoff.pretty/` | project footprints: the SS-12F23G5 slide switch and the 3.4 mm M3 mounting hole with its boss keep-out |
| `tools/gen_schematic.py` | **the source of the schematic.** Generates the sheet from the §7 netlist and proves it with `kicad-cli`: ERC, the netlist against `EXPECTED_NETS` pin for pin, and the BOM against the parts table below. Edit this, not the sheet |
| `tools/render.py` | renders `build/handoff.pdf` to `build/sheet.png` so the sheet can be looked at |
| `review.md` | the pre-layout review: every check, its number, its verdict |
| `schematic-prompt.md` | the brief for the next session: the schematic changes the floor plan forces, then layout |
| `tools/gen_sw_footprint.py` | writes `handoff.pretty/SW_Slide_SS-12F23G5`, geometry measured on the physical part |
| `tools/gen_mount_footprint.py` | writes `handoff.pretty/MountingHole_3.4mm_M3_Boss8mm`: 3.4 mm unplated drill plus the 8 mm boss keep-out |
| `tools/gen_floorplan.py` / `floorplan.svg` | the floor plan: which block sits where in the enclosure, and why. Placement only, no tracks — the input to the layout |
| `tools/gen_pcb.py` / `handoff.kicad_pcb` | **the source of the board.** Builds it from the schematic's netlist and the floor plan, then proves it with `kicad-cli`: DRC, and schematic parity pin for pin. Edit this, not the `.kicad_pcb` |
| `handoff.kicad_dru` | the two custom DRC rules, both about height, which KiCad does not model. Written by `gen_pcb.py` |

Regenerate and verify after any change (must end `ERC: 0 violation(s), 0 error(s)`,
`0 missing, 0 unexpected` nets and `0 difference(s)` against the parts table):

```
python hardware/tools/gen_schematic.py && python hardware/tools/render.py
```

The board is regenerated separately, with KiCad's own Python (it needs `pcbnew`):

```
"C:/Program Files/KiCad/10.0/bin/python.exe" hardware/tools/gen_pcb.py
```

It must end `DRC: 0 error(s)` apart from the unrouted count, and `parity 0`.
Run `gen_schematic.py` first: the board is built from `build/handoff.net`.

---

## Parts — as ordered from robu.in

Every part below is the exact SKU on order. Do not substitute a package
without changing the footprint.

| Ref | Part | Package / mounting | Robu SKU | Qty |
|---|---|---|---|---|
| U1 | Raspberry Pi Pico 2 W | **socketed** in two 1×20 female strips, not soldered to the board | R190344 | 1 |
| — | 2.54 mm 1×40 female single-row header | cut into two 1×20 for the Pico; nothing else uses it now | 555698 | 1 strip |
| U2 | MCP6292-E/MS dual op-amp, 10 MHz | MSOP-8, soldered directly to the board (no DIP adapter on the PCB) | R193529 | 1 |
| R1, R2, R3 | 1 MΩ | 1206 | 574983 | 3 |
| R4, R6, R7, R10, R11 | 100 kΩ | 1206 | 574955 | 5 |
| R5, R8, R15 | 10 kΩ 1 % (Yageo RC1206FR-0710KL) | 1206 | R137556 | 3 |
| R9 | 1.5 kΩ 1 % (Yageo RC1206FR-071K5L) | 1206 | R137563 | 1 |
| R12, R13, R14 | 330 Ω | 1206 | 575088 | 3 |
| C3 | 100 nF X7R 50 V (TCC1206X7R104J500DT) | 1206 | R153721 | 1 |
| C1, C2 | 330 pF C0G/NP0 50 V (KEMET C1206C331J5GACTU) | 1206 | R111869 | 2 |
| C4, C5 | 10 µF 63 V electrolytic | radial through-hole, 5 mm dia, 2.54 mm lead pitch (KiCad `CP_Radial_D5.0mm_P2.50mm`) | 1090083 | 2 |
| C6 | **DNP** — 330 pF C0G, the C2 part, fitted only if GP11 leakage turns out to matter (see *Deviations*) | 1206 | R111869 | 0 |
| D1 | 1N5819 Schottky 40 V 1 A | DO-41, horizontal, **7.62 mm** lead pitch (body measured against it, 11 Sep 2026) | R241509 | 1 |
| SW1 | SS-12F23G5 slide switch, SPDT (1P2T), right-angle, 5 mm handle | 3 terminals at 3.0 mm pitch + 2 mounting ears, `handoff:SW_Slide_SS-12F23G5` | R132611 | 1 |
| D2 | RGB LED, common cathode, 5 mm, clear (5-pack) | off-board: solders into J3 or plugs in via a 4-pin XH pigtail | R183455 | 1 |
| SW2 | Tactile push button 6 × 6 × 5 mm, 4 legs | through-hole, `Button_Switch_THT:SW_PUSH_6mm` | 618182 | 1 |
| J1 | JST-XH 2.54 straight 2-pin male (battery) | through-hole | — | 1 |
| J5 | JST-XH 2.54 straight 2-pin male (charger — **one more 2-pin header than before**) | through-hole | — | 1 |
| J2 | JST-XH 2.54 straight 2-pin male (electrodes) | through-hole | — | 1 |
| J3 | JST-XH 2.54 straight 4-pin male (LED), or the LED soldered straight in | through-hole | — | 1 |
| BT1 | KP384455 Li-ion 3.7 V 1500 mAh | off-board, on a JST-XH pigtail | — | 1 |

Copper only, nothing to buy: 8 solder jumpers JP1–JP8, 10
expansion breakout pads E1–E10, and 4 mounting holes H1–H4 (M3 clearance, 3.4 mm,
unplated, each with an 8 mm boss keep-out).

The 10 MΩ (Robu 574992) bought for R3 is no longer fitted — see *Deviations*. Keep
it: it is the A/B part for the M7 recovery-versus-loss comparison.

Not on the board, by decision: the TP4056 charger (the cell is charged off-board)
and the MSOP-to-DIP adapter (perfboard only).

> JST "XH" is a 2.50 mm pitch family, not 2.54. Over four pins the difference is
> 0.12 mm and the footprint holes absorb it, so the 5 mm LED's 2.54 mm legs go
> straight into J3.

---

## Deviations from the design doc, and why

**D1 in series with VSYS.** The Pico datasheet's own recommendation for a board
that is battery powered but still gets plugged into USB for flashing: the Pico's
internal Schottky ORs VBUS into VSYS, and this external one stops VSYS from
back-driving the battery. It also makes a reversed battery plug harmless (nothing
powers up) instead of destructive. Cost: ~0.35 V, leaving 2.6–3.8 V on VSYS
against a 1.8–5.5 V input range.

**Battery polarity on J1: pin 1 = BAT−, pin 2 = BAT+.** There is no universal
convention for a 2-pin JST battery plug; the one real XH convention is the
balance-lead one, where pin 1 (square pad) is the negative end. The silkscreen
will say so. Verify the pigtail against the silkscreen before the first plug-in —
D1 means getting it wrong costs nothing but a re-pinned plug.

**RGB LED on GP17 / GP18 / GP19 (R / G / B) through 330 Ω.** J3 pin order is
R, K, G, B — the lead order of a standard 5 mm common-cathode RGB LED, longest
leg second — so the LED can be soldered directly. At 3.3 V and 330 Ω the red
runs ~4 mA and green/blue ~1 mA: a status indicator, not a torch.

*Moved one pin up from GP16/17/18 by the floor plan.* J3 sits hard against the
thumb-side edge, parallel to it, opposite the Pico's **thumb row — which is pins
21–40**, not 1–20. With R → GP17 (pin 22), K → GND (pin 23), G → GP18 (pin 24),
B → GP19 (pin 25), J3's four pins face pins 22–25 straight across: four ~7 mm
traces, no crossings, and the LED's legs bend 90° straight into the header with
no pigtail. The cathode lands on a Pico GND pin that is already there, so the
LED's return does not travel.

**Which row is which.** The Pico sits at 180°: USB overhanging the elbow edge
puts the antenna keep-out at the hand end, and it puts **pins 1–20 on the
little-finger row and pins 21–40 on the thumb row.** The floor plan had these
mirrored until the layout session checked the footprint's own pad coordinates
against its USB and antenna zones. Everything that names a Pico pin and a board
position — J3, the breakout pads, the TX escape, BTN and ROLE — depends on
getting this right; it is the cheapest thing on the board to verify and the most
expensive to get wrong.

**J2 carries both electrodes: pin 1 = PAD, pin 2 = ground-plane electrode.**
The ground plane does *not* need air contact; it needs area and distance from
the PAD. Inside a box, the board's own ground pour (plus the Pico's) already is a
ground plane facing outward. J2 pin 2 exists so a separate copper-clad sheet can
be added on the outer face of the enclosure — design §15.1 — if measurement
shows the board's pour is too small. Exactly one wire, per §8.4; the connector
is that one point.

**Solder jumpers, all normally OPEN.** Two copper pads 0.3 mm apart, no part;
bridge with a solder blob to connect, wick it off to isolate. Open rather than
cut-to-open because a blob is reversible and a knife cut is not. The price:
**the board does nothing as delivered until the in-line jumpers are bridged**,
which is deliberate — nothing on this board has been tested, so every stage can
be isolated and driven from a test pad on its own. An open jumper is two
separate nets in the netlist oracle.

| | in series with | open, it isolates… | …and the isolated side is driven from | first used |
|---|---|---|---|---|
| JP5 | D1 cathode → VSYS | the whole board from the cell (USB still powers it) | an ammeter across JP5's own two pads reads the board current; a bench supply on pad 2 replaces the cell | M3 bring-up, M11 battery life |
| JP4 | 3V3 → U2 VDD (and the VREF divider) | the analogue side from the Pico's rail | a bench supply / second cell on JP4 pad 2 — the SMPS-noise experiment; an ammeter across JP4 reads U2's current | M7 |
| JP6 | R10/R11/C4 divider → VREF | the bias divider from the amplifiers | an external 1.65 V on JP6 pad 2 | M7 if VREF is suspected |
| JP7 | PAD → R2 | the pad and the transmitter from the receiver | a signal generator on JP7 pad 2 (through R2: the 1 MΩ is still in circuit) | M7 gain / corner / clipping sweeps |
| JP2 | GP11 → R1 | the transmitter from the pad | nothing — leave open and fit C6 instead if GP11 leakage biases the pad (moving TX to another GPIO does not help: every bank-0 pin has E9) | M3 (measure leakage first), M8 |
| JP3 (3-way) | OUT1 / OUT2 → R9 | the ADC from both stages; centre pad → R9, pad 3 ← OUT2 (×121), pad 1 ← OUT1 (×11) | JP3's own pad 3 or pad 1 as a bare output for the scope | M7: blob to pad 3; move it to pad 1 if a noisy hall clips stage 2 (design §15.2) |
| JP1 | R9 → GP26/ADC0 | the ADC pin from the amplifier | JP1 pad 2 directly, as M5 does with its own attenuator; a wire from JP1 pad 2 to any Pico GND pin grounds the ADC for the M4 noise floor | M4/M5 open, M7 bridged |
| JP8 | GP14 → GND | (a strap, not in-line) | — | M6: bridge on one board to set its role; firmware enables GP14's pull-up |

Nothing touches the 10 MΩ node (R2/R3/U2 pin 3): no jumper, no test pad. JP7
is on the *pad* side of R2.

**Bring-up order** — the sequence that makes the jumpers pay for themselves:

1. Nothing bridged, no cell. USB in: the Pico runs, M3–M5 need nothing else
   (M5's loopback goes JP2 pad 1 → JP1 pad 2 by wire; JP1 pad 2 to a Pico GND
   pin grounds the ADC for M4).
2. Bridge **JP4** and **JP6**. Read 3.3 V on JP4 pad 2, 1.65 V ±5 % on JP6
   pad 2, 1.65 V on JP3 pads 1 and 3. Anything else on JP3 is a bias problem:
   open JP6 and inject 1.65 V on its pad 2 to split divider from amplifier.
3. Bridge **JP3 centre–pad 3**. Inject on JP7 pad 2 (JP7 still open): gain,
   corner, clipping and noise sweeps of M7, reading JP3 pad 3 with a scope and
   the ADC after bridging **JP1**.
4. Bridge **JP7**. Now the pad is live; M8 with a capacitor from J2 pin 1 to a
   Pico GND pin.
5. Bridge **JP2** *only after* the M3 leakage measurement (below) says the
   pad node stays at 1.65 V with GP11 high-Z. If it does not, fit **C6** instead.
6. Bridge **JP5** last, with the cell on J1; first reading of J1 pin 2 against
   JP5 pad 2 is the D1 drop.

**There are no test pads.** There were thirteen; every one of them sat on the
same net as a solder-jumper pad, a connector pin or a Pico header pin two
millimetres away, and the owner probes with a meter or a scope tip rather than
a clip. Thirteen through-hole pads were removed for that, and the board went
from 17 unroutable connections to 3. Probe points are named above in terms of
the pad you actually touch. Ground for a scope is any of the Pico's GND header
pins — 3, 8, 13, 18, 23, 28, 33 or 38 — which are through-hole and exposed.
Deliberately **no** pad on the 10 MΩ node (R2/R3/U2 pin 3): a probe there
measures the probe.

> One thing this costs: JP5's ammeter position. To read board current you now
> hold the meter across JP5's own two pads, which are 1 × 0.5 mm. If a probe
> slips the board loses power mid-measurement.

**GP11 leakage, RP2350-E9 — the one thing that can make the receiver dead on
arrival.** With GP11 high-Z the pad node's only DC path is R3 (10 MΩ) to VREF,
so *any* leakage into or out of GP11 shifts U2A's input by I × 10 MΩ: 15 nA is
the whole ±145 mV headroom of stage 1. E9 (A2 stepping) sources up to 120 µA
when the pad sits between logic levels *with the input buffer enabled* — and
our pad node idles at exactly 1.65 V. The owner's Pico 2 W is an RP2350A2,
so this is live, not hypothetical. Firmware must therefore disable GP11's
input buffer (`gpio_set_input_enabled(2, false)`) whenever it is high-Z, and
M3 must measure JP3 pad 1 with JP2 bridged: 1.65 V means clean, a rail means leakage.
C6 (DNP, 330 pF, in parallel with JP2) is the hardware way out: it AC-couples
the transmitter, so no DC can reach the pad node whatever GP11 does, at no cost
to the carrier (2.4 kΩ at 200 kHz against R1's 1 MΩ). See `review.md` §2.4.

**Push button SW2 on GP15 (pin 20), pull-up R15 to 3V3, net `BTN`.** The bench control
the next milestones need: force TX, force RX, provisioning mode, clear bond —
firmware decides which by press length. No debounce cap; debounce in firmware.
E9 does not affect a pulled-up input (the leakage pulls the same way). Pads 1-1
and 2-2 of the footprint are the switch's internally joined pairs, so it cannot
be fitted wrong.

*`BTN` on GP15 (pin 20) and `ROLE` on GP14 (pin 19) — where they started.* The
LED was briefly given GP14/13/12, which would have displaced both; once the
Pico's rows were checked the LED moved to the thumb row instead and these two
never had to move. They sit at the hand end of the **little-finger** row, next to
R15 and JP8, at x = 28.9 — outside the antenna keep-out, which spans x 12.9–27.1.
Neither escape puts any copper under the antenna.

**J5 "CHG": the charger's plug, in parallel with J1, same pin order.** The
TP4056's OUT± plugs in here so charging no longer means unplugging the cell.
**Not a power input** — it is wired straight to the cell, before SW1 and D1; the
silkscreen will say so.

**C5, 10 µF electrolytic (the C4 part) on U2's VDD** alongside C3, against the
Pico's SMPS. JP4 lets the real experiment happen: run the AFE from a bench
supply and compare noise floors.

**H1–H4, M3 mounting holes, 3.4 mm unplated**, one per corner at (4.5, 4.5),
(35.5, 4.5), (4.5, 57.5) and (35.5, 57.5), not tied to GND so a metal screw or
standoff cannot become a second, uncontrolled ground-plane connection (design
§8.4: exactly one). 3.4 mm is the ISO 273 *medium* clearance for M3, up from the
3.2 mm *close* fit: the board is screwed into a printed boss carrying a 5 mm
brass insert, so the screw is never a locating feature and the extra 0.2 mm
swallows the boss's position tolerance instead of fighting it. Each hole carries
an **8 mm keep-out** for that boss — in the footprint, not drawn on the board, so
DRC enforces it wherever the hole goes and it cannot be forgotten on the fourth
corner.

**Expansion E1–E10: ten breakout pads, not a connector.** 3V3 (Pico pin 36),
RUN (30), GP0 (1), GP1 (2), GP4 (6), GP5 (7), GP20 (26), GP21 (27), GP27/ADC1
(32), GND (3). Through-hole, 2.0 mm pad on a 1.0 mm drill: a wire solders in.
The spare ADC input is there on purpose — a second analogue path is the most
likely "hack" this board will ever need. RUN to GND is a reset.

*Was J4, a 1×10 female header.* The floor plan puts each pad directly inboard of
the Pico pin it breaks out, which is ten scattered positions on both sides of the
board — a single 1×10 footprint cannot describe that. Ten `TestPoint` symbols
also keep the netlist oracle honest: each pad is its own reference with one pin,
so `EXPECTED_NETS` names `("E7", "1")` rather than `("J4", "7")` and a
mis-numbered header pin cannot hide inside a net that still has the right size.
The nets themselves are unchanged. The 1×10 comes off the parts list; the 1×40
strip is now only the Pico's two 1×20s.

**SW1 footprint (`handoff.pretty/SW_Slide_SS-12F23G5`, written by
`tools/gen_sw_footprint.py`)** — **measured on the physical part, 11 Sep 2026.**
The first version of this footprint had the body standing *behind* the pin row
with the handle pointing up; that was a misreading of the vendor drawing's
views. The part is a right-angle switch: it **lies flat on the board** with the
handle sticking out sideways, parallel to the PCB.

| | mm |
|---|---|
| pin row → front face of the body | 6.3 |
| pin row → tip of the handle | 11.3 (the 5 mm "G5" handle) |
| body behind the pin row | 0.8 |
| body height above the board | 5.5 |
| body across / over the ears | 8.7 / 12.9 |
| terminals | 0.8 × 0.45 mm at 3.0 mm pitch, centre = common = symbol pin 2 |
| ear holes | ±6.05, 1.15 mm drill for the ±0.2 tolerance |
| handle | 3.0 mm square, 3.5 mm travel → a 6.5 mm slot |

**Layout rule: put the board edge on the line marked "board edge" on
Dwgs.User, at y = +6.3 from the pin row.** The metal body then sits wholly on
the board and all 5 mm of the handle is outside it, which is what the owner
wants — housing inside the enclosure, slider through the wall. The footprint
draws the handle and its travel envelope on Dwgs.User and F.Fab, not
silkscreen, because silk past the board edge is clipped in fabrication.

**SMPS mode pin.** Design §10.4 says tie GP23 high. Wrong for a 2 W — that is a
firmware call on WL_GPIO1 (see README). Nothing on the board.

**Firmware notes that come from the board**

- GP11: `gpio_set_input_enabled(2, false)` whenever it is high-Z (E9, above).
  Never idle GP11 as a driven output between shouts — a driven low pulls the
  10 MΩ node to 0.28 V and stage 1 sits on the rail.
- The VSYS/3 monitor on GP29 (ADC3) reads *after* D1: add ~0.3 V (0.2 V at idle
  current, 0.35 V with the radio on) to get the cell voltage, or calibrate
  against J1 pin 2 once. On a Pico 2 W GP29 is shared with the CYW43 SPI clock, so
  read it the way `pico-examples/adc/read_vsys` does.
- GP14 (`ROLE`): enable the internal pull-up, read it once at boot.
- GP15 (`BTN`): active low, R15 pulls up.
- Hold SW2 at power-on → `rom_reset_usb_boot()` would give a BOOTSEL that is
  reachable with the lid on; the Pico's own BOOTSEL is not.

---

## Layout rules to carry into the PCB (from design §12)

- The R2 / R3 / U2-pin-3 junction is the only critical node: tiny copper island,
  parts butted up to the pin, guard ring around it, no solder-mask-free copper
  nearby, IPA-clean after assembly.
- Op-amp far from the Pico and its antenna; C3 across pins 8/4 with the shortest
  possible loop.
- Ground pour on both sides, stitched; the pour is the ground-plane electrode.
- PAD connector J2 at the board edge nearest where the pad will sit; keep the
  pad wire under 5 cm.
- Enclosure stack, skin outward: PAD → battery → PCB (ground pour on the outer
  face). Never put PAD and ground plane on two faces of one board (§8.2).
- Pico at a board edge, USB / SWD / BOOTSEL end outward; no copper in the
  footprint's antenna keep-out, antenna end away from J2.
- SW1's board edge goes on its Dwgs.User "board edge" line (6.3 mm from the
  pin row): body on the board, handle 5 mm outside it. The enclosure wall
  needs a 6.5 mm × 3.0 mm slot, its centre 3.0 mm beyond the board edge, at
  the handle's height above the board. **Its pin row is therefore at x = 6.3**,
  not the 8.8 the floor plan first drew — at 8.8 only 2.5 mm of the 5 mm handle
  cleared the edge. (The floor plan had drawn the body 5.5 mm deep, which is its
  height above the board; in plane it is 7.1 mm, 0.8 behind the pins and 6.3 in
  front.) At x = 6.3 the body spans x 0–7.1 and stands 4.0 mm off the Pico's
  thumb row, against the 2.3 mm the brief flagged as the tightest clearance on
  the board. That clearance is no longer the tightest thing here.
- SW2 where a lid hole or actuator can reach it; J3 pigtail lets D2 live in
  the lid.
- H1–H4 in the corners, unplated, no copper tie.
- Solder jumpers on the side that stays accessible with the Pico fitted, each
  with its name and "OPEN = isolated" on the silkscreen; J5 marked "CHG only".
- C2's return and AGND (pin 33) to one point.

---

## The board

`handoff.kicad_pcb`, generated by `tools/gen_pcb.py`. 40 × 62 mm, 3 mm corners,
two layers, 1.6 mm. The generator's frame is the floor plan's, one to one, so
every coordinate in it can be read straight off `floorplan.svg`: x 0 → 40 runs
thumb wall → little-finger wall, y 0 → 62 runs hand end → elbow end.

Placement is declarative. Each part names an anchor — its origin, or one of its
pads — and a target coordinate, and the script solves for the footprint position.
No offset arithmetic appears in the table, and a footprint that changes shape
cannot quietly drag a part off its target.

| | |
|---|---|
| Track / clearance | 0.25 mm / 0.2 mm default, 0.5 mm on the `Power` class (GND, +3V3, VSYS, BAT+, SW_OUT, D1_K, AFE_3V3) |
| Vias | 0.6 mm / 0.3 mm drill |
| Fab minimums enforced | 0.15 mm track and clearance, 0.3 mm drill, 0.3 mm copper-to-edge — inside every PCBWay 2-layer process |
| Pours | GND on both faces. The **top** pour is the ground-plane electrode |
| Rule areas | the 20 × 30 cell/pad pocket bars bottom-face pour and bottom-face parts (tracks are allowed); each mounting hole carries its own 8 mm boss keep-out |

**Two custom DRC rules, in `handoff.kicad_dru`.** Both exist because KiCad's
courtyard test has no notion of height, and both suppress *only* courtyard
overlap — every clearance and short test still runs against these parts:

- *The Pico is socketed.* Its own PCB sits ~8.5 mm above this one, so the AFE,
  the breakout pads and the bottom-face jumpers are deliberately underneath it.
- *Breakout pads have no body.* E1–E10 inherit a `TestPoint` footprint's 3.09 mm
  courtyard, which is wider than the 2.54 mm pitch they sit on. Copper clearance
  between them is unaffected and still checked: 0.54 mm.

**Silkscreen.** Values are hidden everywhere. References are on silk for the
things the bring-up procedure names in your hand — test pads, jumpers,
connectors, switches, U1/U2, D1, the mounting holes — and on the fab layer for
the AFE's passives and for E1–E10, where 0.8 mm text does not fit between 1206
pads on a 5.3 mm pitch. **E1–E10 are identified by the Pico pin each one sits
inboard of**, which is unambiguous and listed under *Expansion* above. The
silkscreen *outlines* of U1, the four JST connectors, SW1 and the mounting holes
are moved to the fab layer too: U1's is a 21 × 51 box drawn over everything that
deliberately lives under it. That is what the ten `lib_footprint_mismatch`
warnings are — the only warnings the board reports, and all deliberate.

### The breakout pads moved out, and the channel routes

The ten expansion pads used to sit **inboard** of their own pins, one hop into
the channel under the Pico. That is what over-subscribed it: two columns of
pads, two columns of 1206s and an MSOP-8 in 10 mm, with R2 stopping 0.225 mm
short of U2 pin 1 and no routing lane anywhere.

They now sit **outboard**, between each pin row and the wall - x 8.6 on the
thumb side, x 31.44 on the little-finger side. Each pad is still a single hop
from its own pin, and the channel is the full 16.2 mm between the pin rows.
With that the AFE columns open out to x 15.6 and 24.4, which gives a 1.43 mm
lane each side of U2 and 2.5 mm outboard of each column.

Three other things had to move with them:

* **R2 went 2 mm toward the hand.** Its 1.3 mm pad lands on x 19.025-20.325,
  directly over pins 3 and 2. At the old y it left a 0.9 mm slot above U2's pad
  row and pins 1 and 2 had nowhere to escape. At y 24.0 the slot is 2.65 mm and
  takes two tracks. The cost is the 1 MOhm island: 8.4 mm of trace, not 6.4.
* **The ammeter cluster went to the board edge** (x 37.9). x 32.3 is now the
  little-finger breakout column, and at the edge a clip has nothing to foul.
* **The thumb breakout stubs run on the bottom face**, so the top lane at
  x 9.7 stays clear. That lane is the only way past the antenna keep-out on the
  thumb side, and C4 needs it.

The amplifier now routes: both gain stages, the 1 MOhm island, the VREF bias
bus, the interstage cap and the ADC filter. Pin 2 escapes its pad twice - north
into the slot R2 vacated, and south under U2's body, where the island's
vertical does not reach. That second escape is the one the first pass could not
find.

## What is still open: three connections

**3 unconnected items, no shorts, no crossings and no clearance violations.**
All three are the same fact, and it is now a small one.

OUT1 leaves the amplifier at the top of the right-hand column and has to get
down to C1 and across to JP3. It does that in the only lane outboard of the
column, and in doing so it draws a line between the column and the board edge
from y 28.6 to y 43.2. Two nets need to cross that line:

* **AFE_3V3** from C3 out to JP4 and to C5 at the hand end (2 items)
* **JP3 pad 3 to R7** — stage 2's output to the gain jumper (1 item)

Anything leaving the column between those two y values is behind it. Three ways
out, cheapest first:

1. **Reorder the right-hand column** so C3 and R7 sit above OUT1's exit rather
   than below it. Free, but it moves the decoupling cap away from pin 8.
2. **0805 passives.** At 5.3 mm pitch a 1206 leaves a 0.31 mm gap between parts
   and an 0805 leaves 1.51 mm, which is a lane. That gives the column doorways
   of its own and OUT1 stops being a wall.
3. **Four layers** — 5 and 4-5 days against  and 24 hours.

Nothing is bodged with vias through the electrode pour, which is the one thing
that pour cannot afford.

