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
| `tools/gen_schematic.py` | generated the schematic from the §7 netlist and checks it with `kicad-cli` (ERC + netlist diff). One-shot bootstrap; once the sheet is edited by hand in KiCad, the generator is history, not source |

Validate after any edit:

```
kicad-cli sch erc --severity-all --exit-code-violations hardware/handoff.kicad_sch
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
| R5, R8 | 10 kΩ 1 % (Yageo RC1206FR-0710KL) | 1206 | R137556 | 2 |
| R9 | 1.5 kΩ 1 % (Yageo RC1206FR-071K5L) | 1206 | R137563 | 1 |
| R12, R13, R14 | 330 Ω | 1206 | 575088 | 3 |
| C1, C3 | 100 nF X7R 50 V (TCC1206X7R104J500DT) | 1206 | R153721 | 2 |
| C2 | 330 pF C0G/NP0 50 V (KEMET C1206C331J5GACTU) | 1206 | R111869 | 1 |
| C4 | 10 µF 63 V electrolytic | radial through-hole, 5 mm dia, 2.54 mm lead pitch (KiCad `CP_Radial_D5.0mm_P2.50mm`) | 1090083 | 1 |
| D1 | 1N5819 Schottky 40 V 1 A | DO-41, horizontal | R241509 | 1 |
| SW1 | SS-12F23G5 slide switch, SPDT (1P2T), right-angle, 5 mm handle | 3 terminals at 3.0 mm pitch + 2 mounting ears, `handoff:SW_Slide_SS-12F23G5` | R132611 | 1 |
| D2 | RGB LED, common cathode, 5 mm, clear (5-pack) | off-board: solders into J3 or plugs in via a 4-pin XH pigtail | R183455 | 1 |
| J1 | JST-XH 2.54 straight 2-pin male (battery) | through-hole | — | 1 |
| J2 | JST-XH 2.54 straight 2-pin male (electrodes) | through-hole | — | 1 |
| J3 | JST-XH 2.54 straight 4-pin male (LED), or the LED soldered straight in | through-hole | — | 1 |
| BT1 | KP384455 Li-ion 3.7 V 1500 mAh | off-board, on a JST-XH pigtail | — | 1 |

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
which is deliberate — bring it up one stage at a time.

| | between | bridge it when |
|---|---|---|
| JP1 | R9 and the GP26/ADC0 node | normal operation; leave open to drive the ADC from TP4 directly, as M5 does with its own attenuator |
| JP2 | GP2 and R1 | normal operation; leave open if RP2350-E9 leakage through R1 turns out to matter (development plan, open item) and route TX from a J4 pin instead |

**Test pads** (1.5 mm SMD): PAD, stage-1 out, AFE out, ADC0, VREF, 3V3, GND,
VSYS, GP2. Deliberately **no** pad on the 10 MΩ node (R2/R3/U2 pin 3): a probe
there measures the probe.

**J4 expansion, 1×10 female:** 3V3, RUN, GP0, GP1 (UART0), GP4, GP5
(I²C0), GP20, GP21, GP27 (ADC1), GND. The spare ADC input is there on purpose — a
second analogue path is the most likely "hack" this board will ever need. RUN to
GND is a reset.

**SW1 footprint (`handoff.pretty/SW_Slide_SS-12F23G5`)**, from the vendor
drawing: terminals 0.8 × 0.45 mm at 3.0 mm pitch (centre = common = symbol pin
2), mounting ears 12.9 mm overall (holes at ±6.05, 1.15 mm drill for the ±0.2
tolerance), body 8.7 × 5.5 mm standing 3.3 mm behind the pin row with the 5 mm
handle vertical, 3.5 mm travel. Confirm the body-to-pin-row offset against a
real part before the board goes out; it is the one dimension read off a small
drawing rather than stated.

**SMPS mode pin.** Design §10.4 says tie GP23 high. Wrong for a 2 W — that is a
firmware call on WL_GPIO1 (see README). Nothing on the board.

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
