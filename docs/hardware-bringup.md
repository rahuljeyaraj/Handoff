# Hardware bring-up

Assemble the Handoff PCB in stages and prove each stage with a multimeter
before the next one starts. No oscilloscope is needed anywhere in this
procedure. Every powered check is one command from `scripts/bringup.py`
and one meter reading with a stated pass window.

Drawings: [top face](hardware-bringup/top.png) · [bottom face](hardware-bringup/bottom.png)
(the bottom is drawn mirrored, the way you see it when you flip the board over).

---

## Before you start

**Tools**

- Fine-tip iron, 0.5 mm solder, a flux pen, solder wick, tweezers
- Isopropyl alcohol (IPA) and a brush
- A magnifier or phone camera for the MSOP-8
- Multimeter with continuity beep, Ω, DC V, diode mode
- A micro-USB cable to the Pico, one short jumper wire
- Python 3 with pyserial: `pip install pyserial`

**Three rules**

- **Every solder jumper JP1–JP8 stays open until its step says to bridge it.** The board ships open on purpose so each block can be proved alone.
- **When the board is powered, never touch a probe to U2 pin 3, R2's inner pad or R3's inner pad.** That node is 1 MΩ and the probe changes what it does. Unpowered Ω checks on it are fine.
- **Clean the board with IPA after every soldering pass, and let it dry.** Flux across the 1 MΩ node puts the amplifier on a rail.

**Holding the board**

- Hold it like the top drawing: push button **SW2 at the top**, the Pico's **USB connector at the bottom**, hanging off the edge.
- The three JST connectors J2 / J1 / J5 are on the **right** wall. The LED connector J3, diode D1 and slide switch SW1 are on the **left** wall.
- Pico pin numbers in this position: **pin 1 is the bottom-right hole**, counting up the right side to **pin 20 at the top right**, then **pin 21 at the top left**, counting down to **pin 40 at the bottom left**.

**Pico pins this board uses**

| Pin | Name | On this board |
|---|---|---|
| 15 | GP11 | transmitter → JP2 → R1 → pad |
| 19 | GP14 | ROLE strap, JP8 |
| 20 | GP15 | push button SW2, pull-up R15 |
| 22 / 24 / 25 | GP17 / 18 / 19 | LED red / green / blue |
| 31 | GP26 / ADC0 | receiver output, from JP1 |
| 34 | GP28 | motor gate |
| 36 | 3V3 | Pico's regulator → JP4 → amplifier |
| 39 | VSYS | board supply, from JP5 |
| 40 | VBUS | USB 5 V |
| 3, 8, 13, 18, 23, 28, 33, 38 | GND | ground |

**Meter ground.** For every voltage reading the black probe goes on **TP1** (top face, labelled `GND`) or any Pico GND pin.

**Telling jumper pads apart.** Every jumper is two bare copper pads (JP3 has three). Which pad is which is found with the continuity beep, never by eye:

| Jumper | Joins | The pad that beeps to… | …is the | Bridged at |
|---|---|---|---|---|
| JP5 | D1 → VSYS | Pico pin 39 / TP6 `VSYS` | VSYS side; other pad is D1 (TP7 `D1K`) | step 11 |
| JP4 | 3V3 → amplifier supply | Pico pin 36 | 3V3 side; other pad is AFE supply (U2 pin 8) | step 7 |
| JP6 | bias divider → VREF | TP4 `VREF` | VREF side; other pad is the divider | step 7 |
| JP3 | amplifier → R9 | TP3 `OUT1` | OUT1 side; **the other outer pad is OUT2**; centre goes to R9 | step 8, centre ↔ OUT2 |
| JP1 | R9 → ADC | Pico pin 31 / TP2 `ADC0` | ADC side; other pad is R9 | step 8 |
| JP7 | pad → receiver | TP5 `PAD` | pad side; other pad is RX_IN (R2) | step 9 |
| JP2 | GP11 → transmitter | Pico pin 15 | GP11 side; other pad is R1 | step 10 |
| JP8 | ROLE → GND | any GND pin | GND side; other pad is Pico pin 19 | never (role strap) |

**Firmware note.** The breadboard benches drove the pad from GP2; the PCB uses GP11. `scripts/bringup.py flash` builds for GP11 into `build-pcb/`. Anything flashed with plain `scripts/build.py` still targets GP2 and reaches nothing on this board — use `--tx-pin 11`.

---

## Parts by face

**Top face, surface mount** (soldered first)

| Ref | Value | Notes |
|---|---|---|
| U2 | MCP6292 | MSOP-8. Pin 1 corner is marked on the silk; see step 1 for the meter check |
| R2, R3 | 1 MΩ | the receive node — keep them clean |
| R4, R6, R7 | 100 kΩ | |
| R5, R8 | 10 kΩ | |
| R9 | 1.5 kΩ | |
| C1, C2 | 330 pF C0G | |
| C3 | 100 nF | |
| D1 | PMEG3020ER-TP | SOD-123FL. **Band toward J3 / the top of the board** |
| R16 | 100 Ω | |
| R17 | 100 kΩ | |

**Bottom face, surface mount** (soldered second)

| Ref | Value | Notes |
|---|---|---|
| R1 | 1 MΩ | |
| R10, R11 | 100 kΩ | the VREF divider |
| R12, R13, R14 | 100 Ω | LED series |
| R15 | 10 kΩ | button pull-up |
| C4, C5 | 10 µF | not polarised |
| Q1 | AO3400A | SOT-23; pin-1 dot on the silk |
| D3 | PMEG3020ER-TP | **band toward the motor pads J6** |
| C6 | — | **do not fit** (crossed out on the drawing) |

**Through-hole, top face** (soldered last)

- Two 1×20 female headers for the Pico (cut from the 1×40)
- J1, J5 (2-pin XH), J2 (2-pin XH), J3 (4-pin XH)
- SW1 slide switch, SW2 push button

**Off-board, wired later**

- D2 RGB LED into J3 (step 5), M1 motor into J6 (step 5), the cell into J1 (step 11), the electrode wire into J2 (step 9)

---

## Step 0 — bare board

Nothing soldered. Continuity mode.

- [ ] Every jumper JP1–JP8: **no beep** between its pads.
- [ ] Pico holes 3, 8, 13, 18, 23, 28, 33, 38, TP1, J1 square pad, J2 round pad, J3 `K` hole: **all beep to each other** (one ground).
- [ ] Pico hole 36 ↔ GND, hole 39 ↔ GND, hole 40 ↔ GND: **no beep**.
- [ ] U2's eight pads: no beep between any two neighbours.

If anything beeps that should not → the board is shorted from the fab; stop.

---

## Step 1 — top face SMD

Board flat on the bench, top face up. Do U2 first while the board is fully flat.

**Soldering order**

- U2: find pin 1. Of U2's two rows of four pads, one row has a pad that **beeps to GND** — that pad is pin 4, and **pin 1 is the other end of the same row**. Match the chip's dot to it. Tack one corner, check alignment, tack the opposite corner, flux, then the rest.
- R2, R3 — the two 1 MΩ next to U2. Minimum flux, and clean afterward.
- R4, R5, R6, R7, R8, R9, C1, C2, C3.
- D1: band toward J3. Check against the silk bar.
- R16, R17 (near the bottom, beside the Pico's left row).
- IPA clean, dry.

**Meter checks** (unpowered; Ω readings through capacitors climb for a few seconds, wait for them)

| Between | Reads | Proves |
|---|---|---|
| U2 pin 1 ↔ pin 2 | 100 kΩ | R4, no bridge |
| U2 pin 2 ↔ pin 3 | ~1.0 MΩ | R5 + R3 |
| U2 pin 3 ↔ pin 4 | open (OL) | no bridge on the receive node |
| U2 pin 5 ↔ pin 6 | 110 kΩ | R6 + R8 |
| U2 pin 6 ↔ pin 7 | 100 kΩ | R7 |
| U2 pin 7 ↔ pin 8 | open | no bridge |
| U2 pin 4 ↔ pin 8 | open (climbs) | supply not shorted |
| TP3 ↔ TP4 | 110 kΩ | R4 + R5, stage 1 feedback |
| JP7's RX_IN pad ↔ TP4 | 2.0 MΩ | R2 + R3 |
| JP3's OUT2 pad ↔ TP4 | 110 kΩ | R7 + R8, stage 2 feedback |
| JP3 centre ↔ JP1's R9 pad | 1.5 kΩ | R9 |
| Pico hole 34 ↔ GND | 100 kΩ | R16 + R17 |
| Diode mode, red on D1's **un-banded** end, black on TP7 | 0.15–0.40 V | D1 forward |
| same, probes swapped | open | D1 the right way round |

If a neighbour-pin pair reads near 0 Ω → a bridge; wick it and re-test.

---

## Step 2 — bottom face SMD

Flip the board. It rests on the top-face parts (about 1 mm tall) — put it on a silicone mat or a folded paper towel so it does not rock.

**Soldering order**

- R10, R11, C4 (top-right corner of the bottom drawing), C5 (top left)
- R12, R13, R14 (right side, beside J3's holes), R15 (bottom centre)
- R1 (left side, beside JP2)
- Q1: dot on the silk = pin 1. D3: band toward J6.
- Leave C6 empty.
- IPA clean, dry.

**Meter checks** (unpowered)

| Between | Reads | Proves |
|---|---|---|
| JP4's AFE pad ↔ GND | about 200 kΩ (climbs) | R10 + R11, C3/C4/C5 not shorted |
| JP6's divider pad ↔ GND | 100 kΩ | R11 |
| J3 `R` hole ↔ Pico hole 22 | 100 Ω | R12 |
| J3 `G` hole ↔ Pico hole 24 | 100 Ω | R13 |
| J3 `B` hole ↔ Pico hole 25 | 100 Ω | R14 |
| Pico hole 36 ↔ hole 20 | 10 kΩ | R15 |
| TP5 ↔ JP2's R1 pad | 1.0 MΩ | R1 |
| Diode mode, red on J6 **square** pad, black on J6 round pad | 0.15–0.40 V | D3 forward |
| same, swapped | open | D3 orientation |
| Diode mode, red on GND, black on J6 square pad | 0.4–0.8 V | Q1 body diode (drain ↔ source) |
| same, swapped | open | Q1 not shorted |
| Pico hole 39 ↔ GND | open | VSYS not shorted |

---

## Step 3 — through-hole parts

Everything now goes in from the top and is soldered on the bottom.

**Soldering order**

- **Pico headers:** push the two female strips onto the Pico's own pins, drop the whole assembly into the board, solder one pin at each end, check it sits flat, solder the rest. Pull the Pico out afterward.
- J2, J1, J5 on the right wall: the housing's open side faces the wall.
- J3 on the left wall.
- SW1: the metal body sits on the board, the handle sticks out past the edge. Solder the three pins and both ears.
- SW2.
- IPA clean, dry.

**Meter checks** (continuity, Pico still out)

- [ ] Pico hole 39 ↔ JP5's VSYS pad ↔ TP6 ↔ J6 round pad: beep.
- [ ] Hole 36 ↔ one JP4 pad. Hole 31 ↔ TP2 ↔ one JP1 pad. Hole 15 ↔ one JP2 pad. Hole 19 ↔ one JP8 pad.
- [ ] Hole 20 ↔ GND: **no beep** released, **beep** while SW2 is pressed.
- [ ] J1 `+` (round) ↔ J5 `+` ↔ SW1 centre pin: beep. J1 `−` (square) ↔ GND: beep.
- [ ] J2 square pad ↔ TP5: beep. J2 round pad ↔ GND: beep.
- [ ] J3 `K` ↔ GND: beep.
- [ ] SW1: J1 `+` ↔ D1's un-banded end beeps in **one** handle position only. **Mark that position ON** with a pen.

---

## Step 4 — first power: the Pico alone

All jumpers open. No cell. No LED, no motor.

- [ ] Check once more: hole 36 ↔ GND and hole 39 ↔ GND do not beep.
- [ ] Insert the Pico: **USB connector over the bottom edge**, antenna end toward SW2.
- [ ] Plug in USB. First time on a new Pico: hold its BOOTSEL button while plugging in.

```
python scripts/bringup.py flash blink
```

- [ ] The Pico's own green LED blinks once a second.

DC volts, black probe on TP1:

| Red probe on | Reads |
|---|---|
| Pico pin 40 | 4.8–5.2 V |
| Pico pin 39 and JP5's VSYS pad | 4.5–4.9 V |
| JP5's D1 pad | under 0.5 V |
| Pico pin 36 and JP4's 3V3 pad | 3.25–3.35 V |
| JP4's AFE pad and U2 pin 8 | under 0.5 V (nothing feeds it yet) |

If pin 36 is low or the Pico gets warm → unplug; something on 3V3 is shorted (R15 area, JP4).

---

## Step 5 — bring-up firmware: button, ROLE, LED, motor, VSYS

```
python scripts/bringup.py flash
```

Then in a second terminal leave this running for the whole step — it prints a line whenever the button or ROLE changes:

```
python scripts/bringup.py watch
```

**Button and ROLE**

- [ ] `watch` shows `btn=1 role=1`.
- [ ] Press SW2 → `btn 0  (pressed)`. Release → `btn 1`.
- [ ] Meter on Pico pin 20: 3.3 V released, 0 V pressed.
- [ ] Hold a wire across JP8's two pads → `role 0  (JP8 bridged)`. Remove → `role 1`. Leave JP8 open.

**VSYS as the firmware sees it**

```
python scripts/bringup.py vsys
```

- [ ] Prints within 150 mV of the meter on Pico pin 39.

**LED** — first without the LED plugged in:

```
python scripts/bringup.py led red
```

- [ ] Meter on J3's `R` hole: 3.3 V. `led off` → 0 V.
- [ ] Plug the LED into J3: **longest leg into `K`**, the single leg on one side into `R`.

```
python scripts/bringup.py led red
python scripts/bringup.py led green
python scripts/bringup.py led blue
python scripts/bringup.py led white
python scripts/bringup.py led off
```

- [ ] Each colour is the colour it says.

**Motor** — first without the motor:

```
python scripts/bringup.py motor on
```

- [ ] Pico pin 34: 3.3 V. J6 **square** pad: under 0.1 V (Q1 pulls it to ground). J6 round pad: same as VSYS.

```
python scripts/bringup.py motor off
```

- [ ] Pico pin 34: 0 V.
- [ ] Solder the motor's two leads into J6 (either way round).

```
python scripts/bringup.py motor pulse
```

- [ ] It buzzes for half a second.
- On USB, VSYS is ~4.7 V and the motor is a 3 V part: **pulses only**, no `motor on` for long. On the cell it runs at 3.3–3.8 V.

---

## Step 6 — transmitter pin, JP2 still open

```
python scripts/bringup.py pad high
```

- [ ] JP2's GP11 pad: 3.3 V. Pico pin 15: 3.3 V.

```
python scripts/bringup.py pad low
```

- [ ] JP2's GP11 pad: 0 V.

```
python scripts/bringup.py carrier 40
python scripts/bringup.py freq
```

- [ ] Prints `pad 40000 Hz` (39 960–40 040).
- [ ] Meter on JP2's GP11 pad reads roughly half the rail, 1.2–2.0 V — a DC meter averages the square wave.

```
python scripts/bringup.py carrier 200
python scripts/bringup.py freq
```

- [ ] Prints `pad 200000 Hz` (199 800–200 200).

```
python scripts/bringup.py carrier off
```

---

## Step 7 — amplifier supply and bias: bridge JP4, then JP6

Unplug USB before each bridge.

- [ ] **Bridge JP4.** Plug USB in.
- [ ] U2 pin 8: 3.25–3.35 V. JP6's divider pad: 1.57–1.73 V.
- [ ] Unplug. **Bridge JP6.** Plug in.

| Red probe on | Reads |
|---|---|
| TP4 `VREF` | 1.57–1.73 V |
| TP3 `OUT1` | within 0.10 V of TP4 |
| JP3's OUT2 pad | within 0.10 V of TP4 |

(TP3 is U2 pin 1 and the OUT2 pad is U2 pin 7, so both stages are read without a probe near pin 3.)

If TP3 sits at 0 V or 3.3 V → stage 1 is on a rail: U2 pin 1 orientation, R4/R5 values, or flux on the receive node — clean with IPA, dry fully, re-read. To split divider from amplifier: unplug, wick JP6 open, and check the divider pad alone reads 1.65 V.

---

## Step 8 — into the ADC: bridge JP3 and JP1

- [ ] Unplug. **Bridge JP3 centre ↔ its OUT2 pad** (the outer pad that does *not* beep to TP3). Leave the OUT1 pad alone.
- [ ] **Bridge JP1.** Plug in.
- [ ] TP2 `ADC0`: within 0.10 V of TP4.

```
python scripts/bringup.py adc
```

- [ ] `mean` 1550–1750 mV, `p-p` under 200 mV.

---

## Step 9 — the pad hears the world: bridge JP7

- [ ] Unplug. **Bridge JP7.** Plug in.
- [ ] Plug an electrode wire (any 5 cm wire, or the pad pigtail) into J2's `PAD` pin. Leave J2's other pin empty.

- [ ] Meter on **AC volts**, red probe on TP3 `OUT1`, nothing touching the wire: under 0.05 V.
- [ ] Hold the bare end of the wire between two fingers: TP3 reads **over 0.3 V AC** (usually about 1 V). Your body's 50 Hz mains hum, amplified ×11 and clipped by stage 1 — and 50 Hz is the one frequency a multimeter's AC range measures well.

```
python scripts/bringup.py adc 100000
```

- [ ] Fingers off: `p-p` under ~300 mV. Fingers on: `p-p` **over 1000 mV** — stage 2 passes the clipped edges through to the ADC.

That is the receiver alive end to end: pad → R2 → both stages → R9 → ADC.

---

## Step 10 — the transmitter into the receiver: bridge JP2

The leakage check first: GP11 must not shift the receive node when it is released. Do this in order.

```
python scripts/bringup.py pad off
```

- [ ] Unplug. **Bridge JP2.** Plug in. Run `pad off` again.
- [ ] TP3 `OUT1`: still within 0.10 V of TP4. **If it has moved to a rail**, GP11 leaks (RP2350-E9): wick JP2 open again and fit **C6** (a 330 pF, the C2 part) in the crossed-out spot under JP2 instead. Then continue.

```
python scripts/bringup.py pad high
```

- [ ] TP3: above 3.0 V (stage 1 hits the top rail).

```
python scripts/bringup.py pad low
```

- [ ] TP3: below 0.3 V.

```
python scripts/bringup.py pad off
```

- [ ] TP3: back within 0.10 V of TP4.

```
python scripts/bringup.py carrier 40
python scripts/bringup.py adc
python scripts/bringup.py carrier off
python scripts/bringup.py adc
```

- [ ] With the carrier on: `p-p` **over 2000 mV** — the board hears its own shout and clips it, as designed. Off: back under 300 mV.

The whole signal path is now proved, both directions.

---

## Step 11 — the battery path: SW1, D1, JP5

USB out for all of this except the last line.

**The pigtail, before it touches the board**

- [ ] J1's silk: square pad `−`, round pad `+`. Check the cell's plug matches; re-pin it if not. (D1 makes a reversed plug harmless, not useful.)
- [ ] Plug the cell into J1. SW1 **ON**. DC volts, black on J1's `−`:

| Red probe on | Reads |
|---|---|
| J1 `+` | the cell, 3.3–4.2 V |
| TP7 `D1K` | within 0.3 V of J1 `+` |
| TP6 `VSYS` | under 0.5 V (JP5 still open) |

- [ ] SW1 **OFF** → TP7 drops toward 0 V.

**Board current (optional, no scope needed)**

- [ ] SW1 ON. Meter on the **mA** range, red probe on TP7, black on TP6 — the meter is now the only path into the board.
- [ ] The RGB LED flashes green for a second (the bring-up image booted), then the meter reads about 20–40 mA.

**Bridge JP5**

- [ ] SW1 OFF. Bridge JP5. SW1 ON.
- [ ] Green boot flash. TP6: the cell minus 0.2–0.4 V. Pico pin 36: 3.3 V.
- [ ] No USB means no console, so the button check is the meter: pin 20 goes 3.3 → 0 V when SW2 is pressed.
- [ ] Plug USB in with the cell still on. TP6 rises to ~4.7 V, TP7 stays at the cell: D1 is blocking USB from the cell.

```
python scripts/bringup.py vsys
```

- [ ] Prints ~4700 on USB. Unplug USB, SW1 ON: the board keeps running (LED flash on the next power-cycle).

**Charger** — J5 is wired straight to the cell, before SW1. It is **not** a power input. The TP4056's OUT+ goes to J5 `+`, OUT− to J5 `−`.

---

## Step 12 — the product image

```
python scripts/bringup.py flash handoff
python scripts/bringup.py watch
```

- [ ] The banner appears; at boot the RGB LED flashes white for 200 ms and the motor taps once.
- [ ] Short press SW2 → a 300 ms battery flash (green / amber / red by level, white if it has no reading yet).
- [ ] Pair from the Android app; provision a card. `python scripts/bringup.py raw "w"` stores a bench card without a phone.
- [ ] With a second assembled board: electrodes touching → `done` lines on both consoles.

---

## What this procedure does not measure

- **Gain, corner frequency, noise floor, clipping point, input capacitance.** Those are milestone M7, with `apps/afe_sweep` on this same board: wire TP4 to Pico pin 32 (GP27) for its `v` command, open JP2 and JP7, and feed its source divider into JP7's RX_IN pad through a 100 nF. Build it with `python scripts/build.py --target afe_sweep --tx-pin 11 --build-dir build-pcb --flash --flash-target afe_sweep`.
- **Bit and frame error rates** — M8 onward, `apps/linktest` and `apps/handoff`, same `--tx-pin 11`.
- **Carrier edge jitter** — needs a scope; design §14.

## If something is wrong

| Symptom | Look at |
|---|---|
| Pico not on USB after a flash | BOOTSEL by hand: hold it, replug, flash again |
| `bringup.py` says "cannot open COM…" | another terminal's `watch` or a serial monitor is holding the port |
| TP3 on a rail with JP6 bridged | flux on the receive node; U2 pin 1; R4/R5 swapped |
| TP3 fine, TP2 not | JP3 bridged to the wrong outer pad; JP1 |
| `adc` p-p large with nothing touching | electrode wire too long or near the USB cable; fine once the box is closed |
| `freq` reads 0 | carrier not started, or the image was built for GP2 — flash through `bringup.py` |
| Board dead on the cell, fine on USB | SW1 position; JP5; pigtail polarity (TP7 must read the cell) |
| Motor runs at power-on | R17 missing or open (Pico hole 34 ↔ GND must read 100 kΩ) |
