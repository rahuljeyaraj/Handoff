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
| `handoff.pretty/` | project footprints: the SS-12F23G5 slide switch, drawn from the vendor drawing |
| `tools/gen_schematic.py` | **the source of the schematic.** Generates the sheet from the §7 netlist and proves it with `kicad-cli`: ERC, the netlist against `EXPECTED_NETS` pin for pin, and the BOM against the parts table below. Edit this, not the sheet |
| `tools/render.py` | renders `build/handoff.pdf` to `build/sheet.png` so the sheet can be looked at |
| `review.md` | the pre-layout review: every check, its number, its verdict |
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
| — | 2.54 mm 1×40 female single-row header | cut into 2×20 for the Pico, 1×10 for J4 | 555698 | 1 strip |
| U2 | MCP6292-E/MS dual op-amp, 10 MHz | MSOP-8, soldered directly to the board (no DIP adapter on the PCB) | R193529 | 1 |
| R1, R2 | 1 MΩ | 1206 | 574983 | 2 |
| R3 | 10 MΩ | 1206 | 574992 | 1 |
| R4, R6, R7, R10, R11 | 100 kΩ | 1206 | 574955 | 5 |
| R5, R8, R15 | 10 kΩ 1 % (Yageo RC1206FR-0710KL) | 1206 | R137556 | 3 |
| R9 | 1.5 kΩ 1 % (Yageo RC1206FR-071K5L) | 1206 | R137563 | 1 |
| R12, R13, R14 | 330 Ω | 1206 | 575088 | 3 |
| C1, C3 | 100 nF X7R 50 V (TCC1206X7R104J500DT) | 1206 | R153721 | 2 |
| C2 | 330 pF C0G/NP0 50 V (KEMET C1206C331J5GACTU) | 1206 | R111869 | 1 |
| C4, C5 | 10 µF 63 V electrolytic | radial through-hole, 5 mm dia, 2.54 mm lead pitch (KiCad `CP_Radial_D5.0mm_P2.50mm`) | 1090083 | 2 |
| C6 | **DNP** — 330 pF C0G, the C2 part, fitted only if GP2 leakage turns out to matter (see *Deviations*) | 1206 | R111869 | 0 |
| D1 | 1N5819 Schottky 40 V 1 A | DO-41, horizontal | R241509 | 1 |
| SW1 | SS-12F23G5 slide switch, SPDT (1P2T), right-angle, 5 mm handle | 3 terminals at 3.0 mm pitch + 2 mounting ears, `handoff:SW_Slide_SS-12F23G5` | R132611 | 1 |
| D2 | RGB LED, common cathode, 5 mm, clear (5-pack) | off-board: solders into J3 or plugs in via a 4-pin XH pigtail | R183455 | 1 |
| SW2 | Tactile push button 6 × 6 × 5 mm, 4 legs | through-hole, `Button_Switch_THT:SW_PUSH_6mm` | 618182 | 1 |
| J1 | JST-XH 2.54 straight 2-pin male (battery) | through-hole | — | 1 |
| J5 | JST-XH 2.54 straight 2-pin male (charger — **one more 2-pin header than before**) | through-hole | — | 1 |
| J2 | JST-XH 2.54 straight 2-pin male (electrodes) | through-hole | — | 1 |
| J3 | JST-XH 2.54 straight 4-pin male (LED), or the LED soldered straight in | through-hole | — | 1 |
| J4 | 1×10 female, cut from the 1×40 strip above | through-hole | 555698 | 1 |
| BT1 | KP384455 Li-ion 3.7 V 1500 mAh | off-board, on a JST-XH pigtail | — | 1 |

Copper only, nothing to buy: 13 test pads TP1–TP13, 8 solder jumpers JP1–JP8, 4
mounting holes H1–H4 (M3 clearance, 3.2 mm, unplated).

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

**RGB LED on GP16 / GP17 / GP18 (R / G / B) through 330 Ω.** J3 pin order is
R, K, G, B — the lead order of a standard 5 mm common-cathode RGB LED, longest
leg second — so the LED can be soldered directly. At 3.3 V and 330 Ω the red
runs ~4 mA and green/blue ~1 mA: a status indicator, not a torch.

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
| JP2 | GP2 → R1 | the transmitter from the pad | nothing — leave open and fit C6 instead if GP2 leakage biases the pad (moving TX to another GPIO does not help: every bank-0 pin has E9) | M3 (measure leakage first), M8 |
| JP3 (3-way) | OUT1 / OUT2 → R9 | the ADC from both stages; centre pad → R9, pad 3 ← OUT2 (×121), pad 1 ← OUT1 (×11) | TP3 or TP2 as a bare output for the scope | M7: blob to pad 3; move it to pad 1 if a noisy hall clips stage 2 (design §15.2) |
| JP1 | R9 → GP26/ADC0 | the ADC pin from the amplifier | TP4 directly, as M5 does with its own attenuator; TP4 to TP13 with a wire grounds the ADC for the M4 noise floor | M4/M5 open, M7 bridged |
| JP8 | GP14 → GND | (a strap, not in-line) | — | M6: bridge on one board to set its role; firmware enables GP14's pull-up |

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
   pad node stays at 1.65 V with GP2 high-Z. If it does not, fit **C6** instead.
6. Bridge **JP5** last, with the cell on J1; first reading at TP10 vs TP8 is the
   D1 drop.

**Test pads** (1.5 mm SMD): TP1 PAD, TP2 OUT1, TP3 AFE out (OUT2), TP4 ADC0,
TP5 VREF, TP6 AFE_3V3, TP7 GND (by U2), TP8 VSYS (after JP5), TP9 GP2, TP10
BAT+ (the cell, before D1), TP11 D1 cathode (before JP5), TP12 RX_IN (R2 side
of JP7), TP13 GND (by the ADC pads, for scope grounds). Deliberately **no** pad
on the 10 MΩ node (R2/R3/U2 pin 3): a probe there measures the probe.

**GP2 leakage, RP2350-E9 — the one thing that can make the receiver dead on
arrival.** With GP2 high-Z the pad node's only DC path is R3 (10 MΩ) to VREF,
so *any* leakage into or out of GP2 shifts U2A's input by I × 10 MΩ: 15 nA is
the whole ±145 mV headroom of stage 1. E9 (A2 stepping) sources up to 120 µA
when the pad sits between logic levels *with the input buffer enabled* — and
our pad node idles at exactly 1.65 V. The owner's Pico 2 W is an RP2350A2,
so this is live, not hypothetical. Firmware must therefore disable GP2's
input buffer (`gpio_set_input_enabled(2, false)`) whenever it is high-Z, and
M3 must measure TP2 with JP2 bridged: 1.65 V means clean, a rail means leakage.
C6 (DNP, 330 pF, in parallel with JP2) is the hardware way out: it AC-couples
the transmitter, so no DC can reach the pad node whatever GP2 does, at no cost
to the carrier (2.4 kΩ at 200 kHz against R1's 1 MΩ). See `review.md` §2.4.

**Push button SW2 on GP15, pull-up R15 to 3V3, net `BTN`.** The bench control
the next milestones need: force TX, force RX, provisioning mode, clear bond —
firmware decides which by press length. No debounce cap; debounce in firmware.
E9 does not affect a pulled-up input (the leakage pulls the same way). Pads 1-1
and 2-2 of the footprint are the switch's internally joined pairs, so it cannot
be fitted wrong.

**J5 "CHG": the charger's plug, in parallel with J1, same pin order.** The
TP4056's OUT± plugs in here so charging no longer means unplugging the cell.
**Not a power input** — it is wired straight to the cell, before SW1 and D1; the
silkscreen will say so.

**C5, 10 µF electrolytic (the C4 part) on U2's VDD** alongside C3, against the
Pico's SMPS. JP4 lets the real experiment happen: run the AFE from a bench
supply and compare noise floors.

**H1–H4, M3 mounting holes, unplated**, one per corner, not tied to GND so a
metal screw or standoff cannot become a second, uncontrolled ground-plane
connection (design §8.4: exactly one).

**J4 expansion, 1×10 female:** 3V3, RUN, GP0, GP1 (UART0), GP4, GP5
(I²C0), GP20, GP21, GP27 (ADC1), GND. The spare ADC input is there on purpose — a
second analogue path is the most likely "hack" this board will ever need. RUN to
GND is a reset.

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

- GP2: `gpio_set_input_enabled(2, false)` whenever it is high-Z (E9, above).
  Never idle GP2 as a driven output between shouts — a driven low pulls the
  10 MΩ node to 0.28 V and stage 1 sits on the rail.
- The VSYS/3 monitor on GP29 (ADC3) reads *after* D1: add ~0.3 V (0.2 V at idle
  current, 0.35 V with the radio on) to get the cell voltage, or calibrate
  against TP10 once. On a Pico 2 W GP29 is shared with the CYW43 SPI clock, so
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
  the handle's height above the board.
- SW2 where a lid hole or actuator can reach it; J3 pigtail lets D2 live in
  the lid.
- H1–H4 in the corners, unplated, no copper tie.
- Solder jumpers on the side that stays accessible with the Pico fitted, each
  with its name and "OPEN = isolated" on the silkscreen; J5 marked "CHG only".
- C2's return and AGND (pin 33) to one point; TP13 beside TP2–TP4.
