# Hardware bring-up

Assemble the Handoff PCB in stages and prove each stage with a multimeter
before the next one starts. No oscilloscope is needed anywhere in this
procedure. Every powered check is one command from `scripts/bringup.py`
and one meter reading with a stated pass window.

Drawings: [top face](hardware-bringup/top.png) · [bottom face](hardware-bringup/bottom.png)
(the bottom is drawn mirrored, the way you see it when you flip the board over).

**A handshake needs two bands.** Steps 0–11 and P are one board, on your own,
with a meter and a USB cable, and they are most of the work. Step 12 flashes the
product image, labels the band and pairs it to a phone — still one board. Only
its last two checks need the other band, two phones and a second person, so
**take board one to the end of step 12's pairing, build board two the same way,
and meet them at the handshake.**

---

## Before you start

**Prove the PC before you solder anything**

Step 4 builds firmware. Set the toolchain up first, on a bare Pico, while the
board is still an unsoldered rectangle — a build that fails then costs nothing.

- Install the **Raspberry Pi Pico** VS Code extension, open this folder, let it
  download SDK 2.3.1. The root [README](../README.md) has the versions and the
  Linux route.
- `pip install pyserial`.
- Hold BOOTSEL on a bare Pico, plug it in, and run `python scripts/bringup.py
  flash blink`. Its green LED should blink once a second. **That is the whole PC
  setup proved.** If it fails, fix it now, not with a half-built board attached.

**Tools**

- Fine-tip iron, 0.5 mm solder, a flux pen, solder wick, tweezers
- Isopropyl alcohol (IPA) and a brush
- A magnifier or phone camera for the MSOP-8
- Multimeter with continuity beep, Ω, DC V, diode mode
- A micro-USB cable to the Pico, one short jumper wire
- Python 3 with pyserial: `pip install pyserial`
- **A laptop that runs on its own battery.** Step 9 is the one step where a
  finger touches the electrode, and the mains lead has to be out for it

**Four rules**

- **Every solder jumper JP1–JP8 stays open until its step says to bridge it.** The board ships open on purpose so each block can be proved alone.
- **When the board is powered, never touch a probe to U2 pin 3, R2's inner pad or R3's inner pad.** That node is 1 MΩ and the probe changes what it does. Unpowered Ω checks on it are fine.
- **Clean the board with IPA after every soldering pass, and let it dry.** Flux across the 1 MΩ node puts the amplifier on a rail.
- **Nobody touches an electrode while the board is on mains-powered USB.** Design §13 rule 1: the hazard is not the signal, it is a mains-referenced ground finding a path through a person. The 1 MΩ in series (R2, proved in step 1) bounds the current to 3.3 µA; the floating supply is the second layer, and the second layer is not optional. Step 9 is the only step that asks you to touch the electrode, and it says how.

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

**About JP8.** It is a bench override, read only by `apps/bringup` and the
earlier one-way test apps, which needed to be told which board talks first. The
product image ignores GP14 entirely and elects roles over the link. **Both bands
run the same firmware and both leave JP8 open** — step 5 bridges it with a wire
for two seconds to prove the pin, and nothing else ever does.

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

- Two 1×20 **female** headers for the Pico, cut from a 1×40 strip
- J1 and J5, the two 2-pin XH sockets on the right wall
- SW1 slide switch, SW2 push button

**The Pico needs pins of its own.** A plain Pico 2 W ships bare. Solder a 1×40
**male** strip to it, or buy the pre-soldered *Pico 2 WH*. Either way the board
side is female, so the Pico lifts out — which is what makes the meter checks in
steps 0–3 possible with no chip on the board.

**Three footprints stay empty, on purpose**

| | Why |
|---|---|
| **J2** (PAD) | the electrode wire solders straight into the `PAD` hole. Leave the other hole empty |
| **J3** (LED) | the 5 mm RGB LED's legs *are* `R`, `K`, `G`, `B` in that order, so its legs solder straight into the four holes with nothing crossed |
| **J6** (motor) | the motor's two leads solder straight in, either way round |

Fit the XH socket instead of soldering in if you would rather the LED or the
plate be unpluggable — you then need a pre-crimped XH pigtail for each, and a
2.50 mm one, not the 2.00 mm PH that looks the same. Soldered in is what fits
the box, and it needs no crimp tool.

**Off-board, wired later**

- D2 RGB LED into J3 (step 5), M1 motor into J6 (step 5), the cell into J1 (step 11), the electrode wire into J2 (step 9)
- **Check the cell's plug is a 2.50 mm XH** before it goes near J1. Many 1S cells
  ship on a 2.00 mm PH lead; re-pin it into an XH housing, or change the lead.
  Step 11 checks polarity, which is the other thing cells get wrong

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
- J1 and J5 on the right wall: the housing's open side faces the wall.
- SW1: the metal body sits on the board, the handle sticks out past the edge. Solder the three pins and both ears.
- SW2.
- **J2, J3 and J6 stay empty** — the electrode wire, the LED and the motor solder into them later, at steps 9, 5 and 5.
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
- [ ] Solder the LED into J3. Its four legs already sit in the hole order: **the longest leg is `K`**, the lone leg on that side of it is `R`, the two beyond are `G` and `B`. Nothing crosses. Leave the legs long enough to bend — the box decides where the LED ends up, and step 12 is easier with it loose.

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
- [ ] JP4's AFE pad (it is U2 pin 8, and far easier to probe): 3.25–3.35 V.
- [ ] JP6's divider pad — the pad that does **not** beep to TP4. TP4 itself is still floating, JP6 being open: 1.57–1.73 V.
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

**This is the step a finger touches.** Before it: the laptop runs on its own
battery, **mains lead out**, and nothing else mains-powered is plugged into it.
Step 1 already proved R2 + R3 = 2.0 MΩ, so the series resistance the design
depends on is measured, not assumed. Both layers, every time. Anyone with a
pacemaker or an ICD does not do this step, or any later one.

- [ ] Unplug USB. **Bridge JP7.** Solder an electrode wire — any 5 cm wire — into J2's **square** pad, the one that beeps to TP5. Leave J2's other hole empty. Plug USB back in.

- [ ] Meter on **AC volts**, red probe on TP3 `OUT1`, nothing touching the wire: under 0.05 V.
- [ ] Hold the bare end of the wire between two fingers: TP3 reads **over 0.3 V AC** (usually about 1 V). Your body's 50 Hz mains hum, amplified ×11 and clipped by stage 1 — and 50 Hz is the one frequency a multimeter's AC range measures well.

A bare wire on a fingertip is not a worn electrode, and this is the only place it
happens. Everything that goes under a strap is taped copper — step P.

```
python scripts/bringup.py adc 100000
```

- [ ] Fingers off: `p-p` under ~300 mV. Fingers on: `p-p` **over 1000 mV** — stage 2 passes the clipped edges through to the ADC.

**If the fingers-off number will not come down, suspect the room before the
board** (found on both boards, 23 Sep 2026). A bare electrode wire is an
antenna, and the receiver's own band — roughly 5 kHz to 320 kHz, set by C1/R6
below and R9/C2 above — is exactly where a mini PC, a monitor and phone
chargers are loud. On a mains-powered bench the capture can sit rail to rail
(`p-p` 3000+, clipping at 3300) with nobody touching anything, repeatably.

- **50 Hz is not the cause.** C1 with R6 is a high-pass at about 5 kHz, so mains
  hum cannot reach the ADC. That is also why the meter reads ~0 V AC at TP3
  while the ADC is clipping: a DMM's AC range is deaf above a few hundred Hz.
- **The discriminator:** tie the electrode wire to ground and capture again. If
  it falls to a few tens of mV, the board, its supply and its ground are clean
  and the wire is the antenna. With the wire desoldered the floor should read
  around 200 mV `p-p`, in window.
- The meter half of this step still passes on a noisy bench — TP3 read 1.5 V AC
  with fingers on the wire against a 0.3 V floor. Judge the board on that, and
  take the ADC numbers on a **battery host** (laptop on battery, or a phone on
  OTG) if they are ever wanted.

That is the receiver alive end to end: pad → R2 → both stages → R9 → ADC.

---

## Step 10 — the transmitter into the receiver: bridge JP2

The leakage check first: GP11 must not shift the receive node when it is released. Do this in order.

```
python scripts/bringup.py pad off
```

- [ ] Unplug. **Bridge JP2.** Plug in. Run `pad off` again.
- [ ] TP3 `OUT1`: still within 0.10 V of TP4. **If it has moved to a rail**, GP11 leaks (RP2350-E9): wick JP2 open again and fit **C6** (a 330 pF, the C2 part) in the crossed-out spot under JP2 instead. Then continue.

**RP2350-E9 is real and both assembled boards needed C6** (23 Sep 2026). With
JP2 bridged and GP11 released, OUT1 went to the top rail (3.33 V) and the ADC
mean jumped 1871 → 2221 mV. After wicking JP2 open and fitting C6, OUT1 came
back to 2.21 V. On a board built from scratch, **fit C6 from the start and
leave JP2 open** — that is the right build, and `pad high` / `pad low` moving
the ADC mean by only ~3 mV while `carrier 200` rails it is the proof that C6
and not a JP2 bridge is carrying the signal.

**The next two checks are for a bridged JP2 only. If C6 is fitted, SKIP them
and go straight to the carrier.** C6 is a DC block, so a statically driven pin
cannot move OUT1 through it — that is the whole point of the part.

```
python scripts/bringup.py pad high
```

- [ ] *(bridged JP2 only)* TP3: above 3.0 V (stage 1 hits the top rail).

```
python scripts/bringup.py pad low
```

- [ ] *(bridged JP2 only)* TP3: below 0.3 V.

```
python scripts/bringup.py pad off
```

- [ ] TP3: back within 0.10 V of TP4.

```
python scripts/bringup.py carrier 40
python scripts/bringup.py adc
python scripts/bringup.py carrier 200
python scripts/bringup.py adc
python scripts/bringup.py carrier off
python scripts/bringup.py adc
```

| Carrier | `p-p` must be | board one | board two |
|---|---|---|---|
| 40 kHz | over 2000 mV | 3265 mV | 3266 mV |
| **200 kHz** | over 2000 mV | 2623 mV | 2291 mV |
| off | under ~400 mV | 410–431 mV | 410 mV |

- [ ] With the carrier on the board hears its own shout and clips it, as designed.

**Do not skip 200 kHz.** It is the marginal carrier — it sits nearest R9/C2's
321 kHz corner, so it is the one that falls off first if C2 is the wrong part
or a stage is sick. 40 kHz rails on almost anything.

**Carrier-off runs a little over the old 300 mV limit once C6 is fitted**, and
that is expected: C6 couples GP11's track into the receive node on top of a
loud room (see the step 9 note). 410 mV against a 2000 mV signal is still
roughly eight times' margin, and the carrier detector is frequency-selective on
top of that.

The whole signal path is now proved, both directions.

---

## Step 11 — the battery path: SW1, D1, JP5

USB out for all of this except the last line.

**The pigtail, before it touches the board**

- [ ] **Charge the cell first**, on the TP4056, until its LED turns green. A cell straight from the seller sits at its storage voltage, around 3.7 V, and the readings below assume a real one.
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

**The green boot flash is the bring-up image only.** It exists so a battery
boot is visible with no USB and no console. If the board is already carrying
the product image (step 12), the sign is different: **white for 200 ms and one
motor tap**. Either one means the board booted on the cell; neither appearing
is the fault.
- [ ] No USB means no console, so the button check is the meter: pin 20 goes 3.3 → 0 V when SW2 is pressed.
- [ ] Plug USB in with the cell still on. TP6 rises to ~4.7 V, and **D1's un-banded end** (the end facing SW1, same node as J1 `+`) stays at the cell: D1 is blocking USB from the cell.

**Probe D1's anode here, not TP7.** Once JP5 is bridged it ties D1's *cathode*
straight to VSYS, so TP7 `D1K` and TP6 `VSYS` are one node and both rise to
~4.7 V. The only point that stays at the cell is the anode side. The path is
SW1 → D1 anode (pad 2) → D1 cathode (pad 1) → JP5 → VSYS → Pico pin 39.

```
python scripts/bringup.py vsys
```

- [ ] Prints ~4700 on USB. Unplug USB, SW1 ON: the board keeps running (LED flash on the next power-cycle).

**Charger** — J5 is wired straight to the cell, before SW1. It is **not** a power input. The TP4056's OUT+ goes to J5 `+`, OUT− to J5 `−`. Because it sits before the switch, **the cell charges with SW1 off** — which is how you want it, and also means J5 is live whenever the cell is in.

---

## Step P — the plate, and closing the box

The board is proved. What is left is mechanical, and it is the part that decides
whether the band works on a wrist rather than on a bench.

**The plate**

- [ ] Cut a **25 × 25 mm** square of single-sided copper-clad board (design §8.1).
- [ ] Solder the free end of the step-9 wire to its copper face. Keep the wire short — under 5 cm — and let it leave the square from the edge nearest J2.
- [ ] **Cover the copper completely** with one layer of clear packing tape, wrapped round the edges. Thinner tape couples better. No bare copper anywhere that can reach skin: this is design §13 rule 3, and it is the whole reason the band is safe to wear.
- [ ] Tape the plate to the **outside of the bottom half**, taped face out, so it lies against the wrist.
- [ ] The board and the cell sit between the plate and the top of the box. The board's own ground plane is the second electrode, and it has to face the room, not the arm (design §8.2 — do not let the two end up back to back).

**The box** — `hardware/enclosure/bottom.3mf` and `top.3mf`, about 45 × 65 × 25 mm.

- [ ] Print both halves. PLA or PETG, 0.2 mm layers. Slice each half as it is oriented in the file, look at the preview, and add supports if your slicer asks for them.
- [ ] Press the four **M3 brass heat-set inserts**, 5 mm across, into the bottom half's posts with the soldering iron. They are on the board's own hole pattern, 33 × 55 mm between centres.
- [ ] Screw the board down through its four 3.4 mm holes into the inserts. The screw passes 1.6 mm of board and then the insert: measure your inserts and buy **M3 to suit**, typically 6–8 mm.
- [ ] Check every opening lines up before the screws go in: the Pico's micro-USB, SW1's handle, SW2, and wherever you have chosen to bring the LED out. **This enclosure is an initial design** (`hardware/enclosure/README.md` says so) — expect to open a hole with a knife or a drill, and expect to route the plate wire and the charger lead through a gap you make yourself.
- [ ] The TP4056 is not in the box. Leave J5's lead long enough to reach it, or unscrew the top to charge.
- [ ] Fit the 22 mm strap. The label goes on the outside of the **top** half — not the bottom, which the plate covers — but it is made in 12b, once the band is running the image that tells you what to print.

---

## Step 12 — the product image

Five parts, in order. **12a to 12c are one band on its own**; 12d needs the
second band finished too, and 12e needs two phones and a second person.

**12a — flash it**

```
python scripts/bringup.py flash handoff
python scripts/bringup.py watch
```

There is no transmitter image and no receiver image: both bands get this one,
and they settle between themselves which of them speaks first.

- [ ] The banner appears; at boot the RGB LED flashes white for 200 ms and the motor taps once.
- [ ] Short press SW2 → a 300 ms battery flash (green / amber / red by level, white if it has no reading yet).

**12b — the label.** The app finds a band by four hex digits, and the banner is
where they come from. They are printed on a sticker or typed; the app takes
either.

- [ ] The banner line reads `name "Handoff band 7A3C", label 7A3C`. Write those four digits down — from the outside the two bands are identical, and this is the only thing that tells them apart.
- [ ] For the printed QR: `pip install "qrcode[pil]"`, then `python tools/band_label.py 7A3C`. It writes `band-7A3C.png` into the current directory. Print it about **20 mm square**; below that the code stops scanning on a phone at arm's length.
- [ ] Stick it on the outside of the top half, where the plate is not.

**12c — the phone.** Build and install the app from `android/` (`android/README.md`),
one phone per band.

- [ ] Pair from the app: scan the label or type the digits, and confirm the one device Android offers.
- [ ] Fill in the contact card and save it. The band blinks green twice.
- [ ] A band belongs to one phone. Holding SW2 for five seconds clears the phone and the card and returns it to a slow blue pulse.

`python scripts/bringup.py raw "w"` stores a bench card with no phone at all,
which is enough to prove the link before either app exists.

**12d — the link, on the bench.** Now the second band has to be finished too.

- [ ] Both bands on USB, a console each. Hold them plate to plate, the two taped faces together → `done` lines on both consoles. A solo builder gets this far alone.
- This proves both bands are alive and that a whole exchange completes. It does **not** prove coupling: two bands on one host already share a ground through it. Step 12e is the one that proves coupling.

**12e — the handshake.**

- [ ] Both bands **on their own cells, USB out of both** — design §13 rule 1, and also the only condition the link is built for. Plates against skin, one band per wrist, one person each.
- [ ] Shake hands, a normal firm grip, about a second. Each band flickers, then turns green and buzzes; each name lands in the other phone.

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
| `bringup.py flash` fails before it reaches the board | the Pico SDK, not the board. Prove it on a bare Pico first — *Before you start* |
| `bringup.py` says "more than one Pico on USB" | the second band is plugged in too; pass `--port COM7`, or unplug it |
| The app never finds the band | the four digits, not the band: check them against the banner. Type them instead of scanning. Only one phone can own a band — hold SW2 for five seconds to clear the old one |
| Both bands pair and buzz, but a handshake does nothing | both on their own cells with **USB out of both** — that is design §13 rule 1 and it is also the only condition the link was built for — both plates against skin, and hold the grip a full second |
| One band does everything, the other nothing | they run the same image and elect roles; suspect the quiet one's receive path. Re-run step 9 on it |
| The LED or the button cannot be reached once the box is shut | step P: the enclosure is an initial design, so check every opening against your own parts before the screws go in |
