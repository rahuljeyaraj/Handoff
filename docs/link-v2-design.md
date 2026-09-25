# Link v2 — a link with no tuned constants

Branch `redesign/link-v2`, from `main` at `2de842e`, 25 Sep 2026.

This is a ground-up redesign of the communication mechanism. It is not a fix
for the carrier floor. It removes the part of the system the floor lives in.

Nothing here is built yet. Read §10 before writing code.

---

## 1. The rule

Every number in the link must be one of these four things. If it is none of
them, it does not go in.

| Kind | Example | Why it is legal |
|---|---|---|
| **Physical** | ADC runs at 500 kHz | Set by the hardware. Measurable. |
| **Structural** | a byte is 8 bits; the preamble is 16 chips | Part of the format. Changing it changes the protocol, not the tuning. |
| **Derived** | chip rate = window rate / windows per chip | Computed from the above. Already how `config.h` works. |
| **Stated requirement** | one false sync per 24 h; handshake inside 1 s | A number *you* choose as a goal, from which a threshold is then computed. |

Illegal: anything whose value came from watching a bench and turning a knob.

`min_delta = 24` is the clearest example of what has to go. It is 24 **LSB** —
an absolute amplitude, on a link whose amplitude changes with grip, posture,
footwear and which board you picked up. It cannot be right twice.

---

## 2. What is actually wrong with v1

Not the floor. The floor is a symptom.

**v1's trigger is a featureless tone.** A flat 200 kHz shout carries nothing
but energy. So the only question the receiver can ask is *"is there more
energy than usual?"*, and to ask that it must know what *usual* is. That is
the floor. The floor is not a design choice — it is forced on us by a trigger
with no content.

Once the floor exists, everything downstream inherits an absolute reference:

| Thing | What it needs | Where it breaks |
|---|---|---|
| presence | floor + 3× ratio + 24 LSB | the floor climbs, it goes deaf |
| shout vs noise | `SHOUT_MIN_US`, `QUIET_WAIT_MAX_US` | tuned against one bench |
| role election | who heard whose tone first | a timing race with no tiebreak |
| preamble hunt | an adaptive slicer with its own decay | freezes after a transient |

Four separate subsystems, all reaching for the same missing reference.

---

## 3. The change, in one sentence

**Stop measuring energy against a remembered number. Measure it against
another measurement taken in the same window.**

Two consequences, and they are the whole design:

1. Transmit **two tones** instead of one. Every data decision becomes a
   comparison of two bins measured at the same instant, so gain, coupling and
   noise cancel exactly.
2. Give the **trigger content** — a tiny frame with a CRC — instead of a bare
   tone. "Did I hear a peer?" stops being a threshold question and becomes
   "did a CRC pass?", which is self-validating and has a false-alarm rate you
   can calculate rather than tune.

`carrier.c` is then not fixed. It is deleted.

---

## 4. Physical layer: five bins, two of them transmitted

The Goertzel bin spacing is the window rate: 500 kHz / 25 = **20 kHz**. Bin
centres are exact multiples of it, and an on-bin tone is exactly zero in every
other bin — no leakage, stated in `goertzel.h` and already relied on.

So we get orthogonal channels for free.

| Bin | Frequency | Role | Transmitted? |
|---|---|---|---|
| k=7 | 140 kHz | guard (noise) | no |
| k=8 | 160 kHz | guard (noise) | no |
| **k=9** | **180 kHz** | **tone A** | yes |
| **k=10** | **200 kHz** | **tone B** | yes |
| k=11 | 220 kHz | guard (noise) | no |

**The two tones are adjacent bins — the closest the transform allows.** That is
deliberate, and it is the single most important choice in this table. Coupling
rises with frequency, so two tones far apart arrive at different strengths and
the receiver needs a correction. Put them one bin apart and the difference
falls to about 10 % (≈1 dB), and both tones sit at the top of the band where
coupling is best. A tone pair at 160/200 kHz was the first proposal; it is
worse in every direction and is recorded here only so it is not rediscovered.

**Guard bins are receive-only, so they are free.** They need no PIO divider,
no transmitter, no protocol. They are three live noise meters sampled at the
same instant as the signal, in the same amplifier, through the same body.

That is what replaces the floor. Not a better average — a measurement.

### System clock

Both tones must come out as a whole, even number of system cycles per period
(§7 explains why even).

| sys_clk | 180 kHz | 200 kHz |
|---|---|---|
| 150 MHz (today) | 833.33 ✗ | 750 ✓ |
| 126 MHz | 700 ✓ | 630 ✓ |
| **144 MHz** | **800 ✓** | **720 ✓** |

So the board runs at **144 MHz** — a 4 % cut, not the 20 % the earlier
160/200 pair would have cost. That matters: core 1 is about to take on five
Goertzels and needs every cycle it has.

The ADC and USB clocks come off their own PLL and should be unaffected —
*verify this first, it is step 1 in §10.*

### The harmonics have to be checked, not assumed

A square wave carries odd harmonics. At 500 ksps they fold back into the band,
and a harmonic landing in a guard bin would poison the noise reference with our
own transmitter — v1's floor with extra steps.

| Tone | 3rd | 5th | 7th |
|---|---|---|---|
| 180 kHz | bin 2 | bin 5 | bin 12 |
| 200 kHz | bin 5 | DC | bin 5 |

So bins 2, 5 and 12 are unusable as guards. Bins 7, 8 and 11 are clear, which
is what fixes the guard set above.

Even harmonics would land in bins 7 and 11 — but they are exactly zero for a
50 % duty cycle, and §7's generator produces one to the cycle. That makes those
two bins a **built-in duty-cycle monitor**: if the generator ever slips, they
are the first place it shows. One measurement, two jobs.

### Why OOK was chosen, and why that reason was wrong

Design §9.1 rejects FSK because it "requires phase or frequency tracking
between two independent crystals". That is true of **coherent** FSK. It is not
true of what is proposed here: two Goertzel magnitudes compared to each other
need no phase at all, and crystal offset is tens of ppm — a few Hz against a
20 kHz bin. The original rejection conflated coherent and non-coherent
detection.

### Two things this buys for free

- **Constant envelope.** The pad is always driven during transmit, so there is
  no DC step, no interstage capacitor recovering from one, and design §9.8's
  released-space trick is no longer needed. Turnaround becomes the amplifier's
  own settling time, which is a measurable RC, not a guess.
- **+3 dB.** OOK is off half the time. FSK is not.

---

## 5. Every decision, and what it is measured against

This is the table that matters. Nothing in the right column is remembered.

| Decision | v1 | v2 |
|---|---|---|
| Is this chip a 1 or a 0? | Manchester halves *(already fine)* | `E_A` vs `E_B`, same window |
| Is anyone transmitting? | level vs tracked floor + 24 LSB | `max(E_A,E_B)` vs median of the three guards |
| Is this a preamble? | adaptive slicer + 22-of-24 alternation | sign of `E_A − E_B` alternating — no slicer exists |
| Is that my own echo? | sample-time cut + hope | nonce in the beacon |
| Who sends first? | who heard whose tone first | higher nonce |
| Is the frame good? | CRC-16 *(already fine)* | CRC-16 |

Read down the v2 column: **every entry is a comparison of two things present
at the same moment.** No state, no time constant, no reset, no prime, no
freeze, no hysteresis.

---

## 6. Presence, without a floor

```
signal  = max(E_A, E_B)
noise   = median(E_140, E_160, E_220)
busy    = signal > noise × k
```

Three notes, and they are the whole point:

- **`median`, not mean.** One interferer landing in one guard bin cannot move
  a median of three. This is standard CFAR, not an invention.
- **`k` is derived, not tuned.** It comes from a stated false-alarm rate. For
  magnitude-squared of Gaussian noise the null distribution of that ratio is
  known, so `k` is computed once from "I will accept one false busy per N
  seconds" and written into the build as a derivation, next to its working.
- **No square roots.** Every comparison here is a ratio, so it can be done on
  `mag²` with integer cross-multiplication. `gz_isqrt64` leaves the hot path.

And `busy` has exactly one consumer: listen-before-talk. Everything else that
used to ask `carrier_present()` now asks the frame decoder, which is
self-validating.

### What this fixes, concretely

The 379E reading from the brief — peak 133–150, floor 61–83, deaf — cannot
occur. There is nothing to climb. If the room is genuinely noisy, the guard
bins read high *in the same window*, and the ratio is unchanged.

---

## 7. Rendezvous, without a timing race

v1's trigger is a 10 ms tone, timed against `SHOUT_MIN_US = 9000` to tell a
peer from a burst off the room. Four bugs on 24 Sep came out of that scheme.

v2's trigger is a **beacon frame**:

| Field | Bits | Purpose |
|---|---|---|
| preamble | 16 chips | timing lock |
| marker | 8 | frame start |
| nonce | 16 | identity and election |
| flags | 8 | have-your-record, reply |
| CRC-16 | 16 | validation |

About 30 ms at 4000 chips/s. Then:

- **"Is that a peer?"** — the CRC passed. False-accept rate 2⁻¹⁶ by
  construction, not by tuning.
- **"Is that my own echo?"** — the nonce equals mine. Structural. The "own
  shout heard" bug cannot be written.
- **"Who sends?"** — higher nonce. No race, no backoff constant. Tie
  probability 1.5 × 10⁻⁵, resolved by redraw.
- **Beacon period** — derived from the 1 s handshake requirement in design §2
  and the beacon's own airtime, with the collision probability computed.

`beacon.c`'s `SHOUT_US`, `SHOUT_MIN_US`, `TRIG_SETTLE_US`, `LISTEN_MIN_US`,
`LISTEN_MAX_US`, `QUIET_WAIT_MAX_US` all go. So does `trig_take_carrier_reprime`,
because there is no floor to reprime.

---

## 8. Sync and data, without a slicer

`frame.c`'s adaptive slicer exists because the preamble hunt needs hard 1/0
decisions before Manchester lock, and the only way to get one from a single
energy stream is a threshold.

With two tones there is no threshold. A chip is `E_A > E_B` or it is not. The
preamble is tones alternating, the marker is the same trick on the same sign,
and `hi`, `lo`, `primed` and `step_toward` all delete.

`FRAME_ALT_WINDOW = 24` / `FRAME_ALT_MIN = 22` stay, but they are promoted
from tuned to derived: they come from the stated "one false sync per 24 hours"
and the chip rate. The derivation already exists in `frame.h` — it just needs
to be computed rather than typed.

### Manchester stays, and this is a decision, not a deferral

Design §9.2 gives Manchester two jobs: beating threshold drift, and guaranteeing
bit timing. FSK retires the first — there is no threshold left to drift. The
second stands: a run of identical bits is a run of one tone with no transitions,
and `sync.c`'s tracker needs edges.

Dropping Manchester would double the data rate, but it would cost a scrambler
and a run-length guarantee to put the transitions back — more machinery, not
less, and machinery with its own failure modes. Manchester is structural, it is
one line of code, and it is already tested. **It stays.** Do not revisit this as
a throughput optimisation without a measured reason.

### One real problem, and the fix

180 kHz couples slightly worse than 200 kHz — capacitive coupling rises with
frequency — so tone A arrives about 10 % weaker than tone B. Small, because the
tones are adjacent bins, but not zero, and a raw `E_A > E_B` would be biased.

**Do not correct it with a constant.** The preamble alternates the two tones,
so it measures the imbalance directly, every frame, on the link as it is at
that moment. Carry that ratio into the body decisions. The AFE's own response
difference at the two tones is absorbed by the same measurement.

This is the design rule applied to itself: when a correction is needed, take
it from the signal, not from the bench.

---

## 9. What is left, and where each number comes from

| Constant | Value | Kind | Derivation |
|---|---|---|---|
| `ADC_FS_HZ` | 500 k | physical | converter |
| `SYS_CLK_HZ` | 144 M | physical | both tone periods must be a whole even number of cycles (§4, §7) |
| `GZ_N` | 25 | structural | sets 20 kHz bin spacing |
| `WINDOWS_PER_CHIP` | 5 | structural | chip rate 4 kHz |
| tones | bins 9, 10 | structural | adjacent, §4 |
| guards | bins 7, 8, 11 | structural | clear of odd harmonics, §4 |
| `CFAR_K` | — | **derived** | from stated false-busy rate |
| `ALT_MIN` | — | **derived** | from stated false-sync rate |
| nonce width | 16 | stated req | tie rate 1.5e-5 |
| CRC width | 16 | stated req | false-accept 2⁻¹⁶ |
| beacon period | — | **derived** | from the 1 s handshake requirement |
| turnaround | — | **physical** | AFE high-pass RC, measured |

Eleven entries. Three computed at build time from a requirement, the rest
physical or structural. **None tuned.**

Against v1's `fast_shift`, `slow_shift`, `rise_shift`, `ratio_num`,
`min_delta`, `hold_chips`, `prime_chips`, `prime_shift`, `freeze_chips`,
`FLOOR_FRAC`, `EDGE_DECAY_SHIFT`, `SHOUT_US`, `SHOUT_MIN_US`,
`TRIG_SETTLE_US`, `LISTEN_MIN_US`, `LISTEN_MAX_US`, `QUIET_WAIT_MAX_US`,
and the slicer's two shifts.

### Files

| File | Fate |
|---|---|
| `dsp/carrier.c` / `.h` | **deleted** |
| `proto/beacon.c` / `.h` | rewritten — nonce beacon |
| `dsp/goertzel.c` | extended to a bank; `isqrt` off the hot path |
| `dsp/sync.c` | kept; edge detector now runs on the tone difference |
| `link/frame.c` | slicer deleted, decisions become sign comparisons |
| `proto/link_sm.c` | `carrier_present()` callers rewired |
| `hal_pico/pio_carrier.c` | two-tone generator |

---

## 10. Build order

Each step is a bench measurement, not a feature. Stop at any step that fails.

| # | Step | Passes when |
|---|---|---|
| 1 | Move sys_clk to 144 MHz | ADC rate and USB unchanged; link still works as-is — **PASSED 3b4e902** |
| 2 | Two-tone PIO generator | see below — no external hardware needed — **PASSED baa2782** |
| 3 | 5-bin Goertzel bank on core 1 | no dropped windows at 500 ksps; budget printed — **PASSED**, 46 cycles a sample, 16 points of core 1 |
| 4 | Passive: one board TX, one RX, plate to plate | `E_A`/`E_B` separate cleanly; guards do not rise while transmitting — **PASSED**, guard median flat across an 11× signal sweep |
| 5 | Presence by guard median, tethered | busy tracks reality with the level swept — **PASSED 1fd27d0**, crossover on the derived k |
| 6 | Frame decode with no slicer | BER at least as good as v1 on the same bench — **PASSED**, better at two gaps of three, and core 1 falls to 21 % |
| 7 | Nonce beacon and election | two boards, no double-send, no self-trigger — **PASSED**, and the election was derived away rather than built |
| 8 | Skin path, on cells, floating | §14.1's 447-frame result matched or beaten |

Step 4 is the one that decides whether this is worth it. Do not build past it
on faith.

### Step 2 in detail — there is no scope on this bench, and none is needed

The board has measured itself since M3. `pio_carrier_measure_hz()` runs a
second PIO state machine that counts edges on the pad independently of
whatever is driving it, and `linktest`'s `x` command loops our own carrier
into our own receiver.

| | Check | Passes when |
|---|---|---|
| 2a | drive each tone continuously, read `measure_hz` | 180000 and 200000, within 0.1 % |
| 2b | self loop, alternating chips, watch the bin bank | bins 9 and 10 alternate cleanly, guards near zero |
| 2c | self loop, a known 624-chip pattern | no decode errors accumulating toward the end of the frame |

2b proves four things in one measurement: both tones exist, both are on bin
centres, they switch on chip edges rather than a few cycles late, and the
receiver separates them. A scope would show only the first.

2c is the specific hazard `pio_carrier.pio` warns about — a cycle-count
imbalance between the two symbol paths shifts every chip after the first, so
the errors pile up down the frame rather than scattering.

### What 2b could not do, and what replaced it (25 Sep 2026)

**The self loop saturates the receiver, so it cannot measure a guard bin.**
Driving tone A reads guard bin 7 at 390 LSB against tone A's 809, which is
§11's kill switch — but the duty at the pad is 50.0008 %, the raw operating
point is mean code 3564 of 4095, and the **v1** generator through the same loop
rails harder still. The board has always done this listening to itself; it is
what the ~65 dB of a body path exists to avoid.

So the transmitter is verified **at the pad** instead, which needs no receiver
at all and is stronger where it overlaps:

| | measured | how |
|---|---|---|
| period | 180005 / 200000 Hz, 28 / 0 ppm | edge count over a gate |
| its two halves | duty **50.0008 / 50.0000 %** | a third state machine counting high cycles |
| chip alignment | **exact**, 16 counts across two boards | each chip is a whole number of periods, so the edge count across a chip pattern is exact arithmetic |

The duty measurement is what makes this sound. §4 makes guard bins 7 and 11 a
duty-cycle monitor, and that is right — but it is a monitor *through the
amplifier*, so it cannot separate a generator that slipped from an amplifier
that distorted. Reading the duty on the pad can, and does.

**A guard-bin reading is only meaningful at a linear level**, so step 4 is
where the noise reference is judged. That is where it always belonged — this
only removes the false comfort of thinking step 2 could pre-empt it.

### What step 3 measured about §6, 25 Sep 2026

The bank is built and costed (brief §4). Two things it settled early:

**The median is not a nicety.** Driving tone A into the self loop, the three
guards read 2, 23 and **391** — bin 7 is the aliased second harmonic of a
receiver railed on its own transmitter. The median is 23. A mean would have
been 138, and a mean is what would have deafened the receiver. §6's first note
is now a measurement.

**A quiet room reads as one number five times over.** Both boards silent: 17,
18, 29, 24, 19 LSB across bins 9, 10, 7, 8, 11 — the signal *below* the guard
median, which is what "there is nothing to climb" looks like as a reading.

### What step 4 measured, 25 Sep 2026 — the gate, and three corrections

**PASSED**, two boards, passive, coupled plate to plate with **no wire**. Ten
matched quiet/tone pairs, every capture linear, the signal swept 64 → 716 LSB
by closing the gap. Brief §5 has the full tables; three things here change what
this document says.

**The kill switch does not fire.** The guard median tracks the ambient and not
the transmitter, at every level: at the loudest, quiet 79 → tone A 76 → tone B
74 → alternating 71. CFAR margin 94 in power for tone B, 52 for tone A, against
a `k` of about 10.

#### Correction 1 — §8's imbalance has the sign backwards

§8 predicts tone A arrives **~10 % weaker**, because coupling rises with
frequency. Measured on the alternating pattern, which reads both bins in the
same windows: **tone A is 9.1 % stronger**, and it holds to ±0.5 % across a
2.3× level range. The magnitude §4 argued for is right and the direction is
wrong — the AFE's own response across two adjacent bins evidently outweighs the
coupling slope. It changes nothing mechanically, because §8 takes the ratio from
the preamble rather than from a constant, which is exactly why that was the
rule. It does mean the words "tone A couples slightly worse" are not true of
this hardware.

#### Correction 2 — bins 7 and 11 monitor the RECEIVER, and only for tone A

§4 calls bins 7 and 11 a built-in duty-cycle monitor. They are a monitor, and
a good one, but not of the generator:

- At contact the receiver saturates — mean code walks 2309 → 3624 and the
  excursion leaves the converter. Bin 7 then reads 369 against tone A's 729,
  bin 11 reads 31. Both are **even** harmonics (2nd folds 360 → 140, 4th folds
  720 → 220) and the duty at the pad is 50.0000 %, so the square wave cannot
  have sent them. Walking every bin puts the whole comb where the arithmetic
  says, 3rd at bin 2 and 5th at bin 5, plus fill-in at bins 3 and 6 that no
  harmonic of the tone can reach. The distortion is made **after** the coupling.
- **And tone B cannot be watched this way at all.** 200 kHz is exactly
  `ADC_FS_HZ / 2.5`, so its harmonic comb closes on bins 5, 10 and DC and never
  touches a guard. Measured, not predicted: driven into saturation on tone B the
  three guards read 20–28 LSB, flat.

So half the symbols are invisible to the even-harmonic monitor, and a guard
reading cannot by itself say the receiver was linear. That is what the
operating-point print under every `n 2` is for — the mean code walking is a
saturation signature that assumes no crest factor at all.

#### Correction 3 — the three guards do not agree; they sit on a fixed slope

§6 expects the three to read as one number. Silent and settled they read
**116 / 88 / 52** LSB on bins 7 / 8 / 11 — a 2.3:1 fall with frequency, stable
and repeatable, which agrees with this project's earlier finding that the pad
noise is 1/f. It is a slope, not scatter, so `median` of three **always selects
bin 8**, the guard between the tones, with 7 and 11 serving as the outlier
protection CFAR wants them for. §6's reasoning survives; its arithmetic should
say bin 8 is the reference rather than implying an average of three.

### What step 5 measured, 25 Sep 2026 — and the one thing §11 missed

**PASSED.** `carrier.c` is deleted. `busy` tracks the level across an
eleven-fold sweep, and the crossover lands on the `k` §6 derives — not near
it, on it: at the hold where the instantaneous ratio read 12:1 against a
threshold of 16.76, exactly 58.5 % of windows read busy. Brief §6 has the
tables.

#### Correction 4 — §11's kill list is missing the one that nearly killed it

§11 lists four ways this design dies. None of them is what actually went
wrong, and the real one is not about the DSP at all: **core 1 runs from
flash.**

`apps/handoff` hit 98 % core-1 load with 1709 DMA overruns in thirty seconds
and not one handshake completing, while `main` on the same bench at the same
gap managed 13 complete with 0 overruns at 35 % load. The obvious reading —
"the bank does not fit" — was wrong, and the instrument said so: a live
two-second interval read **46 %**, not 98 %, while overruns kept arriving.
Blocks are not dropped at 46 % load. They are dropped when something STALLS
core 1 rather than keeping it busy.

Core 1 executes from XIP, and `apps/handoff` has BTstack on core 0 using the
same flash. The bank's window end runs 20 000 times a second, and every one of
those was a chance to stall on an instruction fetch. Average load was fine;
worst-case latency was not. `linktest` never showed it because core 0 there is
an idle console — **which means every core-1 budget taken in `linktest` is a
lower bound, not a budget.**

The whole core-1 path now runs from RAM. Not one instruction of arithmetic
changed; only where it lives. Step 3 decided against this because the bank fit
without it, and it does — beside an idle console.

**So add to §11's list, above all four of the others:** the core-1 loop must
be measured in the app that carries the Bluetooth stack, not in the one that
carries a console. A DSP budget taken with core 0 idle does not transfer.

#### Correction 5 — §6's "no square roots" is a hard requirement, not advice

The first presence implementation took two `isqrt64` and a 64-bit divide every
window, for telemetry nobody reads more than once a second. Core 1 went to
73 % against a 55 % baseline: **eighteen points, for two numbers a human looks
at.** Core 0 now raises a flag and the next window answers it, and the
decision itself cross-multiplies in line rather than through
`gzb_ratio_gt()`, whose overflow guard divides. Measured after: presence costs
**under one cycle a sample** on top of the bank.

#### What §6 did not anticipate: v1's waveform

§6 assumes both symbols are tones. Until step 6 they are not — **v1 switches
the carrier off for a zero** — so half of every v1 frame is silence and a
detector with no memory answers "nobody is transmitting" in each gap,
truthfully. No rendezvous completed at any phase in the simulator.

`link_sm.c` therefore carries a bridge whose length is DERIVED: the longest
run of identical chips Manchester can produce, plus one because `hal_rx_busy()`
answers about an interval rather than an instant. `manchester.h` states the run
length and a host test walks all 65 536 bit pairs to prove it. **It deletes at
step 6**, when constant envelope makes it unreachable — which is §4's second
"free" benefit arriving as a requirement rather than a bonus.

---


### What step 6 measured, 25 Sep 2026 — and the correction it forces on §8

**PASSED.** The slicer is deleted, the OOK bridge is deleted, and the radio is
FSK end to end. BER is at least as good as v1 at every gap measured and
strictly better at two of three; `apps/handoff` completes the same 15
handshakes in 42 s for **21 % of core 1 against v1's 35 %**. Brief §7 has the
tables.

#### Correction 6 — §8's imbalance correction is not needed, and that is a derivation

§8 says a raw `E_A > E_B` would be biased, and asks for the ratio to be
carried from the preamble into the body decisions. §10 correction 1 already
found the sign backwards. Step 6 finds the whole requirement absent, for two
reasons that are arithmetic rather than measurement:

- **The comparison is never between two signals.** Step 4 measured the
  off-tone bin as indistinguishable from silence — 62 against 67 quiet, while
  the on-tone bin read 716. So `sign(E_B - E_A)` is a signal against a noise
  floor, and 9 % cannot change its sign.
- **It cancels in the body, by construction.** Manchester puts one tone-A chip
  and one tone-B chip in EVERY bit, so the difference of the two halves is
  ±(S_A + S_B) whatever the two strengths are. Symmetric, not approximately
  symmetric.

`test_frame.c` sweeps the imbalance to 3:1 — thirty times worse than this
hardware, in both directions — and no decision changes. So nothing corrects
it, which is §1 applied to itself: a number goes in when a requirement asks
for it, and none does. The framer still MEASURES it off every synced preamble
and reports it, because an instrument costs nothing and a correction would
have been machinery with nothing to correct.

#### The bridge deleted itself, and presence said so in one number

`p` with frames flowing reads **53227 busy windows of 60046 — 88.6 %**, and a
156 ms frame every 176 ms is 88.6 %. The detector is busy for exactly the
airtime. §4's "constant envelope" arrives as a deletion rather than a bonus:
there is no gap inside a frame left to bridge.

#### What the branch has given back, cumulatively

Core 1 was 46–47 % at step 5 and is **21 %** now, in `handoff`, beside
BTstack. Nothing was optimised. The v1 Goertzel and its symbol sync came out
of the link when the bank became the receiver, and the retunable Goertzel that
remains is a console probe that runs only while someone is looking at it.
Correction 4's warning still stands — a budget taken in `linktest` is a lower
bound — which is why that 21 % is quoted from `handoff` and not from the
console app.


### What step 7 measured, 25 Sep 2026 — and the second thing §7 asked for that does not exist

**PASSED.** The flat-tone shout is a frame carrying a nonce under a CRC-16.
`SHOUT_US`, `SHOUT_MIN_US`, `LISTEN_MIN_US`, `LISTEN_MAX_US`,
`QUIET_WAIT_MAX_US`, `DETECT_US` and the whole `TRIG_WAIT` state are deleted.
Brief §8 has the tables.

#### Correction 7 — §7's election is machinery with nothing to elect

§7's table says the sender is the band with the **higher nonce**. It is not,
and this is the same shape as correction 6: the requirement the comparison
would serve does not exist.

A band is deaf from the start of its own beacon until its amplifier and the
ADC ring have cleared. So:

- Decoding a peer means you had **not yet started your own beacon** this
  cycle — otherwise you were deaf for part of theirs.
- A decoder stops beaconing, because it leaves the trigger as the sender. Its
  beacon never goes out, so the band it decoded has nothing to decode.
- If you could not decode because the two beacons **overlapped**, neither
  could they: your beacon began inside their deaf window.

**At most one band ever decodes the other.** That is v1's timing algebra
surviving intact, which is the part of v1 that was never broken — what was
broken was inferring "a peer" from how long a tone lasted. The nonce keeps the
job that does have a requirement behind it, which is the one the 24 Sep bug
came from: *is this my own echo?* The tie falls out of the same comparison,
because a band that hears its own nonce cannot tell an echo from a peer that
drew the same sixteen bits, and both want the same answer — stand down, and go
out next time under a different name.

Measured: 774 beacons from a band alone on the bench, **0 sends, 0 receives,
0 self-echoes, 0 framer syncs**. v1 elected itself sender on 60 shouts out of
60.

#### Correction 8 — §7's 16-chip preamble cannot meet the stated false-sync rate

§7's beacon table gives the preamble as 16 chips. There is no hunt window for
which that works: the rule needs a run longer than the window, 16 chips plus
four marker bits is a run of 24, and a 23-transition window against
`HANDOFF_FALSE_SYNC_S` is four orders of magnitude out. The beacon therefore
shares the card frame's **32-chip preamble and its hunt**, and the beacon is
112 chips — 28 ms, which is what §7 estimated anyway.

Accepting a second marker tail doubles the false-sync rate, and the window
pays for it: 29 of 30 transitions becomes **28 of 30**, computed, with both
ends still asserted. One chip of window is the entire cost of a second frame
type.

#### Correction 9 — the settle is the RING, not the AFE

§9's table calls the turnaround "physical — AFE high-pass RC, measured", and
brief §8 asks for that reading. There is no RC to read. `y 4` drives the
board's own pad, releases it and watches presence: the signal bin reads ~670 at
0, 1 and 2 ms and ~20 by 4 ms, which is a **step at exactly one DMA block**,
not a decay. The amplifier is back inside 123 µs; everything else is the ring
draining.

So the settle is derived rather than measured-and-typed —
`HANDOFF_RX_LATENCY_US + HANDOFF_TURNAROUND_US`, 4096 + 1000 — and it **does
not grow with the beacon**, which was the real worry when the transmission went
from 10 ms to 28. Three drive lengths, eight runs each: means 3230, 3949 and
3010 µs, with the longest drive not the worst.

#### What the phase sweep caught that no amount of reading would have

The first beacon marker was picked on the obvious criterion, Hamming distance
from the card's tail. It was wrong, and the sweep failed on it.

A receiver that joins a frame late never believes the marker's first 00, so it
goes on hunting **through the body** — and a Manchester run of identical bits
is a run of alternating chips, which is what a preamble looks like. The marker
with the furthest tail has only two chip-pair violations, so the window is
clean again one chip into the body, and a nonce beginning 0000 then presents a
perfect card marker. One nonce in sixteen, and two phases of 141 where both
bands ended up listening.

The marker with **four** violations holds it off for 29 chips of the body and
leaves 39 nonces in 65536; `frame_beacon_nonce_ok()` refuses those at the draw,
so it is none. The nonce is the one field in this protocol whose value is free,
which is why rejection sampling is the honest fix and a whitener would not have
been.

---

## 11. What would kill this

Honest list. Any of these and we stop.

- **Core 1 cannot run five Goertzels at 500 ksps.** Measured at step 3. Fall
  back to three bins (one guard, at 180 kHz between the tones) before
  abandoning the idea.
- **The AFE is not flat enough between 140 and 220 kHz.** If the guard bins
  see a wildly different gain from the tones, the CFAR ratio is biased. Step 4
  measures it. A one-time per-board calibration would be a tuned constant by
  another name — if it comes to that, the design has failed its own rule.
- **The lower tone couples too poorly through skin.** §5's link budget is
  already 65 dB down on the safety resistors, and with FSK the link is only as
  good as the weaker tone. Adjacent bins keep that cost near 1 dB, which is why
  they were chosen — but step 4 measures it rather than assuming.
- **RP2350-E5 or E9 bite again in a 5-channel ADC configuration.** The ring
  already carries one erratum workaround.

---

## 12. What I want from you before step 1

1. **144 MHz.** Is there anything else on the board timed off sys_clk that I
   have not seen — BLE, the flash XIP divider, the motor PWM?
2. **Tone pair.** 180/200 kHz, adjacent bins, both at the top of the band. This
   is the minimum-imbalance choice the transform allows; nothing better exists
   without changing the window length.
3. **Scope.** This branch replaces the radio. It does not touch the record
   store, the carousel, BLE, the app, or the wearer UI. Confirm that is what
   you want.
