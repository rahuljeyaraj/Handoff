# Body-Coupled Handshake — Design Document

Rev 1.0

---

## 1. Scope

Two people wear a wristband. They shake hands. Contact details transfer between the wristbands through their bodies.

The body is the transmission medium. The handshake closes the circuit; it does not generate the signal.

This document specifies the electrical design, the modulation and protocol, the firmware architecture, and the bill of materials for one wristband. Two identical wristbands are required for a working demonstration.

---

## 2. Requirements

| # | Requirement |
|---|---|
| R1 | ~~Transfer a 40-byte contact record~~ **Transfer a full vCard** between two wristbands during a normal handshake (approx. 1 second of contact) — see [firmware-architecture.md §8](firmware-architecture.md) |
| R2 | Battery powered, fully floating, no mains reference at either end |
| R3 | No bare metal contacts skin |
| R4 | Buildable on 2.54 mm single-sided perfboard; no custom PCB |
| R5 | Debuggable without an oscilloscope |
| R6 | Deliver the received record to the wearer's phone as a usable contact |

### Non-requirements

- Multi-party operation (human chain) is explicitly out of scope for this revision
- iOS support is out of scope — the phone client is Android-only (§10.6)
- Range beyond direct skin contact is out of scope

---

## 3. Theory of operation

### 3.1 Coupling mechanism

Capacitive (electric-field) coupling. No current is deliberately injected into tissue.

An insulated electrode couples to skin. A second electrode on the outer face of the wristband couples to the room. The body is driven to a signal potential relative to earth, and the return path closes through body-to-environment capacitance, typically 100–200 pF. Currents are in the microamp range.

This is not galvanic coupling and it is not RF. Above roughly 20–30 MHz the electrodes begin behaving as antennas and the mechanism changes to near-field RF; the design stays far below this.

### 3.2 Frequency selection

| Parameter | Value | Reason |
|---|---|---|
| Operating carrier | 200 kHz | Highest carrier that can be directly sampled by the Pico ADC |
| Bring-up carrier | 40 kHz | Weaker coupling but identical code path; only the PIO divider changes |

Coupling improves with frequency, since the body-to-environment path is capacitive. The upper bound here is set by the ADC, not by physics — 200 kHz sits below the 250 kHz Nyquist limit of a 500 ksps converter.

### 3.3 Prior art

Zimmerman, MIT Media Lab, 1995: 330 kHz, 2400 bps. NTT RedTacton later reached 10 Mbps. Realistic hobbyist throughput is 300 bps to 9600 bps.

---

## 4. System architecture

### 4.1 Block level

```
        LiPo ──► TP4056 ──► SW1 ──► Pico VSYS

  Pico GP2 ──► R1 1M ──┐
                       ├──► PAD (skin side, insulated)
  Pico ADC0 ◄── amp ◄──┘ R2 1M

  GROUND PLANE (outer face) ──► Pico GND

  Pico ──BLE──► phone
```

One skin electrode, shared between transmit and receive. One ground plane facing the room.

### 4.2 Why a single shared pad

The link is half duplex. The wristband never transmits and receives at the same instant, so there is no need to isolate a transmit electrode from a receive electrode.

A two-electrode, two-carrier full-duplex arrangement was considered and rejected. It requires the same role negotiation (both ends must still agree who takes which carrier), costs an extra electrode and an extra receive path, and introduces a self-desensitisation problem. It buys nothing.

### 4.3 Why direct sampling instead of a mixer

The conventional approach is a switching mixer with I and Q channels down-converting to baseband. That exists to recover amplitude independently of carrier phase, since the two wristbands run on separate crystals.

A Goertzel filter is inherently complex-valued and returns magnitude directly, so it solves the same problem in software. This removes the mixer, the local oscillator, the baseband filter and the baseband amplifier — roughly half the analogue circuit.

Costs of this choice:

- Carrier is capped at 250 kHz by Nyquist
- No pre-ADC filtering, so strong nearby interference can consume ADC headroom
- The ADC is fully committed to one channel at maximum rate

Benefits:

- Approximately 17 dB of coherent processing gain that the mixer design does not have
- No mixer DC offset drift, which would otherwise corrupt the amplitude measurement campaign
- Signal stays at 200 kHz rather than being folded to DC into the op-amp flicker noise region

---

## 5. Link budget

All figures at 200 kHz.

| Stage | Value | Note |
|---|---|---|
| TX drive | 3.3 V p-p | Direct from PIO pin, no driver IC |
| Body-to-environment path | ~150 pF, ≈5.3 kΩ | Dominant series impedance |
| R1 divider loss | ≈ −45 dB | 1 MΩ against 5.3 kΩ |
| Signal on body | ≈ 17 mV | |
| RX pad to preamp | ≈ −20 dB | 1 MΩ against ~10 pF preamp input |
| Preamp input | ≈ 1.3 mV | |
| Analogue gain | ×121 | Two stages of 11 |
| ADC input | ≈ 160 mV | ~200 LSB at 12 bits |
| Goertzel processing gain | +17 dB | N = 50 coherent integration |

The dominant loss in the entire system is the pair of 1 MΩ safety resistors, costing roughly 65 dB combined. This is accepted. It is not negotiable, and it is the reason analogue gain and processing gain are both needed.

### 5.1 Rejected: series resonance

A series inductor resonating out the body capacitance would give 20–30 dB of free voltage gain for one cheap component. It does not work here: the 1 MΩ safety resistor destroys the Q. Recorded so it is not rediscovered.

---

## 6. Circuit design

### 6.1 Power

LiPo cell feeds the TP4056 module. Module output passes through SW1 to Pico VSYS (pin 39).

The Pico 2 W contains an RT6154 buck-boost accepting 1.8–5.5 V on VSYS, so no external regulator is required. The Pico's own 3V3 output (pin 36) powers the op-amp.

### 6.2 Bias reference (VREF)

Single-supply operation means the signal must swing around a mid-rail reference rather than around zero.

- R10 100 kΩ from 3V3 to VREF
- R11 100 kΩ from VREF to GND
- C4 10 µF from VREF to GND

VREF = 1.65 V.

### 6.3 Transmit path

```
Pico GP2 ──► R1 (1 MΩ) ──► PAD
```

Two components. No gate driver, no boost converter.

The load is capacitive and draws around 12 µA, well within a GPIO's capability. A CD4049 at 12 V would gain approximately 11 dB but adds a chip, a boost converter, and a switching noise source adjacent to a high-impedance node. It is deliberately kept off the critical path and can be added later if measurement shows the link is short.

**Firmware requirement:** GP2 must be switched to input (high-Z) while receiving, so the transmitter does not load the pad.

### 6.4 Receive path

```
PAD ─► R2 (1M) ─► U2A ─► C1 ─► U2B ─► R9 ─► ADC0
```

**Stage 1 — U2A, non-inverting, gain 11**

| Component | Value | Function |
|---|---|---|
| R2 | 1 MΩ | Series safety limit |
| R3 | 10 MΩ | Bias +IN to VREF |
| R4 | 100 kΩ | Feedback |
| R5 | 10 kΩ | Gain set, returns to VREF |

Gain = 1 + R4/R5 = 11.

No input coupling capacitor is required. The pad is capacitively coupled to skin through its insulator, so there is no DC path to block.

**Interstage**

| Component | Value | Function |
|---|---|---|
| C1 | 100 nF | Blocks stage 1 DC offset |
| R6 | 100 kΩ | Re-biases stage 2 +IN to VREF |

**Stage 2 — U2B, non-inverting, gain 11**

| Component | Value | Function |
|---|---|---|
| R7 | 100 kΩ | Feedback |
| R8 | 10 kΩ | Gain set, returns to VREF |

Total analogue gain 121.

**Bandwidth check:** MCP6292 GBW is 10 MHz. At a closed-loop gain of 11, each stage has approximately 900 kHz of bandwidth. Two cascaded stages give a −3 dB point around 580 kHz. Ample at 200 kHz, and the cascade's second-order rolloff above 580 kHz forms a useful part of the anti-alias response.

**Output filter**

| Component | Value | Function |
|---|---|---|
| R9 | 1.5 kΩ | ADC input protection and filter |
| C2 | 330 pF | Anti-alias |

Corner frequency 321 kHz. Attenuation at 200 kHz is 1.6 dB; at 1 MHz approximately 10 dB, supplemented by the amplifier rolloff.

**Decoupling**

C3, 100 nF, directly across U2 pins 8 and 4.

---

## 7. Netlist

MCP6292 pinout: 1 OUT A, 2 −IN A, 3 +IN A, 4 VSS, 5 +IN B, 6 −IN B, 7 OUT B, 8 VDD.

Pico 2 W pins referenced by physical pin number.

**Power**

| From | To |
|---|---|
| BT1 + | M1 B+ |
| BT1 − | M1 B− |
| M1 OUT+ | SW1 |
| SW1 | U1 pin 39 (VSYS) |
| M1 OUT− | U1 pin 38 (GND) |
| U1 pin 36 (3V3) | U2 pin 8 |
| U1 pin 38 (GND) | U2 pin 4 |
| C3 | U2 pin 8 → U2 pin 4 |

**VREF**

| From | To |
|---|---|
| R10 | 3V3 → VREF |
| R11 | VREF → GND |
| C4 (+ to VREF) | VREF → GND |

**Transmit**

| From | To |
|---|---|
| U1 pin 4 (GP2) | R1 |
| R1 | PAD node |

**Receive stage 1**

| From | To |
|---|---|
| PAD node | R2 |
| R2 | U2 pin 3 |
| R3 | U2 pin 3 → VREF |
| R4 | U2 pin 1 → U2 pin 2 |
| R5 | U2 pin 2 → VREF |

**Interstage**

| From | To |
|---|---|
| U2 pin 1 | C1 |
| C1 | U2 pin 5 |
| R6 | U2 pin 5 → VREF |

**Receive stage 2**

| From | To |
|---|---|
| R7 | U2 pin 7 → U2 pin 6 |
| R8 | U2 pin 6 → VREF |

**Output**

| From | To |
|---|---|
| U2 pin 7 | R9 |
| R9 | U1 pin 31 (GP26 / ADC0) |
| C2 | GP26 node → GND |

**Electrodes**

| From | To |
|---|---|
| PAD | PAD node (R1 / R2 junction) |
| GROUND PLANE | U1 pin 38 (GND), single point |

---

## 8. Electrode and mechanical design

### 8.1 Configuration

| Electrode | Face | Size | Insulation |
|---|---|---|---|
| PAD | Skin side | ~25 × 25 mm | Tape over the copper, mandatory |
| GROUND PLANE | Outer face | As large as the wristband allows | Not required |

Material: single-sided copper clad board, cut to size. Copper clad is preferred over copper foil tape — it solders reliably and does not peel.

### 8.2 Separation

The PAD and the GROUND PLANE must be separated by as much distance as the mechanical design allows, with the perfboard and battery between them.

Do **not** use the two faces of a single double-sided board as PAD and GROUND PLANE. At 1.6 mm separation their mutual capacitance is roughly 15 pF, which partially shorts the signal electrode to the return electrode instead of letting the return electrode couple to the room.

If double-sided stock is used, bond both faces of each piece into one net with a wire around the edge, so no floating conductor sits behind an active electrode.

### 8.3 Insulation

Any thin plastic film works. Thinner is better, since insulator thickness sets the pad-to-skin capacitance.

| Material | Suitability |
|---|---|
| Clear packing tape | Good, thin, cheap |
| Kapton | Marginally better, not worth the cost |
| PVC electrical tape | Acceptable, thicker, adhesive degrades |

### 8.4 Ground plane connection

Exactly one wire from the ground plane to circuit ground. Two connections form a loop that will pick up interference.

---

## 9. Modulation and protocol

### 9.1 Modulation

On-off keying. The 200 kHz carrier is gated on for a mark and off for a space.

OOK is chosen over FSK or PSK because it requires no phase or frequency tracking between two independent crystals. The receiver only measures energy in one bin.

### 9.2 Line coding

Manchester. A mark is transmitted as on-then-off; a space as off-then-on.

Two reasons:

1. **Threshold drift.** Received amplitude varies with grip, posture and footwear. A fixed decision threshold fails during long runs of the same symbol. Manchester lets the receiver compare the two halves of each bit against each other rather than against an absolute level.
2. **Bit timing.** Every bit contains a guaranteed transition, so the receiver's timing can never drift more than one bit before being corrected.

Cost: halves the data rate. There is sufficient margin.

### 9.3 Rates

| Parameter | Value |
|---|---|
| Chip rate | 2000 chips/s |
| Data rate | 1000 bps |
| Chip period | 500 µs |
| Goertzel windows per chip | 5 |

### 9.4 Packet format

> **Superseded by [firmware-architecture.md §8.3](firmware-architecture.md).** A full
> vCard does not fit one packet, so the payload is fragmented: a 2-byte header
> (fragment index, count, record id) is added and the checksum widened to CRC-16.
> The preamble and start marker below are unchanged.

| Field | Length | Content |
|---|---|---|
| Preamble | 32 chips | Alternating 1010… |
| Start marker | 8 bits | `11110000` |
| Payload | 40 bytes | Contact record |
| Checksum | 8 bits | CRC-8 |

Approximately 350 ms per packet at 2 kchips/s.

The preamble carries no data. It exists to give the receiver a dense run of transitions to lock timing onto. The start marker contains four identical consecutive bits, a pattern that cannot occur within the preamble, making the payload boundary unambiguous.

### 9.5 Transport strategy

> **Superseded by [firmware-architecture.md §8.4](firmware-architecture.md).** With a
> fragmented payload the repetition becomes a priority-weighted carousel —
> fragment 0 (name + mobile, 248 ms) is repeated most, later fragments enrich it.
> The property below that matters is kept: no acknowledgement, no retry
> negotiation.

The transmitting wristband sends the packet **continuously and repeatedly** for the duration of contact. The receiver accepts the first packet that passes CRC and discards everything else.

At roughly 350 ms per packet, a one-second handshake gives at least two complete attempts and typically three.

This removes all acknowledgement, retry and timing negotiation from the first working version.

### 9.6 Two-way operation

Half duplex, single carrier, listen-before-talk, following the collision avoidance model of ISO/IEC 18092.

On contact, each wristband waits a random 0–5 ms and then listens. If a carrier is heard it becomes the target and only responds. If silence is heard it becomes the initiator and drives the exchange. Ties are broken by redraw and retry, as in Ethernet backoff.

After roles are settled, the link switches to master/slave polling with stop-and-wait: initiator sends its record, target acknowledges, initiator requests the target's record, target sends, initiator acknowledges.

**Rejected: burned-in priority IDs.** Static arbitration in the style of CAN bus requires a wired-AND bus so a losing node can detect its loss mid-bit. Body coupling has no dominant/recessive state, so this degenerates into hoping, and the lowest-numbered device starves the rest.

### 9.7 Turnaround timing

With a shared electrode, the wristband's own transmitter drives the amplifier input hard. No damage results — the 1 MΩ limits fault current to approximately 3 µA — but the amplifier saturates.

Allow 1 ms of settling after transmit ends before trusting received data. Actual recovery is in the microsecond range; 1 ms is deliberately generous.

---

## 10. Firmware design

### 10.1 Carrier generation

PIO state machine toggling a GPIO.

| Carrier | System clock | Total divider |
|---|---|---|
| 200 kHz | 150 MHz | 750 |
| 40 kHz | 150 MHz | 3750 |

Both are exact integer divisions. Only the divider changes between bring-up and operating frequency.

### 10.2 Sampling

| Parameter | Value |
|---|---|
| ADC clock | 48 MHz |
| Cycles per conversion | 96 |
| Sample rate | 500.000 ksps exactly |
| Resolution | 12 bit |
| Channel | ADC0 (GP26) |

Set `adc_set_clkdiv(0)` for maximum rate. DMA into a ring buffer.

RP2350 is specified rather than RP2040 because of the known RP2040 ADC differential-non-linearity defect.

### 10.3 Demodulation

Goertzel filter, N = 50 samples.

| Parameter | Value |
|---|---|
| Window length | 50 samples = 100 µs |
| Bin spacing | 10 kHz |
| Target bin | 20 (200 kHz) |
| Window rate | 10 kHz |
| Processing gain | ~17 dB |

The carrier sits exactly on a bin centre, so the window contains an exact integer number of carrier cycles and there is no spectral leakage.

Output is a single magnitude value per window — the "score". Five consecutive scores form one Manchester chip.

Core 1 runs the Goertzel loop; core 0 handles protocol and BLE.

### 10.4 Noise mitigation

Tie GPIO23 high to force the Pico's SMPS into fixed-frequency PWM mode rather than power-save. A wandering switching frequency aliases unpredictably into the ADC band.

### 10.5 Instrumentation

The receiver is also the test instrument. Raw ADC buffers are streamed over USB serial and plotted on a host PC in Python.

This is a design decision, not a fallback: it removes the need for an oscilloscope, and every measurement in the validation campaign comes from the same firmware that runs the link. A USB sound card cannot be used for this — consumer dongles cap at 48 kHz sample rate, far below the carrier.

### 10.6 Phone integration

> **Settled: the phone client is a native Android app. No web client, no iOS.**
> See [firmware-architecture.md §11](firmware-architecture.md) for the app design.
> The firmware exposes the fixed §11.2 BLE GATT contract and is insulated from
> everything above it.

Pico 2 W → BLE, bonded and encrypted → a native Android app holding a background connection through a foreground service → `rx_vcard` notify → the received card is written to the app's own database, and optionally promoted into the system address book by an explicit user action.

The app pairs with one wristband once, through Android's `CompanionDeviceManager`, and reconnects automatically thereafter. The foreground service is what keeps the link alive while the phone is pocketed during a handshake — and is the reason a web client was rejected, since a backgrounded browser tab drops the GATT connection.

No display, LED or motor on the wristband. The phone provides all feedback, including the buzz on a completed handshake.

**Rationale for BLE as a secondary link:** BLE broadcasts to everything within ten metres and cannot identify who was intended in a room of fifty. A handshake is unambiguous — physical touch is the addressing. BLE is only the wire to the wearer's own pocket; the link between two people remains skin.

**Android only, by choice.** iOS is out of scope (§2). The app is a normal Android project at `android/`, outside the firmware tree; the Pico SDK toolchain does not build it.

---

## 11. Bill of materials

Prices are indicative and were current at the time of writing. Verify at time of order.

### 11.1 Modules and semiconductors

| Ref | Part | Qty per wristband | Price | Source |
|---|---|---|---|---|
| U1 | Raspberry Pi Pico 2 W | 1 | ₹729 | [robu.in](https://robu.in/product/raspberry-pi-pico-2-w/) |
| U2 | MCP6292-E/MS, MSOP-8 | 1 (buy 4 total) | ₹111 | [robu.in](https://robu.in/product/mcp6292-e-ms-microchip-operational-amplifier-dual-2-channels-10-mhz-7-v-%c2%b5s-2-4v-to-6v-msop-8-pins/) |
| — | SOP8/MSOP8 to DIP8 adapter | 1 (5-pack) | ~₹60 | [robu.in](https://robu.in/product/so-msop-tssop-soic-to-dip8-board-pcb-pack-of-5/) |
| M1 | TP4056 module with protection | 1 | ~₹49 | [robu.in](https://robu.in/product/tp4056-1a-li-ion-lithium-battery-charging-module-with-current-protection-mini-usb/) |
| BT1 | LiPo 500 mAh 1S, protected | 1 | ₹269 | [robu.in](https://robu.in/product/500mah-pcm-protected-micro-li-po-battery/) |

Buy four op-amps, not two. MSOP-8 is 0.65 mm pitch; expect to lose one or two while learning to drag-solder it.

### 11.2 Board and hardware

| Item | Qty | Price | Source |
|---|---|---|---|
| Perfboard 6 × 8 cm, single-sided, 2.54 mm | 1 per wristband | ~₹40 | [robu.in](https://robu.in/product/6-x-8-cm-universal-pcb-prototype-board-single-sided-2-54mm-hole-pitch/) |
| Copper clad board, single-sided | 1 sheet total | ~₹100 | [Zero PCB / copper clad category](https://robu.in/product-category/electronic-components/breadboard-and-zero-pcb/) |
| 2.54 mm female header, 40 pin | 1 strip per wristband | ~₹30 | search Robu: `female header 2.54 40 pin` |
| DIP-8 IC socket | 1 per wristband | ~₹10 | search Robu: `IC socket 8 pin` |
| SW1 slide switch, 2.54 mm | 1 per wristband | ~₹15 | search Robu: `slide switch SPDT` |

### 11.3 Passives — through-hole option

Recommended for the first build. Legs allow the high-impedance node to be joined in a single solder blob rather than routed across the board.

| Ref | Value | Qty | Package |
|---|---|---|---|
| R1, R2 | 1 MΩ | 2 | 1/4 W axial |
| R3 | 10 MΩ | 1 | 1/4 W axial |
| R4, R6, R7, R10, R11 | 100 kΩ | 5 | 1/4 W axial |
| R5, R8 | 10 kΩ | 2 | 1/4 W axial |
| R9 | 1.5 kΩ | 1 | 1/4 W axial |
| C1, C3 | 100 nF | 2 | Ceramic disc 50 V (`104`) |
| C2 | 330 pF | 1 | Ceramic disc 50 V (`331`) |
| C4 | 10 µF | 1 | Electrolytic 16 V, polarised |

Sourcing:

| Item | Price | Source |
|---|---|---|
| 600 pc metal film resistor kit, 30 values | ~₹450 | [robu.in](https://robu.in/product/30-different-valued-metal-film-resistor-assorted-kit-for-diy-electronic-projects-and-experiments/) |
| 10 MΩ 1/4 W | ~₹5 each | **Not in the kit.** Order separately — assorted kits stop at 1 MΩ |
| Ceramic capacitor assortment | ~₹200 | search Robu: `ceramic capacitor kit` |

### 11.4 Passives — SMD option

Use 1206 only. A 1206 part is 3.2 mm long and spans two adjacent 2.54 mm pads with overlap on each end. 0805 at 2.0 mm barely reaches. Anything smaller cannot be used on this pitch.

| Ref | Value | Package | Note |
|---|---|---|---|
| R1, R4–R11 | as above | 1206 | |
| R2, R3 | 1 MΩ, 10 MΩ | **Keep through-hole** | See 12.2 |
| C1, C3 | 100 nF | 1206 X7R | |
| C2 | 330 pF | 1206 C0G/NP0 | C0G for stability |
| C4 | 10 µF | 1206 X7R 16 V | Ceramic replaces the electrolytic — no polarity |

SMD resistor marking:

| Value | Code |
|---|---|
| 1.5 kΩ | 152 |
| 10 kΩ | 103 |
| 100 kΩ | 104 |
| 1 MΩ | 105 |
| 10 MΩ | 106 |

**SMD ceramic capacitors carry no marking whatsoever.** A 330 pF and a 100 nF are visually identical. Keep them in labelled strips and cut off only what is needed.

### 11.5 Consumables

| Item | Price | Note |
|---|---|---|
| Rosin flux paste or pen | ~₹150 | Required for MSOP soldering |
| Desoldering braid, 2 mm | ~₹100 | Required for MSOP soldering |
| Clear packing tape | — | Electrode insulation |

### 11.6 Cost summary

Two complete wristbands, through-hole build, including shared kits and consumables: approximately **₹3,700**.

---

## 12. Assembly notes

### 12.1 Op-amp mounting

Solder the MSOP-8 to the DIP adapter once, fit header pins to the adapter, and plug the assembly into a DIP-8 socket. The socket adds roughly 1 pF, which is acceptable; desoldering a hand-soldered MSOP twice is not.

Technique: tack one corner pin, verify alignment under magnification, tack the diagonal corner, flood the side with flux and drag-solder all four pins together. Bridges are expected — remove them with braid. Verify every adjacent pin pair with a continuity test before applying power.

### 12.2 High-impedance node

The junction of R2, R3 and U2 pin 3 is the only node in the circuit with special layout requirements. It sits at 10 MΩ, so both stray capacitance and surface leakage matter — the latter especially in humid conditions.

**Construction:** place R2 and R3 in the two perfboard holes immediately adjacent to socket pin 3, standing upright so their bodies sit above the board. Join all three pads with a single solder blob on the underside.

Do not route this node through a track. Three adjacent pads spans about 5 mm; a wire across the board would be roughly twenty times worse.

Clean with IPA and allow to dry.

This is why R2 and R3 remain through-hole even in the SMD build: a 1206 part lies flat and requires two pads plus a bridge to reach pin 3, whereas an axial part drops one leg directly into the adjacent hole.

Impact if ignored: approximately 2–3 dB. Not fatal.

### 12.3 General layout

- Socket the Pico rather than soldering it, so it can be removed while probing the analogue side
- Run a ground guard trace around the preamp input node
- Place the preamp as far from the Pico as the board allows
- Keep the pad wire under 5 cm

---

## 13. Safety

Non-negotiable.

1. **Battery power only, both ends, fully floating.** The hazard is not the signal; it is a mains-referenced ground finding a path through a person. Never tether a wristband to a mains-powered laptop while anyone touches an electrode. Use isolated USB or optical isolation for instrumentation.
2. **1 MΩ minimum series resistance on every electrode.** Worst-case fault current is 3.3 µA, orders of magnitude below the ~1 mA perception threshold, and that threshold rises with frequency.
3. **Insulated electrodes only.** No bare metal on skin.
4. **Hand-to-hand and finger contact only.** No electrode placement that creates a path across the torso. Anyone with a pacemaker or ICD is excluded from the demonstration entirely.
5. **Explicit consent** from anyone appearing in video or photographs, stated in any writeup.

---

## 14. Validation plan

### 14.1 Proof of body coupling

Every hobbyist body-coupling demonstration attracts the accusation that the signal is RF leaking through air. This measurement disproves it and is the technical centrepiece of the project.

Plot received amplitude across:

| Condition |
|---|
| Hands clasped |
| 1 cm apart |
| 10 cm apart |
| Contact through a nitrile glove |
| Rubber mat vs tiled floor |
| Barefoot vs shoes |
| TX electrode off the body entirely, geometry unchanged |

If signal collapses when the body is removed but geometry is unchanged, the mechanism is body coupling and not an air path.

All plots come from the receiver firmware's raw ADC stream.

### 14.2 Bring-up sequence

| Step | Goal |
|---|---|
| 1 | One-way only at 40 kHz. A transmits, B receives, nothing else |
| 2 | Move to 200 kHz. Only the PIO divider changes |
| 3 | Add Manchester coding once threshold drift is observed on the plots |
| 4 | Add BLE and the phone page |
| 5 | Add role election for two-way operation |

Do not build both directions at once. If it fails there is no way to tell which half is wrong.

Manchester specifically should be added *after* observing drift, not before — the observation is worth having.

---

## 15. Known risks

### 15.1 Environmental return path

Rubber soles on a dry insulating floor can gut body-to-earth capacitance and drop the link. The system can work perfectly on the bench and fail on camera.

Humidity helps on the skin side. Footwear and flooring are the uncontrolled variables.

Mitigations:

- Characterise it deliberately, so barefoot-vs-shoes becomes another plot rather than a failure
- Keep the carrier at the upper end of the usable range
- Provide the receiver with a switchable earth-referenced ground as a wired comparison fallback

### 15.2 ADC headroom

Direct sampling has no pre-ADC filtering. Everything from DC to 250 kHz reaches the converter at full amplitude — mains hum, phone chargers, fluorescent ballasts. Goertzel rejects this content, but only after it has consumed ADC range. A strong enough interferer clips the converter and processing gain cannot recover from that.

This is the most likely failure mode in a hall full of other projects.

Mitigation: keep analogue gain modest, monitor for clipping, and add an LC bandpass ahead of the ADC if the environment demands it. One inductor and one capacitor.

---

## 16. Errors recorded

Design decisions that were tried and rejected, kept here so they are not repeated.

| Error | Why it fails |
|---|---|
| TL072, OPA2134 | Require ~6 V minimum supply. Will not run on 3.3 V. Use rail-to-rail CMOS throughout |
| MCP6002 as preamp | 1 MHz GBW. At a gain of 11 this gives 90 kHz of bandwidth and dies at 200 kHz |
| AO3400A as gate driver | Single N-channel, pulls down only, no active pull-up. The rising edge becomes a slow RC curve |
| TC4427 gate driver | 1.5 A rating against a 12 µA load. Over-specified by 100,000× |
| Series resonant inductor | Q destroyed by the 1 MΩ safety resistor |
| Static priority IDs for arbitration | Requires collision detection that body coupling cannot provide |
| Sound card as bring-up instrument | Consumer USB dongles cap at 48 kHz sample rate, far below any usable carrier |
| Carriers at 150 and 200 kHz | Relevant only if FDD is revisited: the 2nd harmonic of 150 kHz aliases to exactly 200 kHz at 500 ksps |

---

## 17. Open items

| Item | Status |
|---|---|
| Preamp input capacitance in situ | Not measured. Determines whether 200 kHz is achievable or the carrier must drop |
| 500 ksps ADC + DMA + Goertzel timing on core 1 | Not verified. If this does not hold, the analogue mixer must be reinstated |
| Contact record format | Replaced by full vCard; codec and fragmentation specified in [firmware-architecture.md §8](firmware-architecture.md) |
| Phone client | **Settled: native Android app**, [firmware-architecture.md §11](firmware-architecture.md) |
| Wristband provisioning (how it learns its own card) | Specified in [firmware-architecture.md §9](firmware-architecture.md) |
| Role election implementation | Logic tested at development plan M1; hardware binding at M14 |
| Enclosure and strap | Not designed |
