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
| `handoff.pretty/` | project footprints: the SS-12F23G5 slide switch, the 3.4 mm M3 mounting hole with its boss keep-out, and the SOD-123FL diode (KiCad has none) |
| `tools/gen_schematic.py` | **the source of the schematic.** Generates the sheet from the §7 netlist and proves it with `kicad-cli`: ERC, the netlist against `EXPECTED_NETS` pin for pin, and the BOM against the parts table below. Edit this, not the sheet |
| `tools/render.py` | renders `build/handoff.pdf` to `build/sheet.png` so the sheet can be looked at |
| `review.md` | the pre-layout review: every check, its number, its verdict |
| `schematic-prompt.md` | the brief for the next session: the schematic changes the floor plan forces, then layout |
| `tools/gen_sw_footprint.py` | writes `handoff.pretty/SW_Slide_SS-12F23G5`, geometry measured on the physical part |
| `tools/gen_mount_footprint.py` | writes `handoff.pretty/MountingHole_3.4mm_M3_Boss6mm`: 3.4 mm unplated drill plus the 6 mm boss keep-out. The name follows the boss diameter, so the board cannot point at a footprint that is no longer the one described |
| `tools/gen_sod123fl_footprint.py` | writes `handoff.pretty/D_SOD-123FL` for D1/D3, from the PMEG3020ER-TP outline drawing; `--prove` puts it on `build/fp_test.kicad_pcb`, runs DRC and renders it |
| `tools/gen_bom.py` / `bom.csv` | the committed BOM: kicad-cli's export plus the **Package** column it cannot produce, derived from each footprint in one place. Called by `gen_schematic.py` after its BOM check |
| `tools/gen_floorplan.py` / `floorplan.svg` | the floor plan: which block sits where in the enclosure, and why. Placement only, no tracks — the **pre-layout** input. **Superseded by the board**: it still draws E1–E10 inboard of their pins, `JP1-JP7`, `TP12`/`TP13` and the 4.5 mm hole positions, none of which exist. Read `gen_pcb.py`'s `PLACE` table for where things actually are |
| `tools/gen_pcb.py` / `handoff.kicad_pcb` | **the source of the board.** Builds it from the schematic's netlist and the floor plan, then proves it with `kicad-cli`: DRC, schematic parity pin for pin, an independent short check, a ground-pour island check, and every pad against every mounting boss. Writes the fab pack last. Edit this, not the `.kicad_pcb` |
| `build/plot/` | the fab pack: Gerbers for every layer, both Excellon drill files with their maps, and the job file. Regenerated on every `gen_pcb.py` run, so it cannot go stale against the board. Not committed (`build/` is ignored) |
| `build/drills.md` | the drill table: every distinct hole, how many, plated or not, and what asks for it |
| `handoff.kicad_dru` | the four custom DRC rules: two tell KiCad about the third dimension it does not model (the Pico is socketed 8.5 mm up; bare pads have no body, but not over a mounting boss), two enforce the layout conventions it will not check unless asked (no right-angle corners, no stub segments). Written by `gen_pcb.py` |

Regenerate and verify after any change (must end `ERC: 0 violation(s), 0 error(s)`,
`0 missing, 0 unexpected` nets, `0 difference(s)` against the parts table, and
`passive packages: 0603` from the BOM writer):

```
python hardware/tools/gen_schematic.py && python hardware/tools/render.py
```

The board is regenerated separately, with KiCad's own Python (it needs `pcbnew`):

```
"C:/Program Files/KiCad/10.0/bin/python.exe" hardware/tools/gen_pcb.py
```

It must end `DRC: 0 error(s)`, `parity 0`, `unrouted 0 item(s)` and
`fab pack: 17 files in build/plot`, with no `NET`, `OUTSIDE` or `IN BOSS` lines.
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
| R1, R2, R3 | 1 MΩ ±1 % 100 mW 75 V (Uniohm 0603WAF1004T5E) | **0603** | R134792 | 3 |
| R4, R6, R7, R10, R11, R17 | 100 kΩ (104) | **0603** | MakerBazar 1481824519-20P | 6 (pack of 20) |
| R5, R8, R15 | 10 kΩ (103) | **0603** | MakerBazar 1481824531-20P | 3 (pack of 20) |
| R9 | 1.5 kΩ ±1 % 100 mW 75 V (Uniohm 0603WAF1501T5E) | **0603** | R134878 | 1 |
| R12, R13, R14, R16 | 100 Ω (101) | **0603** | MakerBazar 1860310035-20P | 4 (pack of 20) |
| C3 | 100 nF X7R ±10 % 50 V (Samsung CL10B104KB8NNNC) | **0603** | R172322 | 1 |
| C1, C2 | 330 pF **C0G/NP0** ±5 % 100 V (Samsung CL10C331JC8NNNC) | **0603** | R136892 | 2 |
| C4, C5 | 10 µF X5R ±20 % **25 V** (Murata GRM188R61E106MA73D) | **0603**, non-polarised | R144171 | 2 |
| C6 | **DNP** — 330 pF C0G, the C2 part, fitted only if GP11 leakage turns out to matter (see *Deviations*) | **0603** | R136892 | 0 |
| D1 | PMEG3020ER-TP (**Tech Public**), SOD-123FL, 40 V 2 A Schottky — *not* the Nexperia PMEG3020ER, which is SOD-123W | **SOD-123FL**, `handoff:D_SOD-123FL` | R241663 | 1 |
| SW1 | SS-12F23G5 slide switch, SPDT (1P2T), right-angle, 5 mm handle | 3 terminals at 3.0 mm pitch + 2 mounting ears, `handoff:SW_Slide_SS-12F23G5` | R132611 | 1 |
| D2 | RGB LED, common cathode, 5 mm, clear (5-pack) | off-board: solders into J3 or plugs in via a 4-pin XH pigtail | R183455 | 1 |
| SW2 | Tactile push button 6 × 6 × 5 mm, 4 legs | through-hole, `Button_Switch_THT:SW_PUSH_6mm` | 618182 | 1 |
| Q1 | AO3400A N-channel MOSFET, 30 V 5.2 A, 27 mΩ @ 4.5 V | SOT-23 | MakerBazar R209179 | 1 |
| D3 | PMEG3020ER-TP (**Tech Public**), SOD-123FL, 40 V 2 A Schottky, motor flyback — the D1 part | **SOD-123FL**, `handoff:D_SOD-123FL` | R241663 | 1 |
| M1 | Coin vibration motor, 1034, 10 mm disc, ~3 V | off-board: leads solder into J6 | MakerBazar 522260 | 1 |
| J6 | Motor pads, 2 × 2.54 mm through-hole | takes the motor leads direct, or a 2-pin header | — | 1 |
| J1 | JST-XH 2.54 straight 2-pin male (battery) | through-hole | — | 1 |
| J5 | JST-XH 2.54 straight 2-pin male (charger — **one more 2-pin header than before**) | through-hole | — | 1 |
| J2 | JST-XH 2.54 straight 2-pin male (electrodes) | through-hole | — | 1 |
| J3 | JST-XH 2.54 straight 4-pin male (LED), or the LED soldered straight in | through-hole | — | 1 |
| BT1 | KP384455 Li-ion 3.7 V 1500 mAh | off-board, on a JST-XH pigtail | — | 1 |

Copper only, nothing to buy: 8 solder jumpers JP1–JP8, 8 test pads TP1–TP8, and
4 mounting holes H1–H4 (M3 clearance, 3.4 mm, unplated, each with a 6 mm boss
keep-out).

The 10 MΩ (Robu 574992) bought for R3 is no longer fitted — see *Deviations*. Keep
it: it is the A/B part for the M7 recovery-versus-loss comparison.

**Every passive on the board is 0603** since the 12 Sep 2026 order (see the last
*This session*). The 1206 1 MΩ / 1.5 kΩ / 330 pF / 100 nF (574983, R137563,
R111869, R153721) and the 0805 10 µF (MakerBazar 1491785836-10P) are no longer
fitted anywhere, and nor is the SS220F (Slkor R193098) both diodes were.

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
powers up) instead of destructive. **Now PMEG3020ER-TP (Tech Public, 40 V 2 A,
SOD-123FL)**, the third part in this position after the 1N5819 (DO-41) and the
SS220F (SMB) — see the session logs. Cost: **~0.4 V at 100 mA** (0.41 V typ at
1 A, 0.5 V max at 2 A, from the Tech Public datasheet; its Fig. 4 puts 100 mA at
0.35–0.4 V — there is no 100 mA line item), leaving ~2.9–3.8 V on VSYS from a
3.3–4.2 V cell, against the Pico's 1.8–5.5 V input range. That is 0.45 V better
than the SS220F's ~0.85 V, which was the price of a 200 V rating this board never
needed. Reverse leakage is 2 µA at 10 V, 25 °C.

**A haptic motor was added, on VSYS behind an N-FET.** Not in the design
doc at all — it arrived with this session's parts order. M1 is off-board on
leads into J6; Q1 switches it low-side from GP28, D3 catches the flyback,
R16/R17 are the gate chain. It runs from VSYS rather than the Pico's 3V3
regulator so that 100 mA of pulsed motor current never shares a rail with
the analogue front end. See *This session* below.

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
position — J3, the TX escape, BTN and ROLE — depends on
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
JP8, at x = 28.9 — outside the antenna keep-out, which spans x 12.9–27.1.
Neither escape puts any copper under the antenna.

**J5 "CHG": the charger's plug, in parallel with J1, same pin order.** The
TP4056's OUT± plugs in here so charging no longer means unplugging the cell.
**Not a power input** — it is wired straight to the cell, before SW1 and D1; the
silkscreen will say so.

**C5, 10 µF (the C4 part, an X5R 0603 now) on U2's VDD** alongside C3, against the
Pico's SMPS. JP4 lets the real experiment happen: run the AFE from a bench
supply and compare noise floors.

**H1–H4, M3 mounting holes, 3.4 mm unplated**, one per corner at (4.5, 4.5),
(36.5, 3.5), (3.5, 58.5) and (36.5, 58.5), not tied to GND so a metal screw or
standoff cannot become a second, uncontrolled ground-plane connection (design
§8.4: exactly one). 3.4 mm is the ISO 273 *medium* clearance for M3, up from the
3.2 mm *close* fit: the board is screwed into a printed boss carrying a 5 mm
brass insert, so the screw is never a locating feature and the extra 0.2 mm
swallows the boss's position tolerance instead of fighting it. Each hole carries
a **6 mm keep-out** for that boss — in the footprint, not drawn on the board, so
DRC enforces it wherever the hole goes and it cannot be forgotten on the fourth
corner.

**The boss was 8 mm and the holes were 4.5 mm in.** 6 mm is enough to hold a
5 mm insert with a wall round it, and 8 mm was what kept each screw a
millimetre further from its own corner than it needed to be — it reached 4 mm
in from two edges at once and was the widest thing on the board's perimeter.
Hole position is derived now rather than written out four times: each centre
sits `BOSS_R + 0.5` from both its edges, so the boss keeps the same half a
millimetre of wall it always had and the screws move to (3.5, 3.5) and its
three mirrors. `BOSS_R` in `gen_pcb.py` and `BOSS_D` in
`gen_mount_footprint.py` have to agree — the footprint draws the keep-out, and
`report()`'s every-pad-against-every-boss check uses the number.

**Expansion E1–E10 is gone.** Ten breakout pads used to bring 3V3, RUN, GP3,
GP22, GP4, GP5, GP20, GP21, GP27/ADC1 and GND out to pads beside their own Pico
pins. Nothing on this board or in the firmware used them; they were there in
case. What they cost was real: a column of pads down each wall, the three that
kept landing inside H4's boss, and the two pin-row gaps the test pads were
driven through when the pads had taken the space beside J6. Removing them frees
both walls, and the eight Pico pins they broke out are no-connect on the
schematic again. **The Pico is socketed** — anything those pads offered is
available on the header itself, which is the argument that should have applied
from the start.

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
- The VSYS/3 monitor on GP29 (ADC3) reads *after* D1: add ~0.35 V (0.3 V at
  idle current, 0.4 V with the radio on — PMEG3020ER-TP Fig. 4 at 30 and
  100 mA) to get the cell voltage, or calibrate against J1 pin 2 once. (This
  used to say 0.3 V beside a D1 paragraph that said 0.85 V; the diode has
  changed and the two now agree.) On a Pico 2 W GP29 is shared with the CYW43 SPI clock, so
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
| Track / clearance | 0.25 mm default, 0.4 mm on the `Rail` class (+3V3, AFE_3V3), 0.5 mm on the `Power` class (GND, VSYS, BAT+, SW_OUT, D1_K). 0.2 mm clearance throughout |
| Vias | 0.6 mm / 0.3 mm drill |
| Fab minimums enforced | 0.15 mm track and clearance, 0.3 mm drill, 0.3 mm copper-to-edge — inside every PCBWay 2-layer process |
| Pours | GND on both faces, inset 0.5 mm from the edge (PCBWay's routed-edge tolerance is ±0.2 mm). The **top** pour is the ground-plane electrode |
| Rule areas | the 20 × 30 cell/pad pocket bars bottom-face pour and bottom-face parts (tracks are allowed); each mounting hole carries its own 6 mm boss keep-out |
| Corners | every track corner is 45° or straight; `handoff.kicad_dru` fails the run on any right angle or stub segment |

**The `Power` net class was inert.** `gen_pcb.py` widened its nets to 0.5 mm,
but the project file it was widening them *in* only ever defined `Default` —
`gen_schematic.py` writes the `Power` class into `handoff.kicad_pro`, but
`pcbnew.BOARD.Save()` rewrites that same file from the blank project the board
was built under, on every run, and silently dropped it again. Every "0.5 mm"
track on the board was actually 0.25 mm. Fixed two ways: the net-class table
now lives once, in `gen_schematic.py`'s `NET_CLASSES`/`net_settings()`, and
`gen_pcb.py` calls `apply_board_settings()` to re-merge it into the project
file *after* `Save()`, every run, so it cannot go missing silently again. A
second class, `Rail`, was added at 0.4 mm for +3V3 and AFE_3V3: real current
there is under 5 mA, but 0.4 mm is what fits the lanes those nets actually
run in (see *Test points* below) — 0.5 mm did not clear both pads in the one
lane left past the antenna keep-out.

**The battery path was three crossings and six pin-row threads; it is now
one of each.** BAT+, SW_OUT, D1_K and VSYS used to zigzag between the
little-finger and thumb walls because SW1, D1 and JP5 were on the
little-finger side. They now live on the thumb wall, in the corridor between
J3 and SW1: J1 → BAT+ crosses the board once, to SW1; SW1 → SW_OUT → D1 is a
straight run down the same wall; D1 → JP5 → VSYS is a single via and one
threaded pin-row gap, straight into pin 39, which is on this row. At 0.5 mm
in a 0.94 mm gap the old crossings left 0.22 mm to each pad — no longer
acceptable once Power tracks were actually 0.5 mm (see above). The two
crossings that remain (BAT+, at y 41.55 and y 46.63) neck down to 0.4 mm for
2.6 mm through the gap, which restores 0.27 mm to each pad, and are otherwise
full width.

**J3 no longer overhangs.** Its housing is 5.85 mm wide; at pin 1 = x 3.0 the
body ran to x −0.93, 0.93 mm past the board edge. Pin 1 is now at x 4.0, which
puts the housing's own outer face flush with the edge (its courtyard starts
at x 0.05) — SW1's handle and U1's USB courtyard still overhang, by design,
and are the only footprints in `OVERHANG`.

**Every silkscreen and fab reference is checked against the outline, not
just eyeballed.** `report()` now walks every footprint's reference and value
text (and every loose `PCB_TEXT`) and fails the run if any of it sits within
0.5 mm of the board edge. H1–H4 no longer carry silk labels at all — a 3.4 mm
hole in a corner needs none, and the label had nowhere to go but off the
board; C4, C5, E3, D1 and the four connectors' texts were repositioned onto
the board or rotated to fit their strip.

**Every corner is 45° or straight.** `mitre()` chamfers every right-angle
join in a `ROUTES` polyline by 0.5 mm before it becomes copper (or half the
shorter leg, whichever is less), so the table can still be written on a grid.
Two DRC rules enforce it going forward: `track_angle (min 134°)` and
`track_segment_length (min 0.15mm)`, so a future edit that reintroduces a
right angle or a stub fails the run instead of passing quietly.

**Two custom DRC rules, in `handoff.kicad_dru`, exist because KiCad's
courtyard test has no notion of height**, and both suppress *only* courtyard
overlap — every clearance and short test still runs against these parts:

- *The Pico is socketed.* Its own PCB sits ~8.5 mm above this one, so the AFE
  and the bottom-face jumpers are deliberately underneath it.
- *Bare pads have no body.* The test pads (below) inherit a `TestPoint`
  footprint's 2.59 mm courtyard, so one placed hard against a jumper or a
  connector reports an overlap with a body that does not exist. Copper
  clearance is unaffected and still checked. Neither side of this rule may be
  a mounting hole — written without that half it also excused a pad sitting on
  a boss.

**DRC severities are tightened past KiCad's defaults.** `silk_over_copper`,
`silk_overlap`, `silk_edge_clearance`, `text_height`/`thickness`,
`connection_width`, `isolated_copper`, `copper_sliver`, `track_dangling`,
`via_dangling`, `hole_to_hole`, `holes_co_located` and the two corner rules
above are all errors now, not warnings — anything a fab would reject, or the
layout convention forbids, stops the run. `lib_footprint_mismatch` is the one
warning left at warning severity, and stays visible rather than silenced, so
a real mismatch could not hide among the ten deliberate ones.

**Silkscreen.** Values are hidden everywhere. References are on silk for the
things the bring-up procedure names in your hand — jumpers, connectors,
switches, U1/U2, D1 — and on the fab layer for the AFE's passives, where
0.8 mm text does not fit between 0603 pads on a 5.3 mm pitch. The test pads
are named by their **net** in printed legends (`ADC0`, `OUT1`, `VREF`, `PAD`,
`VSYS`, `D1K`, `GND`) rather than by reference, which is what someone holding
a probe is looking for. The silkscreen *outlines* of U1, the four JST
connectors, SW1, TP2/TP3 and the mounting holes are moved to the fab layer
too: U1's is a 21 × 51 box drawn over everything that deliberately lives under
it, and TP2's and TP3's rings ran into JP1's and JP3's outlines at the elbow.
(D1's went with them while it was an SMB whose bar reached TP7's mask; the
SOD-123FL's stops 0.7 mm short, so D1's cathode bar is printed again.) That is
what the thirteen `lib_footprint_mismatch` warnings are — the only warnings the
board reports, and all deliberate. A board-edge legend (project name, revision, date) is on
F.SilkS at the hand end and repeated on B.SilkS along the little-finger wall;
a numbered block of fabrication notes (stackup, finish, min track/space, the
drill list, the boss rule, the plot origin) is on `Cmts.User` beside the board, and
the sheet's title block carries the schematic's title, revision and date. The board
sits at `PAGE` (40, 35) on the A4 sheet; the aux origin is its top-left corner and
the fab pack is plotted from there, so every Gerber coordinate is a floor-plan one. J1/J2/J5's pin-1 ends are marked **+ / −** or
**PAD / GP** on F.SilkS, and J3's four pins are marked **R K G B**, so the
board can be assembled from the silk without the schematic in hand.

### The breakout pads moved out, and the channel routes

> The expansion pads were removed outright a session later — see *Expansion
> E1–E10 is gone* above. This section is kept because the channel geometry it
> settled (AFE columns at x 15.6 and 24.4, R2's position, the ammeter cluster
> at the board edge) is still the board's, and because it records why the pads
> were worth moving before anyone concluded they were worth keeping.

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

## The board routes

**0 unconnected items, 0 DRC errors, parity 0.** The last three connections
were all one fact, and the fix was one idea.

OUT1 leaves the amplifier at the top of the right-hand column and has to get
down to C1 and across to JP3. It used to do that on the top face, in the lane
outboard of the column, and then cut west across the channel at y 43.2. That
lane is the only way out of the AFE toward the elbow - the top face is walled
at y 43 by C1 and C2, the bottom by BAT+ and SW_OUT - and OUT1 was drawing a
line across it. AFE_3V3 (to JP4 and to C5) and OUT2 (to JP3) were behind it.

The lane is 3.05 mm wide, which is three tracks with room to spare. What it
cannot take is a crossing, so the three are ordered, innermost to the net that
enters lowest, and each entry crosses only lanes that have not started yet:

| lane | net | enters at |
|---|---|---|
| x 25.7 | OUT2, R7 to JP3 pad 3 | y 36.1 |
| x 26.4 | AFE_3V3, C3 to JP4 and to C5 | y 34.1 |
| x 27.2 | OUT1, R4 to C1 and JP3 | y 28.6 |

OUT1 then leaves the lane by diving. Between BAT+ (y 41.55) and SW_OUT
(y 44.09) the bottom face is empty across the whole channel, and that corridor
lands on C1's OUT1 pad at y 43.36. So OUT1 goes down at the end of its lane,
west along the corridor, and up into C1: two vias, and nothing on the top face
crosses the channel between the amplifier and the elbow any more. A third via
at x 21 takes the JP3 branch back up, where the top face is clear to the jumper.

With the crossing gone, R8's VREF pad no longer has to dive under it, so that
via went; and a second VREF via at y 52.5 turned out to have nothing on the
top face to meet - DRC had been calling it dangling - so that went too.

**Vias: 16 became 21, and the electrode pour lost 28 mm².** The earlier note
here said nothing would be bodged through the electrode pour, so, plainly:
the top pour measures 1493.9 mm² filled against 1521.7 before, a 1.8 % loss.
About 4 mm² of that is the five extra vias (0.6 mm plus 0.2 mm clearance, a
1.0 mm gap apiece); the rest is clearance around the three east lanes and the
5 mm of AFE_3V3 on top at the hand end. Four of the new vias sit under the
Pico in the AFE channel; one is at the hand end for C5. If that is not acceptable, the
alternatives are the ones listed before - reorder the right-hand column so C3
and R7 sit above OUT1's exit (free, but moves the decoupling cap off pin 8), or
0603 passives in that column only (R4, R7, R8, R9, R11, C3; none dissipates
more than 8 mW, so 1/10 W is fine) - and either would let OUT1 stay on top.

AFE_3V3's run to C5 is on the bottom face. Past the antenna keep-out (to
x 27.11) and before the Pico's pads (from 28.1) there is one lane, at x 27.5,
and on the top face RX_IN already crosses it at y 21.23. The bottom is free
the whole way - the pocket has no pour - so the run stays down the whole
lane and comes up once, at the hand end, into C5's + pad. (This paragraph
described the state right after routing closed; the battery path, BTN, VREF
divider and the test pads have all moved since - see the sections below.)

## This session: net classes, the battery path, corners, and test points

Four things were known to be wrong going in (README §0 of the layout brief);
all four are fixed, plus the test points the previous session left for later.

**The `Power` net class was never real.** `gen_schematic.py` wrote it into
`handoff.kicad_pro`, but `pcbnew.BOARD.Save()` rewrites that file from the
blank project the board was built under on every `gen_pcb.py` run, and
silently dropped the class again - every "0.5 mm" track on the board was
actually 0.25 mm, including BAT+ and SW_OUT threading the 0.94 mm gaps
between Pico pads with 0.22 mm to spare either side. Fixed at the root: the
net-class table now lives once, in `gen_schematic.py`, and `gen_pcb.py`
re-merges it into the project file after every `Save()`. A `Rail` class
(0.4 mm) was added for +3V3/AFE_3V3, sized to what their lanes actually hold.

**The battery path crossed the board three times; it now crosses once.**
BAT+, SW_OUT, D1_K and VSYS zigzagged between walls because SW1, D1 and JP5
sat on the little-finger side, opposite J1. They now sit on the thumb wall
with J3, in the run between it and SW1 - see *The board*, above, for the
routing. The two pin-row crossings that remain neck to 0.4 mm through the
gap and are 0.5 mm either side of it.

**J3 no longer overhangs the thumb edge**, SW_OUT no longer U-turns around
its own pad (it left with the whole battery path), the JP4 redundant loop
went with the little-finger-side routing it was part of, and **every track
corner is 45° or straight**, enforced by two new DRC rules rather than by
eye. +3V3 came off the little-finger wall entirely: it now reaches R15 by
crossing the board once, at the elbow, instead of running the full 47 mm
edge at 0.375 mm clearance. (R15 has since moved under the Pico, onto that
branch's own row — see the last *This session*.)

### Test points: all eight, TP1–TP8

Priority order from the layout brief, all through-hole `TestPoint_Pad` so
either face can be probed, each with a silk label:

| Pad | Net | What it tells you |
|---|---|---|
| TP1 | GND | scope earth, beside U2's input stage, in the top pour |
| TP2 | ADC0 | what the Pico actually samples |
| TP3 | OUT1 | stage 1 output, before stage 2 |
| TP4 | VREF | the 1.65 V bias, beside JP6 |
| TP5 | /PAD | the receive node - **10× probe or better only**, it loads a 10 MΩ node |
| TP6 | VSYS | ammeter +, after JP5 |
| TP7 | D1_K | ammeter −, before JP5 (TP6/TP7 replace the old "clip JP5's own pads" position) |
| TP8 | AFE_3V3 | the analogue supply behind JP4, at the hand end |

**TP8 was the one that got away, and it is back.** It was dropped last session
because C5 was a radial electrolytic whose through-hole + lead was already a
better probe point than a pad would have been. C5 is an 0805 MLCC now, so that
lead is gone and the argument with it. TP8 sits *in* the AFE_3V3 run at
(27.5, 8.2) rather than on a stub off it — a branch at a pad is two segments at
90°, which the `track_angle` rule forbids, and a test point in series is what
one is for anyway. JP4 pad 2 is still on the same net if a jumper-point reading
is wanted instead.

## This session: the haptic motor, 0603/0805 passives, and the boss clash

### The motor: M1, Q1, D3 and J6

A 10 mm coin motor (MakerBazar 522260) draws on the order of 100 mA and is an
inductive load, so it is switched low-side by an AO3400A (R209179) rather than
from a GPIO. The whole block is four parts and one connector:

| Ref | Part | Why |
|---|---|---|
| Q1 | AO3400A, SOT-23 | low-side switch. 30 V / 5.2 A and 27 mΩ at 4.5 V is enormous overkill for 100 mA, which is the point: it is fully on at 3 V of gate drive |
| R16 | 100 Ω 0603 | gate series. Limits the GPIO's peak gate-charge current |
| R17 | 100 kΩ 0603 | gate pull-down. **Not optional** — GP28 is an input from power-up until firmware writes it, and through every BOOTSEL reset. Without R17 the gate floats and the motor may run |
| D3 | PMEG3020ER-TP, SOD-123FL (was SS220F, SMB, when this was written) | flyback. Cathode to VSYS, anode on the drain. Same SKU as D1 — one diode to order, one to stock |
| J6 | 2 × 2.54 mm through-hole | the motor's leads solder in, or a 2-pin header takes a plug — the same choice the LED has at J3 |

**VSYS, not +3V3.** The Pico's own regulator carries the RP2350, the CYW43439
radio and — through JP4 — the whole analogue side. A 100 mA pulsed load on that
rail would put motor current through the same supply that feeds AFE_3V3. VSYS is
2.6–3.8 V, which is what a 3 V coin motor wants, and D1 already protects it
against a reversed cell.

**GP28 (Pico pin 34) drives it.** Of the three free GPIOs on the thumb row —
GP16 (pin 21), GP22 (pin 29) and GP28 (pin 34) — GP28 is the one nearest the
elbow, which is where VSYS arrives at pin 39. GP28 is ADC2 and nothing else
wanted it.

**Where it sits.** Bottom face, x 12–18 between the Pico's thumb pin row and
JP1, below JP4 — the last free ground on the board, and the right place
electrically: pin 39 is two millimetres away, so the motor's current never
travels the length of the board, and the block is 10 mm of board and a ground
pour away from the AFE channel. D3 is on the **same face as J6 and Q1**, east
of them, so the flyback loop is four segments of bottom-face copper with no via
in it at all. The gate chain (R16, R17) runs on the top face, where the
lane past the pin row is uncontested — on the bottom, VSYS and BAT+ already own
it.

R17 is placed so the gate run enters its pad 1 from the north and leaves it
south: a shunt part the run passes straight *through*, not a T. A branch at a
pad is two segments at 90°, which the `track_angle` rule forbids.

J6, Q1 and D3 carry no reference on silk — there is no room for three more
labels between JP4's and the JP1-8 legend — but D3's cathode bar and Q1's
pin-1 mark are printed, because a hand-assembled diode needs its band. The
`MOT` legend beside JP4 names J6, whose pin 1 is its square pad.

> **Firmware:** GP28 high runs the motor. Do not run it during an RX window —
> 100 mA of commutating motor on VSYS is not what a ×121 front end wants to see.

**D3 is the same SKU as D1** — one Schottky to order and one to stock instead
of two. When this was written that was the SS220F in SMB, and it did not fit
when first tried: SMB's courtyard is 7.3 × 4.5 mm against SOD-123's 4.7 × 2.3,
and at the time the motor block was a 6 × 9 mm pocket with J6, Q1 and the two
test pads in it, so the body swallowed J6's pad and clipped two solder masks.
The pocket is bigger than that — the test pads had no business in it, and they
are elsewhere now (see *The test pads had taken the motor's pocket* below).
*Both diodes are the PMEG3020ER-TP in SOD-123FL now (4.9 × 2.6 mm of
courtyard), so the courtyard argument is void — see the last* This session.

### The passives: 0603 where the order changed, 1206 where it did not

> Superseded: **every passive is 0603** since the 12 Sep 2026 order (the last
> *This session*). The table below is what this session did; the 1206 and 0805
> rows have since moved too.

| Value | Refs | Package | Note |
|---|---|---|---|
| 100 kΩ | R4, R6, R7, R10, R11, R17 | **0603** | MakerBazar 1481824519-20P |
| 10 kΩ | R5, R8, R15 | **0603** | MakerBazar 1481824531-20P |
| 100 Ω | R12, R13, R14, R16 | **0603** | MakerBazar 1860310035-20P, replacing the 330 Ω LED resistors |
| 10 µF | C4, C5 | 0805 MLCC → **0603** | was MakerBazar 1491785836-10P, replacing the 5 mm radial electrolytics; now Murata GRM188R61E106MA73D |
| 1 MΩ, 1.5 kΩ | R1, R2, R3, R9 | 1206 → **0603** | not re-ordered then; re-ordered 12 Sep |
| 330 pF C0G, 100 nF X7R | C1, C2, C3, C6 | 1206 → **0603** | the first 0603 order did not cover these dielectrics; the second did |

Two package sizes on one board was deliberate at the time, not an oversight:
only the values that had been re-ordered moved. There is one package size now.

**What a package change actually costs.** A 1206 hand-solder pad sits 1.55 mm
from the body centre and an 0603 one 0.9125 mm, so every one of these parts
pulled both its pads 0.64 mm inward — and every route that ended on one was a
literal coordinate in `ROUTES`. That is twenty-odd track ends off their pads at
once. The table now says which *pad* is meant, `P("R6", "1")`, and `gen_pcb.py`
asks pcbnew where it is after placement; `dx`/`dy` give a point a fixed offset
from a pad (the start of a 45, a lane that must stay in line with a pad row).
Lane coordinates that are not pads are still literals. The next package change
costs nothing.

Three things the 0.64 mm shift broke, all of them found by DRC and the pour
check rather than by eye:

* **OUT2's east lane** moved onto the band FB2 used to cross the channel in.
  FB2 now goes to R7's pad, which is on the way, instead of round to R8's — the
  two are already joined by the x 24.4 link below.
* **The pour channel under R5** closed. It is 0.25 mm wide, between R5's pad 1
  and FB1's run under U2's body, and it is the AFE pour's only way out to the
  rest of the electrode. Closed, C3's decoupling return to U2 pin 4 became a
  40 mm² island. R5 moved 0.64 mm toward the hand — exactly as much as its pads
  moved inward — and VREF now leaves its return pad along the pad row before
  turning up the lane, instead of straight out at 45° into the channel.
* **C4 and C5 changed face.** Every track that fed them ran on the bottom and
  reached them through the electrolytics' through-hole leads. As 0805 MLCC they
  have no leads, so they move to the bottom face, each anchored on pad 1 at the
  exact point the old part's + lead stood. Both nets' routes are unchanged, and
  the top face gets 26 mm² of electrode back.

**The 10 µF is a real part now: Murata GRM188R61E106MA73D, X5R, 25 V, 0603.**
The 0805 it replaces was "marked 100 V", which no 0805 10 µF is — the listing
was wrong about the voltage and said nothing about the dielectric, so the only
safe assumption was "perhaps 5 µF in circuit, worst case 3". DC-bias derating
is a field effect: what matters is the applied voltage as a fraction of the
rating, and 3.3 V on a 25 V part is 13 %, against 33 % on a 10 V one. Murata's
curve for this part gives **~8 µF effective at 3.3 V**, and ~5 µF at the
tolerance-and-temperature worst case, instead of ~5 and ~3. Both are still
hold-up and bulk, neither is a filter corner — but the number can be used now.

**100 Ω on the LEDs is a real change, not a like-for-like.** At 3.3 V into a red
LED (Vf ≈ 2.0 V) 100 Ω asks for 13 mA where 330 Ω asked for 4 mA. The RP2350's
per-pin maximum is 12 mA, and the pin's own output impedance at that setting
(~40 Ω) holds the real current to about 9 mA — so it works, and the LED will be
noticeably brighter. Green and blue (Vf ≈ 3.0–3.2 V) barely change. If the red
channel is too bright, or the drive strength is left at 2 mA, that is a firmware
PWM question, not a resistor one.

### The breakout pads were sitting on a mounting boss

> The pads are gone now, but **both checks this section added are still live**
> and still worth having: they are what a future part placed near a corner will
> run into. The boss they were sitting on is 6 mm across now, not 8.

E3, E4 and E10 broke out Pico pins 1, 2 and 3 (GP0, GP1, GND) at x 31.44 on the
little-finger wall. A pad there reaches x 32.19, which is 3.31 mm from H4's
centre — **inside the 8 mm boss the M3 screw stands in**. Nothing could be
soldered to them and nothing could reach them.

DRC did not see it for two reasons, both now fixed:

* the mounting-hole footprint's keep-out says `(pads allowed)`, because the
  hole's own NPTH pad sits in the middle of it and would report itself. The
  boss test is now an explicit geometric check in `gen_pcb.py`'s `report()`:
  every pad on the board against every hole's boss circle, at whatever radius
  `BOSS_R` currently says.
* the `Bare pads have no body` DRC rule excused a courtyard overlap if *either*
  side was a breakout or test pad. A test pad genuinely can sit inside a
  switch's courtyard, so that half stays — but neither side may now be a
  mounting hole, and the overlap against H4 is an error.

Both checks were regression-tested by putting E3 and E4 back on pins 1 and 2:
`IN BOSS E4.1 ... 3.35 mm from the hole`, plus two `courtyards_overlap` errors
and a `pth_inside_courtyard`.

The little-finger wall is clear only between J5's housing (ends y 40.24) and
that boss (starts y 53.5), which is pins 5–8. So the ten breakout pads are now
3V3 (36), RUN (30), **GP3 (5)**, **GP22 (29)**, GP4 (6), GP5 (7), GP20 (26),
GP21 (27), GP27/ADC1 (32), **GND (8)**. GP0 and GP1 — UART0 — are the loss;
GP3 (SPI0 TX) and GP22 replace them, and the ground pad stayed, moved from
pin 3 to pin 8. GP1's slot went to the thumb row because the little-finger band
only holds four.

### The fab pack, and the checks that were still missing

`gen_pcb.py` now ends by writing `build/plot/`: Gerbers for every layer, both
Excellon drill files with their maps, and the job file. It is regenerated on
every run, so the pack cannot go stale against the board.

**Drill table** (`build/drills.md`, regenerated each run):

| Drill (mm) | Holes | Plating | Used by |
|---|---|---|---|
| 0.30 | 20 | PTH | via |
| 0.70 | 8 | PTH | TP1, TP2, TP3, TP4, TP5, TP6, +2 more |
| 0.95 | 4 | PTH | J3 |
| 1.00 | 51 | PTH | J1, J2, J5, J6, SW1, U1 |
| 1.10 | 4 | PTH | SW2 |
| 1.15 | 2 | PTH | SW1 |
| 3.40 | 4 | NPTH | H1, H2, H3, H4 |

**The solder-mask web check was off.** `solder_mask_min_width` was 0.0, which
disables the minimum-dam test outright. It is 0.1 mm now — the dam PCBWay's
green mask holds — and JP1–JP8's 0.3 mm pad gaps, the MSOP-8's 0.25 mm ones and
everything the new parts added clear it.

**AGND got an explicit tie.** U1 pin 33 used to reach the electrode through the
0.96 mm pour sliver between the Pico's thumb pads and the ADC0 lane. MOT_DRV
leaving pin 34 crosses that sliver — 0.25 mm of track plus two 0.2 mm clearances
is 0.65 mm of it — so AGND now runs west on the bottom face, out of the cell
pocket, into the bottom pour, and the sliver is removed by a rule area rather
than left to fill as an isolated island with a starved thermal relief. That is a
better ground than the sliver was.

### D1: THT DO-41 to SMD SMB (SS220F)

D1 moved from a 1N5819 in a through-hole DO-41 to an SS220F (Slkor, 200 V 2 A)
in an SMB (DO-214AA), the same SKU as the motor's flyback diode — see
*Deviations* for why D3 could not follow it. The package change broke three
things, all of them the same root cause D3 hit too: a THT pad is copper on
both faces, so both approach tracks used to just end on the pad; an SMD pad is
F.Cu only.

* **Both approach tracks were stranded on B.Cu.** SW_OUT and D1_K used to
  route to D1 on the bottom face and land straight on the THT pad. Fix: each
  track's *existing* 45° bend point became a via, with a short F.Cu stub
  continuing on to the pad. Reusing the bend point (rather than a fresh via
  right at the pad) matters — the angle a via's two tracks meet at is checked
  by the `track_angle` DRU rule the same as any other corner, so the via had
  to land somewhere the geometry was already a legal 45.
* **D1_K's own pad location has a false symmetry.** `PLACE["D1"]` anchors
  pad 1 (cathode) at a fixed point regardless of footprint, so D1_K's two
  branches still reached the right x,y — but pad 1 on SMB is the *far* end of
  a symmetric 4.3 mm pad-to-pad pitch, not the fixed reference end of an
  asymmetric 7.62 mm DO-41 (pad 1 at the origin, pad 2 7.62 mm away). Anode
  (pad 2) moved from 7.62 mm to 4.3 mm out — SW_OUT's approach track needed
  its final stub shortened to match, not just re-layered.
* **The cathode-bar silk clipped TP7's mask.** SMB's body outline (with its
  cathode bar, on F.SilkS) is bigger than SOD-123's and now reaches past
  TP7, 1.4 mm away. D1 joined `SILK_STRIP` — its outline moves to the fab
  layer, like SW1's and the connectors'. The part's own moulded band still
  marks polarity for assembly; only the on-board copy of it is gone.

## This session: E1–E10 removed, a 6 mm boss, and the elbow untangled

The previous session left the board with **51 DRC errors**. It had been asked
to move D3 to the SS220F (SMB) like D1, and there was no room for it: the SMB
went where TP2 and TP3 were, TP2 and TP3 were pushed out through two gaps in
the Pico's pin row to the little-finger wall, and on that wall they landed on
top of the breakout pads. Four vias, two necked pin-row crossings and two
shorted pads, to make room for one diode. This session took the space back
instead of routing round the lack of it.

### E1–E10 are gone

Ten through-hole pads on nets nothing used, occupying a column down each wall.
Three of them (GP0, GP1, GND) had spent three sessions sitting inside H4's
boss. They are removed from the schematic, the board, the DRU rule and the
parts list; the eight Pico pins they broke out (GP3, GP4, GP5, GP20, GP21,
GP22, GP27, RUN) are no-connect again, which is what the sheet should say about
a pin that goes nowhere. **The Pico is socketed** — its own header is the
breakout, and always was.

The netlist's **named** nets go 41 → 33. The total stays 56, because every pin
that lost its breakout pad becomes an explicit no-connect and KiCad emits an
`unconnected-` net for it — which is the right trade: eight nets that existed
only to reach a pad become eight pins the sheet says nothing is attached to.
The 0.7 mm drill count goes 18 → 8, and the board is 93 holes in 7 sizes.

### The boss is 6 mm, and the screws moved to the corners

8 mm was never a requirement — 6 mm holds the 5 mm brass insert with a wall
round it. The old boss reached 4 mm in from two edges at once, which is why
each hole sat 4.5 mm in and why the little-finger wall was only usable between
y 40.24 and y 53.5.

Hole position is **derived** now rather than written out four times: each
centre sits `BOSS_R + BOSS_EDGE` (3.0 + 0.5) from both its own edges, so the
boss keeps exactly the half-millimetre of wall it had before and the screws
move from (4.5, 4.5) to (3.5, 3.5) and its three mirrors. The footprint's name
carries the diameter (`MountingHole_3.4mm_M3_Boss6mm`), so a board built
against a stale footprint cannot quietly claim a keep-out it does not have.

> **Enclosure:** the four mounting bosses move 1 mm diagonally outward and
> shrink by 2 mm. The printed part has to follow — this is the one change here
> that reaches outside the PCB.

### The test pads had taken the motor's pocket

TP2 and TP3 were at (19.6, 58.0) and (23.5, 58.0) — in the middle of the only
6 × 9 mm of board where the motor block fits. **Test pads are placed last
now**: into space the layout has already settled, never the other way round.
Both sit *on* their own net's existing lane, each replacing a via that was
there anyway, so neither costs a track, a corner or a pin-row crossing:

| Pad | Was | Is | On |
|---|---|---|---|
| TP2 | (19.6, 58.0), then (31.0, 49.17) via two vias and a pin-row gap | (18.8, 54.0) | JP1's own ADC0 pad row, 2 mm west, in the band between AFE_3V3's crossing at y 51.7 and MOT_SW's at y 55.5 |
| TP3 | (23.5, 58.0), then (31.0, 44.09) via two vias and a pin-row gap | (21.0, 48.6) | OUT1's own change of face on the way into JP3 |

Two lanes shifted by well under a millimetre to give TP3 its pad: ADC0's runs
at x 19.8 instead of 20.85 (at 20.85 it passed 0.08 mm from the pad) and OUT1's
steps 0.8 mm west below its corridor via. TP3 also has to clear **JP3's pin-1
arrow**, whose silk reaches y 48.94 at x 22.0 — which is what fixes TP3's x at
21.0 and not 21.8. TP2 and TP3 join `SILK_STRIP`: their own silk rings ran into
JP1's and JP3's outlines, and a ring says nothing the printed net name beside
it does not.

### The flyback loop has no via in it

D3 (SS220F, SMB) sits at (21.5, 58.8) on the **bottom face with Q1 and J6**,
lengthways, east of both. On the bottom face rot 180 puts pad 1 (cathode) west
and pad 2 (anode) east — footprints are mirrored in x down there, so 180 means
what 0 means on top — and cathode-west is the whole trick:

J6 stacks its two holes with MOT_SW (y 53.7) **north** of VSYS (y 56.24), while
Q1 is **south** of both. Whichever of those two nets runs the full width of the
pocket, the other has to cross it. With the cathode west, VSYS stops at x 19.35
— 4.5 mm short of the east wall — and MOT_SW goes round the outside of it in
two legs that meet at D3's anode: north at y 55.5 between JP1's pads and D3's
body, south at y 61.2 just clear of D3's courtyard into Q1's drain. J6's
switched hole reaches Q1 *through* the anode pad, so there is no separate leg
and no via anywhere in the loop.

The first attempt at this had the cathode east and put a right angle and a
short between VSYS and MOT_SW. The orientation is the fix, not a detour.

### Everything else

* **The schematic's bottom band was three blocks deep in one column.** The
  test-pad block was laid out from x 222 and the button/role/mounting block
  from x 238, on the same row, so TP2 and TP3 were drawn inside SW2 and the
  second test-pad column ran through H1–H4. The test pads now occupy the
  column E1–E10 vacated (x 150) and the button block has the right-hand third
  to itself.
* **Nothing overlaps the sheet border or the title block.** The title block
  owns x ≥ 236, y ≥ 199 in sheet grid units; the mounting-hole caption was
  sitting on its top rule, and the haptic notes ran into its left edge. Both
  moved, and the hole row moved with them.
* **TP4 and TP6's power symbols were drawn on top of their own references.** A
  `power` symbol at rot 270 labels *above* its anchor, which is where a test
  pad's reference text sits; the two upward ones get four more grid units of
  lead than the GND ones.

**Result:** ERC 0, netlist 0 missing / 0 unexpected, BOM 0 differences against
the parts table; DRC **0 errors**, parity 0, unrouted 0, independent short
check 0 pairs, no `NET`, `OUTSIDE` or `IN BOSS` lines. The 14 remaining
warnings are all `lib_footprint_mismatch`, one per footprint this board
deliberately strips silk from or re-places reference text on.

## This session: every passive 0603, and the SOD-123FL Schottky

A parts order (Robu, 12 Sep 2026) moved the last 1206s and 0805s to 0603 and
replaced the SS220F on D1 and D3 with a smaller Schottky. Nothing touches a net:
`EXPECTED_NETS` is unchanged, 56 nets, and it was the oracle for every step.

| Refs | Was | Is | Robu |
|---|---|---|---|
| C1, C2, C6 | 330 pF C0G 1206 (KEMET) | **CL10C331JC8NNNC** Samsung, 330 pF C0G/NP0 ±5 % 100 V, 0603 | R136892 |
| C3 | 100 nF X7R 1206 | **CL10B104KB8NNNC** Samsung, 100 nF X7R ±10 % 50 V, 0603 | R172322 |
| C4, C5 | 10 µF "marked 100 V" 0805 | **GRM188R61E106MA73D** Murata, 10 µF X5R ±20 % 25 V, 0603 | R144171 |
| R1, R2, R3 | 1 MΩ 1206 | **0603WAF1004T5E** Uniohm, 1 MΩ ±1 % 100 mW 75 V | R134792 |
| R9 | 1.5 kΩ 1206 (Yageo) | **0603WAF1501T5E** Uniohm, 1.5 kΩ ±1 % 100 mW 75 V | R134878 |
| D1, D3 | SS220F Slkor on `D_SMB` | **PMEG3020ER-TP** Tech Public, 40 V 2 A, SOD-123FL on `handoff:D_SOD-123FL` | R241663 |

C6 stays DNP and stays the C2 part. Everything else on the board is untouched.
There is **no 1206 and no 0805 left**; every passive is 0603.

### The diode is not the Nexperia part

**PMEG3020ER-TP is a Tech Public part that borrows Nexperia's number.** It is
40 V (Nexperia's is 30 V), 2 A, 50 A surge, C<sub>J</sub> 100 pF at 4 V,
R<sub>θJA</sub> 200 °C/W; V<sub>F</sub> 0.41 V typ at 1 A and 0.5 V max at
2 A, with no 100 mA line item (Fig. 4 reads ~0.35–0.4 V there); I<sub>R</sub>
2 µA at 10 V, 100 µA max at 40 V. And it is **SOD-123FL**, where Nexperia's is
SOD-123W — a re-order against the Nexperia number arrives in the wrong package,
which is why the parts table names Tech Public and the package in the same
cell. Nothing in this README quotes the Nexperia datasheet.

**The SS220F was never an SMB.** Robu's own listing calls it SMAF (DO-221AC).
`D_SMB`'s land pattern was oversized for it, which is why both diodes had so
much room and nobody noticed. Moot now, recorded so the next person reading
"SMB" in the older sessions knows what was actually on the bench.

### `handoff:D_SOD-123FL`, written by `tools/gen_sod123fl_footprint.py`

KiCad 10 has no SOD-123FL. `D_SOD-123`, `D_SOD-123F`, `D_SOD-128` and
`Nexperia_CFP3_SOD-123W` are all the wrong size: across the outline drawing's
tolerance band (body 2.5–2.9 long, 3.4–3.9 tip to tip, terminals 0.35–0.9 long
and 0.7–1.2 wide) the terminal sits anywhere from 1.25 to 1.95 mm from the
centre, and none of those four covers that with a fillet. The generated one has
two 1.2 × 1.6 mm rect pads at ±1.65 (copper from 1.05 to 2.25), a 2.9 × 2.0
body on F.Fab with the terminals drawn, a 4.9 × 2.6 courtyard and the cathode
bar on silk past pad 1's copper. Pin 1 = cathode at −x, like every KiCad `D_*`,
so the symbol's pin map did not change. `--prove` puts it on a 16 × 10 mm test
board with a track into each pad and a pour round it, runs DRC (0) and renders
`build/fp_test.png`; that was looked at before the real board was built on it.

D1 and D3 needed **no route change**. Both were anchored on a pad, and every
approach — the two D1_K vias, the SW_OUT via, VSYS's 45 into D3's cathode,
MOT_SW's two legs round the anode — was written against `P("D1", …)` /
`P("D3", …)`. The one literal in the flyback loop, `(21.25, 61.2)`, became
`P("D3", "2", -2.4, 2.4)` so the 45 out of the anode stays a 45 at any pitch.
The freed courtyard (7.3 × 4.5 → 4.9 × 2.6 at each) went back to the pours,
which are not islands: **top 1611.0 → 1629.9 mm², bottom 1154.8 → 1178.8**.
D1's silk outline is printed again (it was stripped because the SMB's bar
reached TP7's mask; the SOD-123FL's stops 0.7 mm short), which takes the
deliberate `lib_footprint_mismatch` count from 14 to 13.

### What the 0.64 mm shift broke this time

Eight parts pulled both pads 0.64 mm inward (0.6875 for the capacitors: KiCad's
0603 capacitor pad is at 0.8625, the resistor's at 0.9125 — the brief for this
session had the capacitor footprint's name wrong, and ERC caught it). The route
ends followed, because they are pads; the geometry around them did not. Four
things, all found by the checks, none by eye:

* **C1 sat on the VREF bus.** VREF crosses the left column at y 41.6 *under
  C1's body*, between its pads. A 1206 left 1.32 mm for it; anchoring C1's pad
  on OUT1's corridor put its other pad on the track — a short, a mask bridge and
  a crossing. An 0603 still clears it, 0.2625 mm each side, but only with C1's
  origin exactly on the bus (`Y_VREF_X`); OUT1's corridor is now derived from
  where C1's pad then lands (`Y_OUT1`, one 45 off the pad) instead of the other
  way round.
* **The AFE pocket lost its only exit, again — a different one.** The pour
  round U2 pin 4 and C3's ground pad gets out west between R3.1 and R5.1, up
  the strip between the VREF lane and the column, and then east **between R3's
  two pads** into the pour north of the island. With a 1206 R3 that gap was
  1.35 mm and VREF's 45 into R3.2 from the south-west passed above it; with an
  0603 the gap is 0.775 mm and the same 45 ran straight through it. C3.2 and
  pin 4 became a 60 mm² island (the pour reported 1569 mm² before anyone looked).
  VREF now enters R3.2 flat from the lane, and the pocket is filled. The R5
  channel the last session closed is still open — C3 moving did not touch it.
* **C6 could no longer straddle BAT+.** It sat across BAT+'s run south from J5
  with a 1206 pad either side of the 0.5 mm track; an 0603's pads are 1.05 mm
  apart, 0.17 mm to each. It stands on end below JP2 now, each pad straight
  under the JP2 pad on its own net — on the board as on the sheet, the cap is
  across the jumper — with GP11_TX's last leg one 45 and BAT+ untouched.
* **A right angle appeared where R3's pad used to be.** VREF's two 45s met at
  R3.2's old centre; when the pad moved, the corner it had excused became a
  bare 90° and the `track_angle` rule caught it. Both legs are written against
  the pad now.

And what was moved on purpose: **R2** went from y 24.0 to 25.0, as far toward
U2 as pin 2's mitred escape allows (1.2 mm above it), so its HIZ pad is nearer
pin 3 rather than further; the island — R3.1, R2.2, pin 3 — is **8.5 mm of
trace, 9.2 before**, and is written entirely against pads. **R9**'s 45 into
JP3's centre pad lands 0.09 mm lower, so the via moved with it (`JP3C_VIA`).
**R1** is anchored on its PAD pad, which is the top of the x 38.05 lane. C2 and
C3 are anchored on the pad their lanes leave, so ADC0's two lanes and the
AFE_3V3 lane did not move. C4/C5 were already anchored on pad 1; pad 2 is GND.

### Why these parts, for the next person who is tempted

* **C1/C2/C6 are C0G, not X7R.** X7R is piezoelectric and **this board has a
  vibration motor on it** — a class-2 dielectric across the receive chain is a
  microphone. C0G also holds the 321 kHz corner (±30 ppm/°C against X7R's
  ±15 % over temperature) and has ~4× less dielectric absorption, which matters
  on C2, the DC block across the TX→RX transition.
* **C4/C5 are 25 V, not 10 V.** See *The 10 µF is a real part now* above:
  derating is a field effect, 13 % of rating against 33 %, ~8 µF in circuit
  instead of ~5.
* **R2/R3 to 0603 is a judgment call the owner made, knowingly.** An 0603's
  1.6 mm body flashes over at a lower voltage than a 1206's 3.2 mm, and J2 is
  skin contact — R2 is the resistor between the pad and the amplifier. Accepted
  for a bench/research board; it is written down here so that if this board
  ever becomes something a stranger wears, the question is asked again.

### Routes tidied: one leg and one 45, not a flight of steps

Eleven routes had a jog, a stub or an off-45 leg that the package changes had
left behind or that had never been cleaned up; 18 segments fewer, no net or
lane moved:

* **Pin 2's two escapes.** North was up, east, a 0.8 mm 45, east; now up and
  one 45 onto R4.1's row. South (FB1) was a U-turn — down, west, back *up* a
  vertical, west into R5.1 — because R5.1 is on pin 2's own row with pins 3
  and 4 between them. It is down, west past pin 4, one 45 up onto the row, in.
  The 45 lands at x 16.9, deliberately not on the pad: straight into R5.1 it
  passes 0.1 mm from R5.2's corner and closes the pour channel beside R5.2,
  and a vertical that reaches pin 7's column walls the C3 pocket off from that
  channel — both were tried, both cost the pocket its exit.
* **Pin 6 to R7.1** was down, east, down 1.2 mm, east; now down and one 45.
* **Pin 1's OUT1** leaves the pad with a 45 onto R4.2's row instead of a
  mitred right angle; **VREF out of R8.2** is one 45 up to the bus instead of
  west-then-north; **ADC0 into pin 31** and **AGND out of pin 33** are true
  45s (AGND was 50°); **GP28 into R16** is one 45 off the pin.
* Stubs gone: 0.25 mm at R11.1 (now one 45 into JP6's pad), 0.34 mm at J6
  (VSYS's 45 runs all the way to J6's row), 0.45 mm at ADC0's drop (the via
  moved to where the 45 into JP1's pad starts, `ADC0_VIA`). AFE_3V3's leg
  into C5.1 was 52°, not 45; BTN had a redundant collinear point.

### BOM

`hardware/bom.csv` was committed with a **Package** column derived by hand from
the footprint. `tools/gen_bom.py` derives it now — from the footprint name, or
for BT1/D2/M1 from what they are — writes the same seven columns grouped by
Value+Footprint, and is called by `gen_schematic.py` after its BOM check, so it
regenerates with everything else. It prints the set of passive packages it saw,
which must read `0603`.

**Result:** ERC 0, netlist 56 nets / 0 missing / 0 unexpected, BOM 0 differences
against the parts table; DRC **0 errors**, 13 warnings (all
`lib_footprint_mismatch`), parity 0, unrouted 0, short check 0 pairs, no `NET`,
`OUTSIDE` or `IN BOSS` lines; 20 vias, 93 holes in 7 sizes, fab pack 17 files.

## This session: five things seen in the layout

Each is a look at the board rather than a change of plan; the netlist and the
parts are untouched.

* **AGND's tie out of pin 33 is straight west.** The cell pocket (x 10–30,
  y 16–46) has no bottom pour, so pin 33 cannot simply sit in the pour: it
  needs a track out of the pocket. That track was one 45 south-west; it is
  now one straight leg west, one pitch, `AGND_VIA = (8.58, 42.82)`. The via
  at its end is kept: the pour alone would connect the track, but the via is
  the stitch to the *top* pour — where U2 and the AFE's returns are — 2.5 mm
  from the AGND pin; the nearest other stitch is 10 mm away at (7, 33).
* **TP7 is east of D1, on the cathode pad's own row, with no via.** It was
  west of D1 at (1.9, 31.5), reached by a bottom-face leg and a via that were
  left over from D1's through-hole days. A through-hole test pad is on both
  faces, so the tie is one straight F.Cu leg out of pad 1 to (7.3, 29.8) —
  where its courtyard clears D1's (to x 5.84) and the pad stays 1.5 mm off
  the VSYS lane at x 9.79. The `D1K` label moved with it. One via fewer: 19.
* **BTN leaves pin 20 at 45, straight out of the pad.** It used to go 1 mm
  north first and then 45: that put the diagonal 1.2 mm from TP8's centre,
  one clearance off the pad. Straight out of the pin it passes at 1.9 mm.
  The jog at x 25.6 stays — a 45 all the way would run through SW2's ground
  pad at (23.25, 7.25). The antenna keep-out was never the constraint; its
  nearest corner is 3 mm from either line.
* **R15 is under the Pico.** It sat at the little-finger wall, at (36.5,
  46.63), with +3V3 crossing the pin row between pins 6 and 7 to reach it.
  It now lies in the band between the pocket and the jumper pads, y 47.2,
  with its +3V3 pad directly above JP4's +3V3 pad on x 15.85: the branch is
  one straight leg north out of JP4's pad, 3.2 mm, and stops in R15. BTN
  leaves pad 2, steps onto y 46.63 with one 45 and runs that row east to
  the pin row, so BTN is what crosses the row now, at 0.25 mm rather than
  the rail's 0.4 mm neck. Nothing about the reasoning for R15's end of the
  board changed: the rail still crosses the channel once, at the elbow.
* **J1, J2 and J5 against U1 — checked, clear.** The XH body's inboard face
  is at x 30.93 (from the footprint's own F.Fab outline); the Pico's edge is
  at 30.51 and its socket strips end at 30.17, so there is 0.4 mm of daylight
  on the board even before the Pico's 8.5 mm of height over a 7 mm header.
  Their *courtyards* do overlap U1's by 1.04 mm, because U1's courtyard is
  drawn 1 mm outside its body — that is the overlap the "Pico is socketed"
  rule in `handoff.kicad_dru` waives, and the only thing DRC would otherwise
  say.

**Result:** DRC **0 errors**, 13 warnings (all `lib_footprint_mismatch`),
parity 0, unrouted 0, short check 0 pairs, no `NET`, `OUTSIDE` or `IN BOSS`
lines; 19 vias, 92 holes in 7 sizes, fab pack 17 files.

## Still open

`layout-prompt.md`, the brief the previous session's open items referred to, is
**no longer in the tree** — it was untracked and is gone, so its §1/§2/§4
numbering cannot be checked against anything. What that brief asked for, as far
as the previous README recorded it, and where it stands:

* **Part-by-part placement justification (§1).** Still not written as a
  document. Placement is justified inline, in the comments beside each entry in
  `gen_pcb.py`'s `PLACE` table and in the sections above, which is where it is
  most likely to be read — but nobody has walked the whole board and asked "why
  here" of every part in one sitting.
* **`floorplan.svg` has drifted out of date.** It is the pre-layout plan and
  several sessions of layout have moved past it: it draws the expansion pads
  (removed), `JP1-JP7` and `TP12`/`TP13` (neither exists), and the old 4.5 mm
  mounting-hole positions. It is left as the record of the plan rather than
  half-corrected into something that is neither the plan nor the board — but
  either regenerate it from `PLACE` or retire it.
* **Full routing review (§2).** Partly done, and by machine rather than by eye:
  every track end is now a named pad rather than a coordinate, the pour is
  checked for islands, shorts are checked independently of DRC, and every pad is
  checked against the mounting bosses. Not done: a via-by-via audit (there are
  19), a count of thermal-relief spokes actually formed on each pad, and a
  confirmation that the analogue guard ring is continuous.
* **Professional finish (§4).** Now largely done — board-edge legend, fab note,
  pin-1 marks, tightened DRC severities, the drill table, the solder-mask web
  check and the Gerber/drill pack are all in. What is left is a human looking at
  the plots.
* **Not re-examined:** the AFE column pitch is still 5.3 mm, which was set by a
  1206's length — every part in those columns is 0603 now, so the columns could
  be tighter and the channel less crowded; but every lane and crossing around
  them was placed against this pitch, so closing it up is a re-route, not a
  constant. C3's loop length, the elbow block's pad size and the mounting-hole
  insets are also unchanged.

Nothing in that list blocks fabrication: DRC, schematic parity, the netlist
oracle, the BOM-against-README check, the short check, the pour-island check and
the boss check all pass.
