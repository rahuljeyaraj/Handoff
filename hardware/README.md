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
| `tools/gen_floorplan.py` / `floorplan.svg` | the floor plan: which block sits where in the enclosure, and why. Placement only, no tracks — the input to the layout session |

Regenerate and verify after any change (must end `ERC: 0 violation(s), 0 error(s)`,
`0 missing, 0 unexpected` nets and `0 difference(s)` against the parts table):

```
python hardware/tools/gen_schematic.py && python hardware/tools/render.py
```

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

Copper only, nothing to buy: 13 test pads TP1–TP13, 8 solder jumpers JP1–JP8, 10
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

**RGB LED on GP14 / GP13 / GP12 (R / G / B) through 330 Ω.** J3 pin order is
R, K, G, B — the lead order of a standard 5 mm common-cathode RGB LED, longest
leg second — so the LED can be soldered directly. At 3.3 V and 330 Ω the red
runs ~4 mA and green/blue ~1 mA: a status indicator, not a torch.

*Moved from GP16/17/18 by the floor plan.* J3 sits hard against the left edge,
parallel to it, opposite the Pico's row A. With R → GP14 (pin 19), K → GND
(pin 18), G → GP13 (pin 17), B → GP12 (pin 16), J3's four pins face pins 16–19
straight across: four ~7 mm traces, no crossings, and the LED's legs bend 90°
straight into the header with no pigtail. The cathode lands on a Pico GND pin
that is already there, so the LED's return does not travel.

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
| JP5 | D1 cathode → VSYS | the whole board from the cell (USB still powers it) | an ammeter TP11 → TP8 reads the board current; a bench supply at TP8 replaces the cell | M3 bring-up, M11 battery life |
| JP4 | 3V3 → U2 VDD (and the VREF divider) | the analogue side from the Pico's rail | a bench supply / second cell at TP6 — the SMPS-noise experiment; an ammeter across JP4 reads U2's current | M7 |
| JP6 | R10/R11/C4 divider → VREF | the bias divider from the amplifiers | an external 1.65 V at TP5 | M7 if VREF is suspected |
| JP7 | PAD → R2 | the pad and the transmitter from the receiver | a signal generator at TP12 (through R2: the 1 MΩ is still in circuit) | M7 gain / corner / clipping sweeps |
| JP2 | GP11 → R1 | the transmitter from the pad | nothing — leave open and fit C6 instead if GP11 leakage biases the pad (moving TX to another GPIO does not help: every bank-0 pin has E9) | M3 (measure leakage first), M8 |
| JP3 (3-way) | OUT1 / OUT2 → R9 | the ADC from both stages; centre pad → R9, pad 3 ← OUT2 (×121), pad 1 ← OUT1 (×11) | TP3 or TP2 as a bare output for the scope | M7: blob to pad 3; move it to pad 1 if a noisy hall clips stage 2 (design §15.2) |
| JP1 | R9 → GP26/ADC0 | the ADC pin from the amplifier | TP4 directly, as M5 does with its own attenuator; TP4 to TP13 with a wire grounds the ADC for the M4 noise floor | M4/M5 open, M7 bridged |
| JP8 | GP17 → GND | (a strap, not in-line) | — | M6: bridge on one board to set its role; firmware enables GP17's pull-up |

Nothing touches the 10 MΩ node (R2/R3/U2 pin 3): no jumper, no test pad. JP7
and TP12 are on the *pad* side of R2.

**Bring-up order** — the sequence that makes the jumpers pay for themselves:

1. Nothing bridged, no cell. USB in: the Pico runs, M3–M5 need nothing else
   (M5's loopback goes TP9 → TP4 by wire; TP4 → TP13 grounds the ADC for M4).
2. Bridge **JP4** and **JP6**. Read 3.3 V at TP6, 1.65 V ±5 % at TP5, 1.65 V
   at TP2 and TP3. Anything else at TP2/TP3 is a bias problem: open JP6 and
   inject 1.65 V at TP5 to split divider from amplifier.
3. Bridge **JP3 centre–pad 3**. Inject at TP12 (JP7 still open): gain, corner,
   clipping and noise sweeps of M7, reading TP3 with a scope and TP4/ADC after
   bridging **JP1**.
4. Bridge **JP7**. Now the pad is live; M8 with a capacitor from TP1 to TP13.
5. Bridge **JP2** *only after* the M3 leakage measurement (below) says the
   pad node stays at 1.65 V with GP11 high-Z. If it does not, fit **C6** instead.
6. Bridge **JP5** last, with the cell on J1; first reading at TP10 vs TP8 is the
   D1 drop.

**Test pads** (1.5 mm SMD): TP1 PAD, TP2 OUT1, TP3 AFE out (OUT2), TP4 ADC0,
TP5 VREF, TP6 AFE_3V3, TP7 GND (by U2), TP8 VSYS (after JP5), TP9 GP11, TP10
BAT+ (the cell, before D1), TP11 D1 cathode (before JP5), TP12 RX_IN (R2 side
of JP7), TP13 GND (by the ADC pads, for scope grounds). Deliberately **no** pad
on the 10 MΩ node (R2/R3/U2 pin 3): a probe there measures the probe.

**GP11 leakage, RP2350-E9 — the one thing that can make the receiver dead on
arrival.** With GP11 high-Z the pad node's only DC path is R3 (10 MΩ) to VREF,
so *any* leakage into or out of GP11 shifts U2A's input by I × 10 MΩ: 15 nA is
the whole ±145 mV headroom of stage 1. E9 (A2 stepping) sources up to 120 µA
when the pad sits between logic levels *with the input buffer enabled* — and
our pad node idles at exactly 1.65 V. The owner's Pico 2 W is an RP2350A2,
so this is live, not hypothetical. Firmware must therefore disable GP11's
input buffer (`gpio_set_input_enabled(2, false)`) whenever it is high-Z, and
M3 must measure TP2 with JP2 bridged: 1.65 V means clean, a rail means leakage.
C6 (DNP, 330 pF, in parallel with JP2) is the hardware way out: it AC-couples
the transmitter, so no DC can reach the pad node whatever GP11 does, at no cost
to the carrier (2.4 kΩ at 200 kHz against R1's 1 MΩ). See `review.md` §2.4.

**Push button SW2 on GP16 (pin 21), pull-up R15 to 3V3, net `BTN`.** The bench control
the next milestones need: force TX, force RX, provisioning mode, clear bond —
firmware decides which by press length. No debounce cap; debounce in firmware.
E9 does not affect a pulled-up input (the leakage pulls the same way). Pads 1-1
and 2-2 of the footprint are the switch's internally joined pairs, so it cannot
be fitted wrong.

*BTN moved from GP15 and `ROLE` from GP14 (pin 19) to GP17 (pin 22)*, because the
LED took GP14/13/12. Both now land on row B beside SW2 and JP8 on the floor plan.
The alternative, GP22 (pin 29) and GP19 (pin 25), was checked and rejected: it
clears the antenna keep-out, but the escapes then have to detour around J2 and
the corner boss to reach SW2 and JP8 at the hand end, which is more copper, not
less. Pins 21 and 22 sit 1.3 mm and 1.6 mm inside the keep-out's edge and both
nets are static DC — a pull-up input read at boot and a strap to GND — so they
are exactly the "thin escape" the keep-out tolerates. Nothing switching or
RF-carrying goes under the antenna.

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
  against TP10 once. On a Pico 2 W GP29 is shared with the CYW43 SPI clock, so
  read it the way `pico-examples/adc/read_vsys` does.
- GP17 (`ROLE`): enable the internal pull-up, read it once at boot.
- GP16 (`BTN`): active low, R15 pulls up.
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
  the handle's height above the board.
- SW2 where a lid hole or actuator can reach it; J3 pigtail lets D2 live in
  the lid.
- H1–H4 in the corners, unplated, no copper tie.
- Solder jumpers on the side that stays accessible with the Pico fitted, each
  with its name and "OPEN = isolated" on the silkscreen; J5 marked "CHG only".
- C2's return and AGND (pin 33) to one point; TP13 beside TP2–TP4.

### What the floor plan changed, including three rules it overturns

The floor plan (`floorplan.svg`) settled the enclosure, and settling it
contradicted three of the rules above. They are struck here rather than quietly
edited away, because each one cost something real.

- **Face assignment.** The AFE is SMD on the **top** face, in the 10 mm channel
  under the Pico. The solder jumpers and test pads go on the **bottom** face, so
  they stay reachable with the Pico socketed.
- **No parts on the bottom face over the cell/pad footprint.** The stack sits
  ~0.5 mm below the board, so any through-hole protrusion there eats the 1.5 mm
  of solder clearance the section drawing allows.
- **No bottom-side pour over the pad.** The TOP pour is the ground-plane
  electrode; a bottom pour there would sit between the pad and the skin.
- **Antenna keep-out at the hand end.** No copper under it. Thin escapes only,
  for pins 16–24 — which is where the LED, and now BTN and ROLE, live.
- **Design §12.3 is no longer met.** "Op-amp far from the Pico and its antenna"
  above is **overturned**: the AFE is now directly under the Pico. Once the ten
  breakout pads take their positions inboard of their own pins, the channel
  under the Pico is the only contiguous area left on the board. This is a known
  compromise, not an oversight. JP4 and the M7 bench-supply comparison are how
  we find out what it costs — open JP4, run the AFE from a second cell, compare
  noise floors. If the SMPS wins, the fix is a shield can or a different board,
  not a re-route.
- **Stacking the pad under the cell restores ~16 pF of pad-to-return shunt** —
  the figure design §8.2 rejects for a two-sided board, and the rule "never put
  PAD and ground plane on two faces of one board" above is **bent** by it. The
  owner chose the stack so cell and pad share one four-sided pocket. Mitigation:
  2–3 mm of foam between cell and pad, which is also what stops the cell chafing.
  M7's loss measurement is the test; if it reads worse than the budget, the foam
  gets thicker before anything else changes.
- **The cell sits partly under the antenna keep-out.** Nothing else fits there
  once the pad is centred, so the rule "antenna end away from J2" above is
  **met only for J2** — the cell is a different intruder. If BLE range
  disappoints, the fix is a smaller pad, not a smaller cell: the pad's area is
  what the link budget can most afford to lose.
