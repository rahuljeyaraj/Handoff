# Body-coupled link: does the new ground electrode fix the dropouts?

Supersedes the 24 Sep morning brief of the same name, most of whose hypotheses
were disproved that afternoon. Everything under **Measured** is data with the
run that produced it. Everything under **Open** is not.

## The project

Two RP2350 wristbands exchange a vCard through the wearer's body. Gated square
wave on GP11 (PCB build, `HANDOFF_TX_PIN=11`), Manchester coded, detected by a
Goertzel bin. Design doc: `docs/body-coupled-handshake-design.md`. Hardware:
`hardware/README.md`.

Signal chain, one board:

    GP11 -> R1 -> PAD -> R2 -> U2A (gain 11) -> C1 -> U2B (gain 11) -> R9 -> ADC0

- R3 biases the U2 pin 3 node to VREF
- C1 = **330 pF** C0G, R6 = 100 k: interstage corner **4.8 kHz**
- C2 = 330 pF with R9 = 1k5: anti-alias at 322 kHz
- R5, R8 = 10 k gain-set, total analogue gain 121
- U2 = MCP6292, 10 MHz GBW
- A space chip RELEASES the pad (high-Z); it is not driven low (design §9.8)

## What changed, and what to do about it

**A ground-plane electrode has been fitted to both bands** — design §8.1's
outer-face electrode, wired to J2 pin 2. Everything under *Measured, 24 Sep
2026 (afternoon)* was measured *without* it.

The link was dropping out. The question was whether the return electrode fixes
that. **It does** — see *Measured, 24 Sep 2026 (evening)*.

## Hardware state

- **R1 = R2 = 100 k on both boards.** The owner requires this value to work.
  Design §13.2 still says "1 MΩ minimum on every electrode, non-negotiable" and
  has not been updated.
- R3 = 1 M as built, although design §6 says 10 M.
- Board 93D1's R3 / U2-pin-3 junction read 0.98 M one way and 0.50 M the other
  on 23 Sep. Judged cosmetic. Reflowed several times since. Never re-measured.
- New: outer-face ground electrode on both bands, to J2 pin 2.
- Wrist pad is insulated — §8.1 makes tape over the copper mandatory, and §8.3
  notes the tape thickness *sets the pad-to-skin capacitance*, so it is in the
  signal path.

## Measured, 24 Sep 2026 (afternoon)

All with R1 = R2 = 100 k, 200 kHz, `linktest_tx` on 93D1 and `linktest` on
379E, no ground electrode fitted.

### 100 k is not broken

Pads held back to back: **130 good frames, 0 bad CRC, BER 0.00e+00, margin
454.** Signal (max chip energy) 722 against a noise floor of 84.

The chain works. Whatever is wrong is not the resistor value and not the board.

### Pad noise is 1/f, and 200 kHz is the best bin available

Listening with nothing transmitting anywhere, mean chip energy by bin:

| Listening at | Mean | Bursts to |
|---|---|---|
| 20 kHz | 225 | 2520 |
| 40 kHz | 123 | 1836 |
| 100 kHz | 62 | 849 |
| 200 kHz | 30 | 409 |

Noise halves each time frequency doubles. 200 kHz is both the quietest bin and
the highest the maths allows — the bin must be under 12.5 and `25e6 / hz` must
be an integer, which leaves only 20, 40, 100 and 200 kHz.

**Do not fall back to 40 kHz.** The previous brief floated it as an operating
point; it is 4x noisier there. Design §15.1's "keep the carrier at the upper end
of the usable range" is independently confirmed by this sweep.

### Worn: plenty of signal, but it collapses

Bands strapped to the wearer's hands, 40 s run:

- chip energy mean 97, **max 1561**; margin **539**; one good frame
- carrier level across the run: **230, then 27, then 4, then 2**
- 42 false syncs

When the path is there it decodes with the same confidence as the working
back-to-back run (margin 539 vs 454). Then it collapses to nothing and stays
there. **The problem is intermittency, not level and not noise.**

### GZ_N 50 is a trap on a real wrist

It genuinely cuts noise 1.7x (31 -> 18.5 — better than the sqrt(2) white-noise
theory, because the noise is 1/f). But it doubles chip time to 500 us and frame
airtime to 312 ms, and the skin channel does not hold still that long:

| Worn, same test | Result |
|---|---|
| GZ_N 50 | **0 frames**, 126 false syncs, margin 0 |
| GZ_N 25 | 1 good frame, margin 539 |

Reverted. It also needs `CARRIER_TX_WORDS` 4096 -> 8192 or `pio_carrier_send`
silently refuses (624 chips x 400 bits = 7801 words).

### SMPS PWM mode had never been enabled

Design §10.4 asks for the switcher to be pinned into fixed-frequency PWM before
the ADC goes live. The call sat commented out in `blink.c` alone since M4. On a
Pico 2 W it is `cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true)` — **not**
GP23, which is the CYW43 power enable on wireless boards.

Added to `linktest` (needs `pico_cyw43_arch_none` linked). Bare-ADC floor drops
1.1 -> 0.3 LSB RMS. Does nothing for pad noise. **Still missing from
`handoff.c` and `bringup.c`.**

## Measured, 24 Sep 2026 (evening) — with the ground electrode

Same boards, same 100 k, 200 kHz. Ground-plane electrode fitted to both bands,
**both bands floating on their cells, no USB attached during the run**.

### The dropouts are fixed

Worn, transmitter gated on and off at SW1 so every counted frame is from the
worn window and nothing else:

| Worn, 200 kHz | No electrode (afternoon) | With electrode (evening) |
|---|---|---|
| Good frames | 1 | **447** |
| Bad CRC | — | 147 |
| Lost | — | 41 |
| Worst gap | link died and stayed dead | **3 frames** |
| FER | ~1.00 | 0.296 |
| BER | — | 1.87e-02 |
| Carrier level | 230, 27, 4, 2 | **~48, steady** |
| Mean margin | 539 (that one frame) | 19 |

**`worst gap` is the dropout instrument** — the longest run of consecutive
missing frames. It is 3. The collapse to nothing is gone and the carrier level
never fell to 2.

The link is now *continuously mediocre* rather than *briefly excellent then
dead*. That is the trade the electrode buys, and it says the return it provides
is modest and steady rather than strong. 30 % of frames still fail.

The margin fall from 539 to 19 is not a regression: 539 was one lucky frame,
19 is the mean over 594 frames including the 147 that failed CRC.

### The app's rendezvous gate now clears

`carrier.c`'s `min_delta = 24` needs `level > floor + 24`. Floor measured 9 with
the transmitter off; level ran ~48 worn. Before the electrode, worn levels of
10-16 meant `handoff` could never have started whatever the decoder did. There
is now room to spare.

### False syncs were never a signal-quality number

With the transmitter switched off at SW1 and nothing transmitting anywhere, the
false-sync counter still climbed 669 -> 1058. **`z` does not reset
`s_rx.false_syncs`** (`rx_zero` memsets `s_st` only), so every false-sync figure
in this brief is cumulative since boot and must be read as a difference. The
afternoon's "42 false syncs" says nothing about the channel.

### A USB-tethered run cannot answer this question

Both bands were first tried with USB attached to a mains-powered desktop. The
ground electrode is wired straight to board GND with no series resistor, so USB
joins the two bands' grounds through the PC — and that wire *is* the return
path under test. Tethered, chip energy max read 1530 and carrier level 144, yet
**zero frames decoded** in 13 s (raw RMS 806 LSB about code 2222: the chain is
driven hard enough to clip). It also bonds the wearer's skin to mains earth on
both hands, which is what design §13.1 forbids and why it forbids it.

Ambient pad noise with the transmitter off measured mean chip energy 28.7,
max 154 — unchanged from the afternoon's 30, so the electrode did not add noise.

### Design §14.1: it is the body, not the air — PROVEN

The control the writeup needed. Both bands laid on a wooden table **40-50 cm
apart, the same spacing as the wearer's relaxed hands**, nobody within reach,
transmitter gated on at SW1 for 90 s, both bands floating on cells:

| Same spacing, same carrier, same boards | Good frames |
|---|---|
| Worn on the body | **447** |
| On the table, nobody near | **0** |

`seen 0` — the receiver never even attempted a decode. 15 false syncs, which is
noise. **Zero frames through the air, 447 through the body.**

The earlier note that "with 100 k the boards were seen completing handshakes on
the bench with no body" therefore only holds at close range. At hand spacing
there is no air path at all. For the demo and the video, keep the bands apart.

### R1 = 10 k is a regression — do not re-chase

R1 is in series between GP11 and the pad, so the divider maths says lowering it
drives the body harder. It does. It also makes the link worse:

| Worn, 200 kHz | R1 = 100 k | R1 = 10 k |
|---|---|---|
| Good frames | 447 | 343 |
| **Good frame rate** | **70 %** | **44 %** |
| FER | 0.296 | 0.558 |
| BER | 1.87e-02 | 7.46e-02 |
| Mean margin | 19 | **32** |
| Worst gap | 3 | 3 |

Margin rose 19 -> 32, so the extra drive is real and the maths was right. The
delivered-frame rate still fell from 70 % to 44 % over ~1400 frames. Reverted to
100 k on both bands.

**Why it gets worse is NOT established.** The first reading suggested R1 = 10 k
doubled the noise (raw RMS ~365 -> ~718, 200 kHz chip energy 28.7 -> 56.5, five
readings each). But after reverting to 100 k the same measurement read 540-577
raw and 71-80 at 200 kHz — higher than either earlier state. **Bench noise
readings move with where the bands are sitting relative to the PC and its
cables, so they are not comparable across sessions.** Only the worn frame
statistics are trustworthy here. Do not quote the noise numbers as the cause.

R2 was left at 100 k throughout and should stay there: it feeds a high-Z node
with R3 = 1 M to VREF, so 100 k -> 10 k is under 1 dB, and it attenuates pad
noise and pad signal equally, so it cannot change SNR at all.

The E9 input-buffer noise path on GP11 was checked and is **already mitigated**
— `hal_pico` calls `pio_carrier_sense(false)` at init and on every carrier
change, so the buffer is off on the link. That easy win does not exist.

## Disproved — do not re-chase

1. *The firmware DSP fails at 200 kHz.* No. Verified on the host with the real
   `gz_push`/`sync_push`/`frame_rx_push`: sine, PIO square wave, through a
   modelled MCP6292 front end, and with hard rail-to-rail clipping at 30x
   overdrive. Clean decode every time, both carriers.
2. *It is saturation.* No. Clipping does not break the decode in simulation, and
   100 k decoded perfectly back to back while clipping.
3. *The resistor sweep shows R2 matters.* It cannot. R2 is in series into the
   high-Z node with R3 = 1 M to VREF, so node/pad is 0.500 at 1 M, 0.909 at
   100 k, 0.990 at 10 k. The step the old brief called dead-to-alive changes the
   received signal by **8.9 %**. That sweep is confounded.
4. *The USB ground loop is the noise source.* No. Unplugging the second board
   made the noise slightly worse.
5. *Turning the gain up will help.* No. The noise enters ahead of the ADC, so
   more gain amplifies it equally.

## Corrections to the old brief's stated facts

- C1 is **330 pF**, not 1 nF (`hardware/bom.csv`, `gen_schematic.py`). The
  "C1's time constant equals a chip" concern is void.
- The chip rate is **4000 chips/s (250 us) at both carriers**, not 10 kchips/s
  at 200 kHz. It is derived from sample rate / GZ_N / windows-per-chip; nothing
  in it depends on the carrier and no build overrides it.
- `adc` in `bringup.c` is a tight loop of blocking one-shot `adc_read()` calls —
  software paced, no sample clock, ~2 us a conversion. At 200 kHz that is ~2.5
  samples a cycle beating against the carrier. **The entire 200 kHz column of
  the old self-test table is an aliasing artefact**, including the mean climbing
  to 2805 mV. Use the ADC ring and `m`, never `adc`, above about 40 kHz.

## Open

1. ~~**Does the ground electrode stop the dropouts?**~~ **Answered 24 Sep
   evening: yes.** What is still open is the 30 % FER that remains — the link no
   longer dies, but it is not yet reliable.
2. **Which side is dropping out — skin or room?** Two 60-second tests separate
   them, and neither has been run:
   - wearer presses the band hard against the wrist; if level jumps, it is the
     pad-to-skin capacitance (§8.3 — tape thickness is in the signal path)
   - wearer touches earthed metal; if level jumps, it is the environmental
     return (§15.1 — "rubber soles on a dry insulating floor can gut
     body-to-earth capacitance")
3. ~~**Design §14.1**~~ **Answered 24 Sep evening: 447 frames worn, 0 on the
   table at the same spacing.** The bench handshakes with no body only happen at
   close range, not at hand spacing.
4. The R3 / U2-pin-3 one-way junction on 93D1 has never been re-measured.

## Bench recipe

Boards are addressed by serial tail, not COM port. `picotool` lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`. `-x` goes before the
filename, `-f` and `--ser` after.

    python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb

    picotool load -x build-pcb/linktest_tx.uf2 -f --ser 4904EF1FFA2393D1   # 93D1, TX
    picotool load -x build-pcb/linktest.uf2    -f --ser 36B9A3C84974379E   # 379E, RX

    python scripts/link2.py --a 93D1 --b 379E --run 40 \
        --at "2:A:c 200" --at "4:B:c 200" --at "8:B:z" \
        --at "20:B:m" --at "36:B:s" --log run.log

**`m` on the receiver is the instrument that matters** and nothing before 24 Sep
had used it. It prints max chip energy (the signal, in LSB) and raw RMS (the
noise). `p` on the transmitter pauses it — `m` with the transmitter paused is a
clean noise reading. Those two numbers are `handoff_ber --amplitude A --noise
sigma`'s operating point.

Only 20, 40, 100 and 200 are valid arguments to `c`.

`r` on the receiver dumps raw ADC across the next frame; `tools/replay.py` feeds
it to `handoff_decode`, the firmware's own pipeline, which splits "the firmware
differs from the model" from "the channel differs from the model". Nobody has
ever looked at raw samples through a frame. It needs the transmitter running or
it blocks.

## Working with this owner

- **The owner is dyslexic.** Short, plain, numbered. No code in explanations.
  Name test pads by signal (GND, VREF, OUT1, ADC0, PAD), not TP number.
- **You run the tests, not the owner.** Flash, launch and read the boards
  yourself; the owner does the physical steps only.
- **The bands are strapped to the wearer's hands.** You cannot ask for the pads
  to be held together. Ask for postures, contact and earthing instead.
- Ask before a timed run and wait — several runs have been wasted with the bands
  sitting on the bench.
- Design §13.1: battery only, both ends, fully floating; never tether a band to
  a mains-powered laptop while anyone touches an electrode. Every run on 24 Sep
  broke this rule.

## Code state

Three uncommitted changes in the tree:

- `firmware/lib/dsp/carrier.c` — `slow_shift` 7 -> 11, from the previous
  session. **This breaks `test_beacon.c:427`** ("re-priming the carrier detector
  mid-frame is not safe"). Verified: green stashed, red restored. The test is
  self-documenting and says design §5.1 should be revisited if the constant
  changes. `python scripts/test.py` is red until this is resolved.
- `firmware/apps/linktest/linktest.c` and its `CMakeLists.txt` — the SMPS fix.
  Worth keeping; belongs in `handoff.c` and `bringup.c` too.

`carrier.c`'s `min_delta = 24` is worth knowing about: the handoff app gates
rendezvous on `carrier_present`, which needs `level > floor + 24`. `linktest`
does not gate on it at all. At the levels seen on skin without the ground
electrode (10-16 in the old brief) the app could never have started, whatever
the decoder did.

## Deadline

The element14 contest closes 23:59 UK on 27 Sep 2026 and needs video and photos.
