# Link v2 — implementation brief

Branch `redesign/link-v2`. Design is in `docs/link-v2-design.md` — read it
first, this brief does not repeat it.

Nothing is built. This is step 0.

---

## 0. How to work this. Read this part first.

1. **Read `link-v2-design.md` end to end before writing code.** Especially §1,
   the rule about what counts as a legal constant. Every line you write is
   judged against it. If you find yourself picking a number because it worked
   on the bench, stop and write down what requirement it should come from
   instead.

2. **The user is dyslexic. Keep every reply short and plain.** Tables over
   paragraphs. No long explanations unless asked.

3. **Steps are gated. Do not build past a step that has not passed.** Step 4
   in particular decides whether the whole idea is worth it. The last redesign
   failed by patching a mechanism nobody had measured; do not repeat it.

4. **Measure before and after every change.** The suite (`python
   scripts/test.py`) is green at 25202 checks on this branch. Keep it green, and
   add host tests as you go — every step below names the test it owes.

5. **Do not claim a step passed until the numbers are in the transcript.** Not
   "should work", not "looks right". The reading, or nothing.

6. **v1 still exists on `main` and still works well enough to hand off cards.**
   This branch is an experiment. If it dies at step 4 or step 11, that is a
   successful outcome, not a failure — write up why and stop.

---

## 1. Settled. Start at step 1.

All three opening questions are answered. Do not re-ask them.

| # | Question | Answer |
|---|---|---|
| 1 | Move `sys_clk` 150 → 144 MHz? | **Yes.** Go ahead. §2 still lists what to check on hardware. |
| 2 | Tone pair | **180/200 kHz, bins 9 and 10.** Adjacent — the minimum imbalance the transform allows. Not open. |
| 3 | Scope | Radio only for behaviour, but **instruments are unrestricted**. See below. |

### Instruments are unrestricted. Behaviour is not.

The user has explicitly allowed touching working code to make debugging
easier. The line is:

**Change what observes. Do not change what does.**

| | |
|---|---|
| Console commands, counters, log lines, telemetry — anywhere in the tree | **free** |
| Finishing `ble_trig_t` / `BLE_CTRL_*` so the bins can be watched on the phone | **in scope** — it is an instrument |
| How the record store, carousel, ownership, or the app behave | **out** |
| Repurposing an existing BLE field or wire format | **out** — add fields, never reuse one |
| An instrument turns up a bug in working code | **report it, do not fix it here** unless it blocks a step |

That last row is the one that matters. A debugging session that quietly fixes
three unrelated things is how a failed experiment takes working code down with
it. This branch must stay cheap to throw away.

---

## 2. Step 1 — move to 144 MHz

Both tone periods must be a whole, even number of system cycles. 144 MHz gives
that for 180 and 200 kHz — 800 and 720 cycles. This step changes nothing else.

**Change:** `HANDOFF_SYS_CLK_HZ` in `firmware/lib/hal/config.h`, and whatever
`set_sys_clock_khz()` call brings the board up.

**Check each of these, on hardware, before moving on:**

| Thing | Expect | How |
|---|---|---|
| ADC sample rate | still 500 ksps | `clk_adc` comes off PLL_USB, not sys_clk — **verify, do not assume** |
| USB serial | still enumerates | you are using it |
| Flash XIP | boots, no hard fault | it boots or it does not |
| BLE | phone still connects | pair and read status |
| Motor | same feel on the `m` command | PWM divides from sys_clk |
| LED | no colour shift | same reason |
| Existing link | `linktest` BER unchanged | the v1 divider for 200 kHz changes 125 → 120 |

**Passes when:** the v1 link still works at 144 MHz with no other change. If it
does not, the rest of this branch is unreachable — say so and stop.

**Owes:** a static assert that both tone frequencies divide `sys_clk` exactly,
so a future clock change fails the build rather than the bench.

---

## 3. Step 2 — the two-tone transmitter

This is the biggest single piece of work on the branch. Budget for it.

### Why the current generator cannot do it

`pio_carrier.pio` plays a **bit stream**: two bits per slot (pad level, pad
direction), one slot per half-period. To get 180 and 200 kHz from one slot
grid the slot must be 0.5 µs — 5× finer than today.

| | Bits per chip | 624-chip frame |
|---|---|---|
| now | 200 | 15.6 kB |
| 0.5 µs slots | 1000 | **78 kB** |

`CARRIER_TX_WORDS` is 4096 words = 16 kB. Five times over. Not a tweak.

### The replacement: a counted toggle loop

`pio_carrier.pio` rejects this design today, and its reason is worth reading:

> a loop that pulls a chip and then toggles N times spends different numbers of
> cycles on the mark path and the space path unless every branch is
> hand-balanced

**That objection is about mark vs space.** One drives the pad, one releases it,
so the two paths differ. **With FSK there is no space** — both symbols are
tones on the same code path, differing only in a loop counter. The hazard does
not exist, and the cheap generator becomes available.

### The program

**Built and measured, 25 Sep 2026 (`baa2782`). This section is corrected
below: the nine-instruction form is two cycles long per chip, and the
shipped program is fifteen instructions.** The rest of the section still
holds and is worth reading first, because it is the reasoning the correction
rests on.

Nine instructions. One 32-bit word per chip.

```
.program fsk_out
.wrap_target
    out y, 16              ; full periods in this chip, minus 1
    out isr, 16            ; half-period loop count
period:
    set pins, 1     [1]
    mov x, isr
hi: jmp x--, hi
    set pins, 0
    mov x, isr
lo: jmp x--, lo
    jmp y--, period
.wrap
```

Autopull threshold 32, so the two `out`s consume exactly one word.

**The `[1]` on the high `set` is not decoration.** Without it the low half
carries the `jmp y--` and runs one cycle longer than the high half. That is a
non-50 % duty cycle, which puts energy in the **even** harmonics — and the
second harmonic of 180 kHz is 360 kHz, which aliases at 500 ksps onto **140 kHz,
guard bin 7**. The noise reference would then be contaminated by our own
transmitter, which is v1's floor wearing a new hat. Balance the halves.

This is also why each period must be an *even* number of cycles: an odd one
cannot be split into two equal halves.

Bins 7 and 11 are the only guards an even harmonic can reach, so **watch them as
the duty-cycle monitor.** If they rise while transmitting, the generator has
slipped. Odd harmonics land in bins 2, 5 and 12 — which is why none of those is
a guard.

### Cycle arithmetic

Half-period = `isr + 4` cycles. Full period = `2·isr + 8`.

At 144 MHz, SM divider 1:

| Tone | Bin | Cycles/period | `isr` | Periods/chip | `y` (periods − 1) | Word |
|---|---|---|---|---|---|---|
| 180 kHz (bit 0) | 9 | 800 | 396 | 45 | 44 | `0x002C_018C` |
| 200 kHz (bit 1) | 10 | 720 | 356 | 50 | 49 | `0x0031_0164` |

Both chips are **36 000 cycles = 250 µs exactly**. Chip alignment is a property
of the arithmetic, not of hand-balanced branches — which is the same argument
the old bit-stream design made, achieved a cheaper way.

### Correction: the two OUTs are not free, and they cost two cycles a chip

The table above is right about the period and wrong about the chip. The two
`out` instructions sit **outside** the period loop, so a chip costs
`2 + periods × period` cycles — 36 002, not 36 000.

Two cycles is 56 ppm of chip rate, it has the same sign every chip, and it
therefore **walks the chip clock down the frame** rather than scattering. That
is the accumulating error `pio_carrier.pio` has warned about since M3 and
precisely what check 2c exists to catch, so it was not left in.

**The shipped program writes the first period out longhand** and spends the two
`out` cycles inside that period's own halves — its high half is
`set`, `out isr`, `mov`, loop, and its low half is `set`, `mov`, loop with no
`jmp y--`. That period then costs the same `2·isr + 8` as every other one, and
a chip is exactly 36 000 cycles. The cost is six duplicated instructions: 15 of
the 32 words of PIO instruction memory, against 20 for all three programs.

| | brief's 9 | shipped 15 |
|---|---|---|
| cycles a chip | 36 002 | **36 000** |
| duty over a chip | 49.9972 % | **exactly 50 %** |
| `isr` | 396 / 356 | 396 / 356, unchanged |
| `y` | periods − 1 = 44 / 49 | **periods − 2 = 43 / 48** |
| word | `0x002C_018C` / `0x0031_0164` | **`0x002B_018C` / `0x0030_0164`** |

`y` drops by one because **two** periods now leave the loop: the one carrying
the `out`s, and the fall-through into the loop head.

Where the slack lives, so a pad reading is not a surprise: every HIGH half is
exactly the tone half-period, and the low halves at the chip boundary are one
cycle short and one cycle long. They cancel inside the chip, so the even
harmonics are still null. `firmware/test/host/test_fsk.c` walks the program
instruction by instruction and checks all of it, including that dropping the
`[1]` would break the duty cycle.

Buffer: 624 chips × 4 bytes = **2496 bytes**, down from 15.6 kB.

`out pindirs` leaves the stream entirely. There is no per-chip release any
more, so `pio_carrier_drive()` at frame level is the only direction control.

### Verify it — there is no scope on this bench and none is needed

The board has measured itself since M3. `pio_carrier_measure_hz()` runs a
second state machine counting edges on the pad, independent of whatever drives
it. `linktest`'s `x` command loops our own carrier into our own receiver.

| | Check | Passes when |
|---|---|---|
| 2a | drive each tone continuously, read `measure_hz` | 180000 and 200000, within 0.1 % |
| 2b | self loop, alternating chips, watch the bin bank | bins 9 and 10 alternate cleanly, guards near zero |
| 2c | self loop, a known 624-chip pattern | no decode errors piling up toward the end of the frame |

2b proves four things at once: both tones exist, both sit on bin centres, they
switch on chip edges rather than a few cycles late, and the receiver separates
them. A scope would show only the first.

2c is the specific hazard the old `.pio` comment warns about — a cycle-count
imbalance shifts every chip after the first, so errors accumulate down the
frame rather than scattering.

**Owes:** a host test that builds the chip words and checks both come to 36 000
cycles, and that each period is an even number.

### What step 2 actually measured, 25 Sep 2026 (`baa2782`)

**2a PASSED**, both boards, with a duty reading beside it:

| | nominal | measured | error | duty at the pad |
|---|---|---|---|---|
| tone A | 180000 Hz | 180005 Hz | 28 ppm | **50.0008 %** |
| tone B | 200000 Hz | 200000 Hz | 0 ppm | **50.0000 %** |

**2b and 2c cannot be read through the self loop, and that is a fact about the
bench, not about the radio.** Driving tone A and walking the bins reads guard
bin 7 at **390 LSB** against tone A's 809 — the reading §5 calls the kill
switch. It is not the generator:

- the duty at the pad is 50.0008 %, measured with `pio_carrier_duty_ppm()`
- the raw operating point is **mean code 3564 of 4095 at 643 LSB RMS**, so the
  receiver is railed on its own transmitter
- **the v1 generator through the same loop rails harder** — mean code 3638, and
  its own second-harmonic bin peaks at 431 against its bin's 493

So the board has always behaved this way listening to itself, and it is what
the ~65 dB of a body path exists to avoid. A guard-bin reading is only
meaningful at a linear level, which means two boards and a real path.

**2c was therefore done at the pad by edge count instead, and it proves more
than a decode through a railed ADC could.** Each chip is a whole number of tone
periods and one period is one rising edge, so the count across a chip pattern
is exact arithmetic. 12 counts over three runs on 93D1 and 4 on 379E, every
one EXACT:

| pattern | chips | edges |
|---|---|---|
| all tone A | 624 | 28080 |
| all tone B | 624 | 31200 |
| alternating A/B | 624 | 29640 |
| a real encoded frame | 624 | 29640 |

**"Do the two bins separate cleanly" moves to step 4**, where the level is
linear. Step 3 is unblocked: the transmitter is proven, at the pad, to the
cycle.

New instruments, all reading rather than doing:

| | |
|---|---|
| `pio_carrier_duty_ppm()` | a third state machine counting cycles the pad is high. `measure_hz` says the period is right; this says its two halves are equal — and only the second can tell a generator that slipped from an amplifier that distorted. It is what settled this step. |
| `hal_pico_set_rx_bin()` | retune core 1's Goertzel alone, transmitter left where it is. `set_carrier` refuses the guards — 140, 160 and 220 kHz are not PIO dividers. |
| `linktest y` / `y 2` | the 2a check with the duty, and the alignment count |
| `linktest k` / `b` / `b 1` | move the receive bin; walk the five design bins, or every bin below Nyquist for the whole comb |

Regression after the change, both boards: the v1 link gave 155 good, 0 crc,
0 lost, FER 0.0000, margin 498 (step 1 recorded 488); `apps/handoff` completed
**8 handshakes each in 22 s**, 0 partial, 0 abort, cards both ways, 0 bad
frames, 0 stalls, 0 overruns. Suite 25315 checks, 0 failures.

---

## 4. Step 3 — the Goertzel bank

Five bins: 7, 8, 9, 10, 11 — 140/160/180/200/220 kHz.
Tones are 9 and 10. Guards are 7, 8 and 11.

### Two things that must change in the DSP

**`gz_isqrt64` comes off the hot path.** Every decision in v2 is a ratio, and a
ratio can be taken on `mag²` by integer cross-multiplication. Five square roots
at 20 kHz each is work we do not need to do. Keep `isqrt` for telemetry only.

**Guards can be decimated.** They are noise estimates and they are time-averaged
anyway (§6). Running them every 4th window costs a quarter and loses nothing.

| | Multiplies per sample |
|---|---|
| two tones, full rate | 2 |
| three guards, ÷4 | 0.75 |
| **total** | **~2.75** |

### The budget concern, stated honestly

sys_clk drops 150 → 144 MHz, so core 1 has **4 % less headroom** at the same
moment we multiply its DSP work. Small, but the wrong direction, and the bank is
what decides whether it fits.

**Passes when:** core 1 reports no dropped windows at 500 ksps, with the busy
figure printed. `hal_pico.c` already keeps `s_busy_us`; use it.

**If it does not fit**, in order: decimate guards harder; drop to two guards
(8 and 11, the tightest bracket); last, reconsider the window length.

**Owes:** host tests for the bank against synthesised two-tone input, including
a case where one guard bin has an interferer in it and the median must ignore
it.

### What step 3 actually measured, 25 Sep 2026

**PASSED.** The bank runs at 500 ksps with nothing dropped, and the budget is
a within-image number rather than a comparison of two builds.

`dsp/gz_bank.c` is the five bins. It runs **beside** the v1 chain on core 1
and starts switched off, which is what makes it costable: `linktest n`
switches it off, measures, switches it on, measures again, in one image on one
board inside ten seconds.

| 93D1, idle, one image | load | cycles/sample | overruns | sps |
|---|---|---|---|---|
| bank off | 31 % | 91.9 | 0 | 499876 |
| bank on | 47 % | **137.9** | 0 | 499863 |

**The bank costs 46.0 cycles a sample — 16 points of core 1.** 2.75 filter
windows a sample at 16.6 cycles each, which is the §4 budget arriving where it
was predicted to. Reproduced four times: 45.7 idle, 46.3 with the self loop
decoding, 45.8 on 379E, 46.0 on the image that shipped.

**The first cut cost 155 cycles a sample and 84 % of core 1.** Same
arithmetic, to the bit — the difference is entirely shape. A per-sample
`gzb_push()` reloads five filters from flash and makes five calls for every
sample; `gzb_push_run()` takes a run up to the window boundary and keeps each
filter in registers across it. Nothing was tuned and nothing was dropped: the
decimation is still 4, the guards are still three.

| | cycles/sample | core 1 |
|---|---|---|
| a call a bin a sample | 242.6 total, 155 for the bank | 84 % |
| a run to the window boundary | 137.9 total, **46** for the bank | **47 %** |

**The five bins in ONE window, which is the whole point.** `b` walks the bins
one at a time and cannot answer a question about a ratio; `n 2` reads all five
in the same window, same gain, same amplifier:

| generator | bin 9 A | bin 10 B | g 7 | g 8 | g 11 | median | signal |
|---|---|---|---|---|---|---|---|
| quiet, both boards silent | 17 | 18 | 29 | 24 | 19 | 23 | 18 |
| tone A driven, self loop | **814** | 2 | **391** | 2 | 23 | **23** | 814 |

The quiet row is the noise reference behaving: five bins inside their own
spread, and the signal *below* the median. Nothing to climb.

The driven row is step 2's self-loop rail, now read in one pass — bin 7 at 391
is the aliased second harmonic of a receiver saturated on its own
transmitter, not a generator fault (duty at the pad is 50.0008 %). **And it is
the median's case, live: median(2, 23, 391) = 23. The mean would have been
138.** One wrecked guard cannot deafen the receiver. That is CFAR earning its
place on hardware rather than in a comment.

**Regression, both boards, with the bank ON on the receiver:**

| | |
|---|---|
| v1 link, bank on | good 166, crc 0, lost 0, **FER 0.0000**, margin 490, **overruns 0**, load 44 % |
| v1 link, bank off | good 94 in 17 s, FER 0.0000, margin 488, overruns 0 |
| `apps/handoff` | **12 handshakes each in 30 s**, 0 partial, 0 stalls, **0 overruns**, load 38 % |
| suite | **25483 checks**, 0 failures (was 25315) |

The link decoding *with the bank running* is the real "no dropped windows"
test, and it is clean. Two one-off events are carried from the start of the
session and did not recur across 100 s of handshakes: one abort on 93D1 at its
first rendezvous, and one bad frame on 379E.

**What this does not say.** 47 % is the bank *plus* the v1 chain it is going
to replace. v1's Goertzel and `carrier.c` come out at steps 5 and 6, so the
steady-state v2 figure will be lower — but that is a projection, not a
reading, and it stays a projection until those steps take the code out.
`__not_in_flash_func` was **not** used: it would change what does, not what
observes, and the bank fits without it.

New instruments, all in `linktest`:

| | |
|---|---|
| `n` | the budget: bank off, then on, in one image, with cycles a sample |
| `n 0` / `n 1` | bank off / on and leave it there |
| `n 2` | one capture — five bins in the same windows, the guard median, the ratio |
| `hal_pico_core1_busy()` | busy and wall clock read together, so a caller can measure an interval instead of everything since boot |
| `hal_pico_bank_capture()` | core 1 fills N windows, then core 0 reads — nothing torn, no 64-bit race |

---

## 5. Step 4 — the gate. Do not build past this on faith.

Two boards, passive, on a wire. One transmits, one receives.

| Measure | Why it matters |
|---|---|
| `E_A` and `E_B` separation | the whole design rests on the bins being orthogonal in practice, not just in theory |
| the 180/200 imbalance | expected ~10 %; if it is wild or unstable, the preamble normalisation of §7 will not save it |
| guard bins with nothing transmitting | must read as noise, and the three must agree within their own spread |
| guard bins **while** transmitting | must **not** rise. If they do, the transmitter is leaking outside its bins and the noise reference is poisoned |

That last row is the kill switch. A contaminated guard bin is v1's floor with
extra steps.

**Passes when:** all four are in the transcript with numbers.

**Board ids:** 93D1 = `4904EF1FFA2393D1`, 379E = `36B9A3C84974379E`. Identify
by label, never by COM port — they move.

### What step 4 actually measured, 25 Sep 2026

**PASSED**, all four measures, and **not on a wire** — the bench had no wire to
give, so the two boards were coupled plate to plate through air. That is closer
to the path the product uses than a wire is, and it made one thing much harder
and one thing much easier.

#### The two things the bench taught before any of it was readable

**At contact the receiver saturates, and a railed capture cannot answer any of
the four questions.** Plate to plate the mean code walks 2309 → 3624, the
excursion leaves the converter, and guard bin 7 reads 369 against tone A's 729.
That is design §10's "a guard reading is only meaningful at a linear level",
arriving on two boards instead of through the self loop. The comb settles what
makes it, and the reasoning is in design §10 correction 2: every raised bin is
an even harmonic, the duty at the pad is 50.0000 %, so the receiver made them.

**A hand near a band is worth more than the transmitter.** Moving the boards by
hand put the raw RMS at 1100–1240 LSB with the tone bins only at ~300 — a
broadband pedestal that swamped every reading and made the guards read as high
as the signal. Readings must be hands-off and settled. Three runs were spent
before this was understood, and one of them was spent measuring an unchanged
bench because nobody was at the table.

**So the level was swept by closing the gap in steps**, hands withdrawn between
steps, alternating silence and tones so every hold gave a matched pair. That is
the shape any future level sweep on this bench wants: there is no gain knob and
no attenuator, so geometry is the only level control, and a hand in the loop is
a second transmitter.

#### The gate

Ten matched quiet/tone rounds, **every capture linear** (mean code 2305–2317,
never walking), signal swept 64 → 716 LSB:

| signal | quiet median | tone A median | tone B median | alt median |
|---|---|---|---|---|
| 64 | 46 | 43 | 45 | 45 |
| 115 | 67 | 84 | 96 | 91 |
| 231 | 83 | 81 | 80 | 77 |
| 297 | 77 | 77 | 78 | 87 |
| 410 | 80 | 78 | 79 | 85 |
| 479 | 76 | 76 | 77 | 78 |
| 716 | 79 | 76 | **74** | 71 |

**Guards while transmitting: they do not rise.** An eleven-fold sweep of the
signal, and the guard median stays on the ambient — it even drifts down as the
hands recede. This is the kill switch, and it is the row the whole branch was
gated on.

**`E_A` / `E_B` separation:** each tone raises its own bin and nothing else.
At the loudest point, tone B reads 716 on bin 10 while bin 9 reads 62 against
67 silent; tone A reads 549 on bin 9 while bin 10 reads 57 against 60 silent.
The off-tone bin is **indistinguishable from silence** — orthogonality in
practice, not just in the transform.

**The 180/200 imbalance: tone A is 9.1 % stronger**, ±0.5 % across a 2.3×
level range (1.10, 1.10, 1.09, 1.09 over four rounds, pedestal subtracted in
mag²). Small and stable, which is what §8's preamble normalisation needed. The
sign is opposite to what design §8 predicted — see design §10 correction 1.

This number is only readable because of `y 3`. Two continuous captures four
seconds apart, on a bench whose coupling was drifting, disagreed by 24 % about
the same imbalance; the alternating pattern puts both bins in the **same**
windows and holds 9.1 % across the whole sweep.

**Guards with nothing transmitting:** 116 / 88 / 52 LSB on bins 7 / 8 / 11.
They do *not* agree within a spread — they sit on a stable 2.3:1 slope falling
with frequency, so `median` always selects bin 8. Design §10 correction 3.

**CFAR margin** at the strongest linear point: 94 in power for tone B, 52 for
tone A, against the `k ≈ 10` §6 derives for ~60 reference cells. Seven to ten
dB, on a plate path. Step 8 is where a skin path gets to argue with that.

#### New instruments, both reading rather than doing

| | |
|---|---|
| `y 3` | tone A and tone B on alternate chips, unbroken, from the same 32-byte ring. The only way to ask an imbalance question — one capture, both bins, same windows, same gain, same coupling. |
| the operating point under `n 2` | raw RMS, mean code, the sinusoid excursion against the converter's own 0..4095, and a verdict. A guard reading means nothing at a railed level and nothing else on the console said which you had. The mean code walking is the tell that assumes no crest factor at all. |

`y 0` / `y 1` now take the pad explicitly and print whether they got it. `y 2`
hands it back when it finishes, so a `y 0` after an alignment check drove a
high-Z pad — and a far board then reads a quiet room, which is a reading nothing
in the transcript could tell apart from a dead coupling path.

#### Regression at this coupling

| | |
|---|---|
| v1 link, bank ON | good 104, crc 0, lost 0, **FER 0.0000**, BER 0, margin 1412, overruns 0 |
| v1 link, bank off | good 49, FER 0.0000, margin 1415 |
| `apps/handoff` | **12 complete handshakes each in 33 s**, 0 partial, 0 stalls, 0 overruns |
| suite | **25483 checks**, 0 failures |

Margin 1412 against the wire bench's 490 — the plate path at this gap is much
louder than the wire was. **Core-1 load reads 48–49 % in `handoff` against the
38 % baseline and 55 % in `linktest` against 44 %.** Overruns are 0 and more
frames are being decoded here, but it is not explained and it is not attributed
to anything. Watch it at step 5.

The one carried abort on 93D1's first rendezvous is still there and still one.

---

## 6. Step 5 — presence, and the death of `carrier.c`

```
signal = max(E_A, E_B)                    /* mag², no sqrt */
noise  = median(E_140, E_160, E_220)      /* time-averaged */
busy   = signal > k × noise
```

**Averaging the guards is safe in a way v1's floor never was, because the
signal can never enter those bins.** That is the whole lesson from the floor
bug — the mistake was never averaging, it was averaging a channel the signal
could reach. Write that in the header, because someone will otherwise "fix" it
back.

**`k` is computed, not chosen.** It comes from a stated false-busy rate. More
reference cells means a smaller `k` means a more sensitive detector:

| Reference cells | `k` for 1-in-10⁴ |
|---|---|
| 3 | ~62 |
| 12 | ~14 |
| 60 | ~10 |

Three bins × twenty windows of averaging ≈ 60 cells. Put the derivation in the
source next to the constant, with its working, so it can be re-derived when the
rate target changes.

**Delete `firmware/lib/dsp/carrier.c` and `carrier.h`.** Not deprecate — delete.
Rewire the callers in `link_sm.c` (§7 below). `test_beacon.c`'s floor tests go
with it; its trigger tests do not.

**Passes when:** `busy` tracks reality with the amplifier gain swept across its
range, tethered. The 379E state from `carrier-floor-still-climbing-brief.md` —
peak 133–150, floor 61–83, deaf — must be unreproducible, because there is
nothing left to climb.

### What step 5 actually measured, 25 Sep 2026 (`1fd27d0`)

**PASSED.** `busy` tracks the level, the crossover lands on the derived `k`,
and both regressions are clean. `carrier.c` is deleted.

#### The sweep

Two boards plate to plate, level swept by geometry with hands withdrawn
between holds, a matched quiet/tone pair at every one, every capture linear.
`linktest p [s]` reports the FRACTION of windows that read busy, which is what
a threshold has to be judged as — one instantaneous read cannot tell 100 %
busy from 5 %.

| gap | quiet | tone A | power ratio | against `k` = 16.76 |
|---|---|---|---|---|
| 1 m | 0 % | **0 %** | 1:1 | out of range |
| 30 cm | 0 % | **0 %** | 2:1 | out of range |
| **15 cm** | 0 % | **58.5 %** | 12:1 | **ON THE LINE** |
| 10 cm | 0 % | **100 %** | 77:1 | 4x clear |
| 5 cm | 0 % | **100 %** | 651:1 | 38x clear |

**The 58.5 % row is the one to keep.** At that hold the instantaneous ratio is
12:1 against a threshold of 16.76, so half the windows fall over the line and
half under it — the detector sitting exactly on its own threshold. `k` was
computed from a stated false-busy rate and never touched; the crossover landed
there on its own.

Quiet, both silent and settled: **0 busy windows in 100024**, on both boards.
Tone A at 5 cm: 100024 of 100024, 1245:1 in power, 74x clear.

The 379E state from `carrier-floor-still-climbing-brief.md` — peak 133–150,
floor 61–83, deaf — is unreproducible, as §6 said it would be. There is
nothing to climb.

#### THE REAL FINDING: the core-1 loop was stalling on flash

`apps/handoff` went to **98 % core-1 load with 1709 DMA overruns in thirty
seconds** and not one handshake out of eight completing. `main` on the same
bench at the same gap: 13 complete, 0 abort, 35 % load, 0 overruns. So the
branch broke it, and the bench said so before any argument could.

**The instrument said the opposite of the obvious answer.** A live two-second
interval read **46 %**, not 98 %, while overruns kept arriving at 47 a second.
You do not drop DMA blocks at 46 % load — you drop them when something STALLS
core 1 rather than keeping it busy.

Core 1 runs from XIP, and `apps/handoff` has BTstack on core 0 using the same
flash. The bank's window end runs 20 000 times a second and every one of those
was a chance to stall. Average load was fine; worst-case latency was not.
`linktest` never showed it because core 0 there is an idle console.

So the whole core-1 path is in RAM now — the bank, presence, the v1 Goertzel,
`sync`, the ipc push and `core1_main` itself. `HANDOFF_HOT_FUNC` in `config.h`
spells out the section attribute rather than using the SDK's
`__not_in_flash_func`, because `.time_critical.*` is plain GCC and `lib/dsp`
must not see an SDK header. **Not one instruction of arithmetic changed; only
where it lives.**

§4 decided against this at step 3 on the grounds that the bank fits without
it. It fits without it in `linktest`. It does not fit without it beside a
Bluetooth stack, and that is a measurement step 3 could not have made.

#### Two mistakes the bench caught, both mine

1. **Two square roots per window.** The first `core1_presence_window()`
   computed both telemetry scores every window — two `isqrt64` and a 64-bit
   divide at 20 kHz, on a part with no 64-bit divider. Core 1 read 73 %
   against a 55 % baseline. Design §6 says no square roots on the hot path,
   and four expensive operations were sitting there for numbers nobody reads
   more than once a second. Core 0 now raises a flag and the next window
   answers it. `gzb_ratio_gt()`'s divide-based overflow guard came off the
   decision at the same time — `config.h` static-asserts why it cannot be
   needed there.

2. **The ratio was printed in amplitude next to a `k` in power.** A 19x margin
   read as though it were scraping past 16.76. Both consoles now print the
   power ratio and how many times clear it is.

#### The OOK bridge, and why it exists

`presence` has no hold, which is right for a radio whose two symbols are both
tones. It is wrong for the radio still on the air: **v1 switches the carrier
OFF for a zero**, so half of every frame is silence and an envelope detector
with no memory answers "nobody is transmitting" in each gap — truthfully.
Measured in the host simulator: a band listening to a v1 frame flapped
busy/quiet at the chip rate and **no rendezvous completed at any phase**.

So `link_sm.c` carries a bridge, and its length is derived rather than tuned:
`MANCHESTER_MAX_RUN_CHIPS + 1` chips — the longest silence the line code can
produce, plus one because `hal_rx_busy()` answers about an interval rather
than an instant. `manchester.h` states the run length and `test_manchester.c`
walks all 65 536 bit pairs to prove it.

**It deletes at step 6.** FSK has no spaces, so nothing will exercise it once
the framer changes.

#### `hal_rx_busy()` took two goes

Sticky-until-read alone flaps for a caller polling faster than windows close.
The last window's verdict alone misses a short burst. It is both, and `hal.h`
says why each half is there — each one was a bug without the other, and each
showed up as a different failure.

#### Two things the simulator had to be told

Both honest gaps, not fudges. The CFAR reference is empty for one preamble
after init, so `sim_twonode` now runs the channel before either band is armed
— a band on a wrist filled that in the first eight milliseconds after boot and
has held it since. And the host HAL runs the **real** `presence_t` off a
two-bin model of the channel, so the real `k`, the real reference length and
the real decimation are in the loop.

A VSYS read no longer throws the reference away: the converter being borrowed
for three blocks says nothing about the room.

#### Regression at this coupling

| | |
|---|---|
| v1 link | good 200, crc 0, lost 0, **FER 0.0000**, BER 0, margin 437, overruns 0, false syncs 1 |
| `apps/handoff` | **12 and 13 complete handshakes in 35 and 36 s**, 0 partial, 0 abort, 0 stalls, 0 overruns |
| core 1 | bank off 85.6 cycles/sample, bank on 132.4 — bank AND presence together cost **46.8**, against step 3's 46.0 for the bank alone. **Presence is under one cycle a sample.** |
| load | 46–47 % in `linktest`, 47 % in `handoff` against `main`'s 35 % |
| suite | **25536 checks**, 0 failures |

#### What is NOT built on this branch

`apps/turnaround` is off the build. It measures v1's released-space
turnaround, which design §4 retires, and it does it with its own `carrier_t`
over chips sorted by sample number — which v2's core-1 bank does not hand core
0. The source stays; it still builds on `main`.

New instruments:

| | |
|---|---|
| `linktest p [s]` | the busy FRACTION over an interval, with signal, reference, the power ratio and the operating point. Does not go through `hal_rx_busy()` — that clears the latch, and an instrument must never take an event away from the link. |
| `handoff n [0\|1]` | the bank off or on, in THIS image on THIS board. It is what split the bank's cost from the rest of the diff, inside the app where the problem actually was. |
| `hal_pico_presence()` | the detector read without disturbing it |

---

## 7. Step 6 — the framer, without a slicer

`frame.c`'s adaptive slicer (`hi`, `lo`, `primed`, `step_toward`) exists only
because the preamble hunt needs hard 1/0 decisions from a single energy stream.
With two tones a chip is `E_A > E_B` and there is nothing to slice. **Delete
all four.**

`FRAME_ALT_WINDOW` 24 / `FRAME_ALT_MIN` 22 stay, but promote them from typed to
computed: the derivation from "one false sync per 24 hours" and the chip rate
is already written in `frame.h`, it just needs to be arithmetic.

**Manchester stays.** FSK retires its threshold job but not its timing job —
see design §7. Do not drop it as a throughput optimisation.

**The imbalance correction, and the rule applied to itself.** Tone A arrives
about 10 % weaker than tone B. **Do not correct it with a constant.** The preamble
alternates the two tones, so it measures the imbalance on the link as it is at
that moment — carry that ratio into the body decisions. A per-board calibration
number would be a tuned constant wearing a different hat, and it would mean the
design has failed its own rule.

**Callers to rewire in `link_sm.c`:**

| Site | Was | Becomes |
|---|---|---|
| `LINK_RX_FRAME` turn-keepalive | `carrier_present() \|\| frame_rx_busy()` | `frame_rx_busy()` alone |
| `poll_idle` trigger input | `listening && carrier_present()` | the beacon decoder, §8 |
| `trig_take_carrier_reprime` | reprimes the floor | **delete — there is no floor** |
| `carrier_reset` on state change | 4 sites | delete |

**Passes when:** BER through the self loop and over the wire is at least as
good as v1 on the same bench. Not "close to" — at least as good.

### What step 6 actually measured, 25 Sep 2026

**PASSED.** The slicer is deleted, the hunt window is computed, the OOK bridge
is gone, and the radio is FSK end to end — transmitter, receiver and framer.
BER is at least as good as v1 at every gap measured and strictly better at two
of three, and `handoff` completes the same number of handshakes for **fourteen
fewer points of core 1**.

#### The link, three gaps, both radios, one session

Two boards on the PCB plate path, `linktest_tx` on 93D1 and `linktest` on
379E. At each gap: measure v2, flash `main` to both boards, measure v1 —
nothing moved between the two halves of a row.

| gap | v2 FER | v2 BER | v2 margin | v2 lost | v1 FER | v1 BER | v1 margin | v1 lost |
|---|---|---|---|---|---|---|---|---|
| 5 cm | **0.0000** | **0** | 667 | 0 | 0.0000 | 0 | 437 | 0 |
| 10–12 cm | **0.0000** | **0** | 247 | **0** | 0.0025 | 0 | 153 | 1 |
| 25 cm | **0.9683** | **0.374** | 48 | — | 1.0000 | 0.485 | 15 | — |

**5 cm proves nothing and is in the table anyway**, because a row where both
radios read zero is the row that says the bench was not the thing being
measured. 10–12 cm is the first place they differ; 25 cm is past both knees,
and v2 still got **11 frames across where v1 got none**.

The margin column is not directly comparable between the two and the factor is
known rather than guessed: v2's margin is `sqrt(S_A^2 + S_B^2)` because every
Manchester bit holds one chip of each tone, so it carries a `sqrt(2)` v1 does
not. 667/437 is 1.53 against that 1.41 — the rest is the link being better.

#### The simulator says the same thing, and it says it about the waterfall

`scripts/test.py --ber`, identical frames, seeds and SNR definition, v1 run
from a `main` worktree:

| SNR | v2 good | v2 FER | v2 BER | v1 good | v1 FER | v1 BER |
|---|---|---|---|---|---|---|
| −6 dB | **30** | 0.5000 | **2.73e-02** | 0 | 1.0000 | 2.82e-01 |
| −4 dB | **59** | **0.0167** | **6.51e-05** | 23 | 0.6167 | 8.52e-02 |
| −2 dB | 60 | 0.0000 | 0 | 60 | 0.0000 | 0 |

About 2 dB, which is what a constant envelope against OOK should buy.

#### `apps/handoff`, and this is the step-5 lesson being obeyed

Step 5's near-disaster — 98 % core-1 load, 1709 DMA overruns, zero handshakes
— existed **only** in the app that carries BTstack, and `linktest` could not
see it. So it is measured here, in that app, and not inferred:

| 42 s at 5 cm | complete | partial | abort | overruns | stalls | core-1 |
|---|---|---|---|---|---|---|
| **v2** | **15** | 0 | 0 | 0 | 0 | **21 %** |
| v1, `main` | 15 | 0 | 0 | 0 | 0 | 35 % |

Same throughput, 14 points cheaper. Handshakes complete in **949 ms as the
faster role and 1133 ms as the slower**, which is design §2's one-second
requirement met on one of the two roles and just missed on the other.

**Core 1 is at 21 %, against 46–47 % at step 5.** Nothing was optimised; two
things were deleted. The v1 Goertzel and its symbol sync came out of the link,
and the retunable Goertzel that is left runs only while a console command is
looking at it. Within one image: **bank off 7.3 cycles a sample, bank on
59.9 — the whole v2 receiver costs 52.6**, against step 5's 46.8 for the bank
and presence sitting beside a v1 chain that is now gone.

#### Presence reads 88.6 %, and that number is the OOK bridge's obituary

`p` on the receiver, with frames flowing: **53227 busy windows of 60046**.
The transmitter sends a 156 ms frame every 176 ms. 156/176 is 88.6 %.

The detector is busy for exactly the airtime and for nothing else — no hold,
no hysteresis, no bridge. That is the whole reason `LINK_OOK_BRIDGE_US` could
be deleted rather than shortened: v1 switched the carrier off for a zero, so
presence flapped at the chip rate inside every frame and a detector with no
memory answered "nobody is transmitting" in each gap, truthfully. FSK has no
gaps. Design §4's second "free" benefit of a constant envelope, arriving as a
deletion.

#### The imbalance: measured again, and still not corrected

`n 2`, both tone bins in the same windows with frames flowing: **bin 9 mean
327 LSB, bin 10 mean 301** — tone A **8.6 %** stronger, against step 4's
9.1 % from `y 3`. Guards flat at 21 / 46 / 33, median 34. Capture linear.

Design §8 expected to carry that imbalance from the preamble into the body
decisions. **Nothing corrects it, and that is a derivation rather than a
shortcut.** Every Manchester bit holds one chip of each tone, so the
difference of the two halves is ±(S_A + S_B) whatever the two strengths are —
symmetric by construction. `test_frame.c` sweeps the imbalance to **3:1, thirty
times worse than this hardware, in both directions**, and no decision changes.
frame.h has the working.

The framer measures it anyway and reports it in `last_imbalance_pct`, off the
preamble of every frame it syncs to. Reported, never fed back — and unlike
`y 3` it reads the far board mid-handshake rather than needing a board
dedicated to driving a pattern.

#### What the window came out at, and what it replaced

`FRAME_ALT_WINDOW` / `FRAME_ALT_MIN` were 24 and 22, typed, and frame.h's own
comment admitted what they bought: "one spurious sync every couple of hours".
Nobody had chosen that. They are now computed from `HANDOFF_FALSE_SYNC_S` —
one false sync per 24 hours — and come out at **29 and 27, one every 43.8
hours**, with the tolerance for one flipped chip anywhere in the window kept
as the structural reason counting exists at all.

Both ends are asserted: 29 must meet the rate, and **28 must fail it**, so the
window is the smallest that satisfies the requirement rather than the tightest
someone felt like. `test_frame.c` recomputes the whole thing in double, the
same contract `test_presence.c` gives the CFAR k.

#### Three things that had to move, and none of them is the radio

**The v1 captures cannot be decoded by a v2 receiver.** There are two, from
the M5 bench, and one of them is the frame the board lost when a transient
froze its slicer. They are OOK, and there is no slicer in the firmware any
more. The slicer moved to `firmware/test/host/v1_replay.c` — the replay
harness, not lib/ — and the captures still replay on every run. What they test
now is the framing, which step 6 did not touch; what they can no longer test
is the chip decision, which is correct, because the decision they were
recorded to exercise does not exist.

**`tools/gen_vectors.py` renders two tones.** It is the only test that reads
the spec independently of the C, so it had to follow the spec. The two
implementations still agree to the LSB with the tone switching every chip, and
that is arithmetic rather than luck: a chip is a whole number of periods of
either tone, so at a chip boundary an integrated phase and a `sin(2*pi*f*t)`
are both at zero.

**The retunable Goertzel became a console probe.** `m`, `k`, `b` and `b 1`
used to read the link's own chip stream, which is why walking the bins cost
frames. They now read a Goertzel that runs only while a capture is
outstanding, reports a mean and a max over an interval, and has nothing
framing behind it. Retuning costs the receiver nothing — it is not on that
bin.

#### One bug found, in code this step did not write

`firmware/test/host/decode.c` had a literal newline inside a `printf` format
string: the file had not compiled since whenever that happened. `handoff_decode`
is only built for a replay, so nothing noticed. Fixed in passing, and it is
exactly the hazard §11's tooling note warns about.

#### Regression

| | |
|---|---|
| suite | **25567 checks**, 0 failures (was 25536) |
| v2 link at 5 cm | good 400, crc 0, lost 0, FER 0.0000, BER 0, margin 667, overruns 0 |
| `apps/handoff` | 15 complete in 42 s each, 0 partial, 0 abort, 0 stalls, 0 overruns |
| core 1 | 21 % in `handoff`, against `main`'s 35 % and step 5's 47 % |

New instruments:

| | |
|---|---|
| `hal_pico_probe()` | the retunable bin, mean and max over N windows, off unless someone is looking |
| `hal_pico_samples()` | ADC samples core 1 took. The denominator of a cycles-per-sample budget, and unlike the window count it survives the bank being switched off — which is one of `n`'s two legs |
| `frame_rx_t.last_imbalance_pct` | the 180/200 imbalance off every synced preamble, reported and never fed back |
| `handoff_decode --v1` | replay an OOK capture through the slicer the firmware no longer has |


---

## 8. Step 7 — rendezvous by nonce

Replace the flat-tone shout with a beacon **frame**.

| Field | Bits |
|---|---|
| preamble | 16 chips |
| marker | 8 |
| nonce | 16 |
| flags | 8 |
| CRC-16 | 16 |

About 30 ms at 4000 chips/s.

| Question | v1 | v2 |
|---|---|---|
| is that a peer? | tone longer than `SHOUT_MIN_US` | the CRC passed |
| is that my own echo? | sample-time cut, and hope | the nonce is mine |
| who sends? | who heard whose tone first | higher nonce |

**This kills four constants and one whole bug class.** `SHOUT_US`,
`SHOUT_MIN_US`, `LISTEN_MIN_US`, `LISTEN_MAX_US`, `QUIET_WAIT_MAX_US` all go.
The "own shout heard" bug from 24 Sep (`handoff-elects-two-senders`) becomes
unwritable, because identity is in the payload rather than inferred from timing.

`TRIG_SETTLE_US` stays but becomes physical: the AFE's high-pass RC, measured,
not 6000 µs because 6000 worked.

**Beacon period** is derived from design §2's one-second handshake requirement
and the beacon's own airtime. Show the collision-probability working.

**Passes when:** two boards, 15 minutes, no double-send, no self-trigger, and
every rendezvous accounted for in the counters.

**Owes:** host tests for nonce tie (both draw the same — must redraw), and for
a beacon arriving with its CRC damaged (must be ignored, not acted on).

---

## 9. Step 8 — skin, on cells

Everything above is USB-tethered and **design §13 says tethered readings are
wrong as well as unsafe** — both boards share the PC ground and that wire is
the return path under test. Nothing is proven until this step.

- Both boards floating, on cells. TX gated at SW1.
- Plug USB back in **without touching SW1** to read the counters.
- Keep the bands apart when not deliberately in contact — §14.1's table result
  was 0 frames at the same spacing, which is the proof the coupling is real.

**Passes when:** the 447-frame worn result from 24 Sep is matched or beaten,
and the ~30 % FER is no worse.

---

## 10. What this branch does not change

**Behaviour** of: record store, `carousel.c`, fragment reassembly, the phone
app, ownership, the wearer UI, the enclosure.

**Instruments** in those files are fair game — see §1. Adding a counter to the
carousel is fine; changing which fragment it picks is not.

---

## 11. Bench recipes

**Build and flash one of two attached boards:**

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target handoff
picotool load -x build-pcb/handoff.uf2 --ser <serial> -f
```

`-x` goes **before** the filename, device selection **after**.
`scripts/build.py --flash` cannot target one of two attached boards.

**Start the serial logger before flashing, not after** — the first lines after
reset are the ones you want, and a stale `console.py` holding the port is the
usual reason a board looks dead.

**Bench order: `linktest` first, then `handoff`.** A fault in the DSP looks
exactly like a fault in the state machine from the `handoff` console, and
`linktest`'s `x` self loop takes the second board out of the question entirely.

**Watch for the self-loop pump trap** — `x` re-queues its carrier a frame at a
time, so any wait loop that does not call `selfloop_pump()` stalls the carrier
and makes `m` read zero. It has cost a session before.

**Never name TP numbers.** The silk has no TP labels. Name the signal or the
part end.

**Tooling note:** the Bash wrapper in this harness breaks on apostrophes and
mangles backslashes in heredocs. Use `Write` plus a python script.

---

## 12. What would kill this, and what to do

| Symptom | Step | Response |
|---|---|---|
| ADC rate moves with sys_clk | 1 | stop — re-derive the clock; 126 MHz is the fallback that also divides both tones |
| core 1 drops windows | 3 | decimate guards, then drop to one guard, then reconsider the pair |
| guard bins rise while transmitting | 2 or 4 | check duty first — bins 7/11 are the even-harmonic monitor. If duty is exact and they still rise, **stop**: the reference is poisoned and this is v1's floor again |
| the weaker tone is too weak through skin | 4 or 8 | adjacent bins is already the minimum-imbalance choice; if it still fails, the idea is dead |
| BER worse than v1 | 6 | stop and report — the design has not earned the rewrite |
| RP2350 errata in a 5-bin config | 3 | the ring already carries E5 and E9 workarounds; check those first |

---

## 13. Where the rest of the story is

| File | What it holds |
|---|---|
| `docs/link-v2-design.md` | the design and the rule every constant is judged against |
| `docs/carrier-floor-still-climbing-brief.md` | the fault this replaces, and the three fixes that did not fix it |
| `docs/carrier-floor-brief.md` | the previous floor redesign, which fixed the opposite failure |
| `docs/body-coupled-handshake-design.md` | the physical layer. §5 link budget, §9.1 rejects FSK — see v2 §4 for why that rejection was wrong |
| `docs/firmware-architecture.md` | framing §8.3, transport §8.4 |
| commit `2de842e` on `main` | the v1 state this branched from, with its known issues in the message |
