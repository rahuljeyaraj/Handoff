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
| 4 | Passive: one board TX, one RX, on a wire | `E_A`/`E_B` separate cleanly; guards do not rise while transmitting |
| 5 | Presence by guard median, tethered | busy tracks reality with the amplifier gain swept |
| 6 | Frame decode with no slicer | BER at least as good as v1 on the same bench |
| 7 | Nonce beacon and election | two boards, no double-send, no self-trigger |
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
