# Prompt for the next session — schematic review before layout

Copy everything below the line into the new chat.

---

You are a senior PCB designer reviewing an untested board before it goes to
fabrication. Read `hardware/README.md`, `hardware/handoff.kicad_sch`, and the
design doc `docs/body-coupled-handshake-design.md` §5–§7, §12–§13, §15 first.
Then do the work below in order. Regenerate and re-verify with
`python hardware/tools/gen_schematic.py` after every change: it must end with
`ERC: 0 violation(s), 0 error(s)` and `0 missing, 0 unexpected` nets — update
`EXPECTED_NETS` in the generator as part of every change, never delete the
check. Render the sheet (`hardware/build/handoff.pdf`) and look at it.
**PCBWay is sponsoring the board; nothing on it has ever been tested, so the
board must be able to isolate and probe every stage on the bench.**

## 1. Verify the schematic against the design, line by line

Walk design §7 (the netlist) against the sheet and confirm every entry, then
check what §7 does not say:

- U2 pinout against the MCP6292 datasheet (1 OUT A, 2 −IN A, 3 +IN A, 4 VSS,
  5 +IN B, 6 −IN B, 7 OUT B, 8 VDD); the project symbol is derived from KiCad's
  MCP6002-xMS, so confirm the units map to the right pins.
- Every Pico pin used against the Pico 2 W pinout: GP2 = pin 4, GP26/ADC0 =
  pin 31, GP16/17/18 = pins 21/22/24, GP27 = 32, GP0/1 = 1/2, GP4/5 = 6/7,
  GP20/21 = 26/27, RUN = 30, 3V3 = 36, VSYS = 39, all GND pins including AGND
  (33), and that VBUS (40), 3V3_EN (37), ADC_VREF (35) are left open on purpose.
- The footprint `Module:RaspberryPi_Pico_Common_THT` has 42 pads and the symbol
  40 pins. Create a scratch board, run *Update PCB from Schematic*, and confirm
  no pad/pin mismatch; note what the two extra pads are.
- Footprint pad counts vs symbol pins for every part, especially the project
  footprint `handoff:SW_Slide_SS-12F23G5` (pads 1/2/3 + two unnumbered ears,
  centre = common = symbol pin 2).
- Battery: J1 pin 1 = BAT−, pin 2 = BAT+; D1 anode at SW1, cathode at VSYS.
- Signal chain: PAD → R2 → +IN A; R3 to VREF; gain 1 + R4/R5 = 11; C1 → +IN B;
  R6 to VREF; gain 1 + R7/R8 = 11; OUT B → R9 → JP1 → ADC0; C2 to GND.
- Power flags, no dangling labels, every label appears at least twice.

## 2. "Will it work from every direction" — check the circuit, not just the wiring

Answer each with numbers and say whether it is a problem:

- DC operating point: with R3 to VREF and R5/R8 returned to VREF, both outputs
  sit at VREF = 1.65 V; ADC0 idles mid-scale. Confirm no stage is biased to a
  rail through a wrong return.
- AC: MCP6292 GBW 10 MHz at gain 11 → ~900 kHz per stage, ~580 kHz for two,
  fine at 200 kHz. R6/C1 high-pass = 16 Hz. R9/C2 = 321 kHz. Max output swing
  ×121 from a 1.3 mV input ≈ 160 mV — well inside the ADC. Also compute the
  input level at which stage 2 clips (design §15.2 risk) and what a strong
  interferer does.
- The shout: GP2 drives 3.3 V through R1 (1 MΩ) into the pad node while R2/R3
  form a 10/11 divider to the preamp — the receiver saturates during its own
  shout. That is expected (it is deaf then, 1 ms settle); confirm the recovery
  time of two saturated stages through C1/R6 is inside 1 ms.
- GP2 high-Z while receiving: RP2350-E9 pad leakage through 1 MΩ. Quantify
  the effect on the pad node (development plan open item). JP2 exists to route
  TX elsewhere if it matters.
- Power: VSYS = VBAT − ~0.35 V (D1). At 3.0 V cell cut-off that is 2.65 V,
  above the 1.8 V VSYS minimum. USB plugged in with SW1 off: the Pico runs from
  VBUS through its own diode; D1 blocks back-feed to the cell. USB plugged in
  with SW1 on: VBUS (~4.7 V after the Pico's diode) wins over the cell — is
  that acceptable? Reversed J1: D1 blocks, nothing powers.
- Pico VSYS/3 battery monitor on GP29 reads VSYS, not VBAT; note the 0.35 V
  offset for firmware.
- LED currents at 3.3 V / 330 Ω: red ~4 mA, green/blue ~1 mA; confirm it is
  bright enough to read through the enclosure or change R12–R14.
- 3V3 rail noise from the Pico's SMPS into U2 with only C3 (100 nF): decide
  whether a 10 µF electrolytic (the C4 part, the owner has spares) belongs at
  U2 VDD. If yes, add it as C5 with the same footprint as C4.
- The 10 MΩ node (R2/R3/U2 pin 3): confirm nothing else touches it (no test
  pad, no jumper) — a probe there measures the probe.

## 3. Interfaces the board is missing — add these

**Push button (owner decision, add it):** *Tactile Push Button Switch 6x6x5,
Robu SKU 618182*, footprint `Button_Switch_THT:SW_PUSH_6mm` (4 legs, 6 × 6 mm,
two pairs of legs are internally joined — wire the diagonal pair or both pairs
so orientation cannot matter). One side to GND, the other to a GPIO with a
10 kΩ pull-up to 3V3 (same 10 k 1206 as R5/R8, Robu R137556; ref R15). Pick
the GPIO from the ones not on J4 and not used — GP15 is the natural choice
(pin 20, bottom of the left column, next to nothing). Name the net `BTN`. No
debounce cap; debounce in firmware. Its jobs, so the next milestones have a
bench control: force TX / force RX / provisioning mode / clear bond. Record
that intent in the README.

**Mounting holes — missing entirely.** Add four `Mechanical:MountingHole` (M3) so the board can be fixed in the box. This is
the most common omission on a first board.

**Charger port.** The cell is charged off-board on a TP4056, which today means
unplugging it. Add a second 2-pin JST-XH (J5, "CHG") in parallel with J1 on
the cell side of SW1, same pin order, so the charger plugs in beside the cell.
Zero cost, saves a connector cycle every charge. Mark clearly that J5 is
**not** an input for anything but the charger.

**Cell voltage test pad.** TP10 on BAT+ (before D1) so the cell voltage can be
read without unplugging anything; TP8 (VSYS) is after the diode.

Then go looking for what *else* is missing, thinking through each phase of
the project: bench bring-up (M3–M8), the wire-link tests between two boards
(M6, which needs grounds commoned — is a ground test pad enough, or does it
want a 2-pin header?), the body-coupling campaign (M10–M11, plates and a
switchable earth reference per design §15.1), the phone integration, and the
enclosure (USB port reachable? BOOTSEL reachable? switch handle through the
wall? LED visible? SWD debug connector on the Pico reachable?). Also consider:
a second electrode connector for the two-electrode experiments; a strap
(jumper to GND) to select board role for M6; whether anything on the analogue
side should have a DNP footprint for the LC bandpass mentioned in §15.2.
Propose each with a one-line justification and add the ones that cost pads
only. Do not add anything that needs a part the owner has not bought without
saying so explicitly.

## 4. Jumpers — the board is untested, so make every stage isolatable

The owner has **no 0 Ω resistors** and has decided **every jumper is normally
open** — you bridge with a solder blob to connect and wick it off to isolate;
nothing is ever cut. Two-pad jumpers use symbol `Jumper:SolderJumper_2_Open`
with footprint `Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm`; the
3-way select uses `Jumper:SolderJumper_3_Open` with
`Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm` (bridge centre to one
side). They cost ~3 × 2 mm of board and nothing in the BOM. Consequence to
keep in the README and on the silkscreen: **the board is inert as delivered
until the in-line jumpers are bridged**; the bring-up order in the README
must say which to bridge first. Write the "as shipped" state of every jumper
as OPEN and the netlist oracle accordingly (an open jumper is two separate
nets).

Already present, both open: **JP1** (R9 → ADC0; leave open to drive the ADC
from TP4 directly) and **JP2** (GP2 → R1; leave open to keep the transmitter
off the pad). Add, at minimum:

- **JP3, 3-way, stage select:** centre → R9; pad 1 → OUT1 (stage 1); pad 2 →
  OUT2 (stage 2). Bridge to OUT2 for ×121; if that clips in a noisy hall,
  move the blob to OUT1 for ×11 without a rework. Design §15.2 names this
  exact risk.
- **JP4, U2 supply:** in series with 3V3 to U2 pin 8. Cut to measure
  the op-amp's current and to power the AFE from a bench supply at TP6.
- **JP5, in the VSYS feed** after D1, with test pads on both sides,
  so the whole board's current can be measured through an ammeter across it.
- **JP6, VREF to the divider** (between the R10/R11/C4 node and the
  VREF net), so an external bias can be injected at TP5 if the divider is
  suspected.
- **JP7, PAD → R2** (receiver input), so the receiver can be tested
  from a signal injected at the R2 side while the pad and TX are off.
- Consider a jumper from ADC0 to GND for a noise-floor measurement, or state
  that TP4-to-TP7 with a wire does the same and skip it.

For every jumper state what is being isolated, which test pad drives the
isolated side, and which milestone uses it. Put that table in the README.
Keep them off the 10 MΩ node.

## 5. Deliverables

1. The updated schematic, generator, `EXPECTED_NETS`, README parts table and
   deviations section — regenerated, ERC clean, netlist check passing, BOM
   exported (`kicad-cli sch export bom`) and diffed against the README table.
2. A short review report (`hardware/review.md`): every check in §1–§2 with its
   number and verdict, every addition with its reason, and anything you
   recommend against and why. Be blunt about uncertainties; say "not verified"
   rather than guessing.
3. A list of what must be confirmed on a physical part before layout (today:
   the SS-12F23 body-to-pin-row offset of 3.3 mm, read off a small drawing).

Layout is the session after this one. Do not start it.
