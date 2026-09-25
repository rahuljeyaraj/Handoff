# Link v2 — where to pick this up

Steps 1 and 2 have passed. Start at step 3, the Goertzel bank.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 is
`baa2782`, and the commits after it are documentation only.

Read `docs/link-v2-design.md` end to end first, then `docs/link-v2-brief.md`.
The design is the authority; the brief is the build order. Both now carry a
"what step 2 actually measured" section — read those, they change step 4's job.
Do not re-read the v1 carrier-floor briefs unless something sends you there.

Also read the memory files `link-v2-redesign`, `never-say-tp-numbers`,
`bash-no-single-quotes` and `pico-bench-recipe`.

## THE RULE, WHICH IS THE WHOLE POINT

Every number in the link must be **physical, structural, derived, or computed
from a stated error rate**. Nothing is allowed in because it worked on a
bench. If you find yourself picking a value because it worked, stop and write
down what requirement it should come from instead. Design §1.

## WHAT IS SETTLED. DO NOT RE-ASK.

| | |
|---|---|
| sys_clk | **144 MHz. Done and on the boards.** |
| Tone pair | **180/200 kHz, bins 9 and 10.** Adjacent. Not open. |
| Guards | **bins 7, 8, 11** = 140/160/220 kHz. Clear of every odd harmonic. Not open. |
| Two-tone transmitter | **built, measured at the pad, exact.** Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |
| v1's carrier floor | not being fixed. `carrier.c` gets deleted, not tuned. |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

160/200 kHz was the **first** proposal and the design rejected it
(`link-v2-design.md:95`): the pair must be **adjacent** bins, because coupling
rises with frequency and one bin apart is the smallest imbalance the transform
allows (~1 dB). 160/200 would also have forced a 20 % clock cut instead of 4 %,
which core 1 cannot afford at step 3. So:

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

160 kHz is listened to and never transmitted. The user asked about this on
25 Sep 2026 — it is a natural place to misremember, so it is written down here
rather than re-derived.

## STEP 1 PASSED (3b4e902)

The board boots at 144 MHz — a boot clock, not a runtime switch. `clk_adc`
sits on PLL_USB by the hardware reset default and **did not move**: 48000 kHz,
so 500 ksps is untouched. That was the one thing that could have ended the
branch at step 1. The `f` console command prints the whole tree measured
against the crystal.

## STEP 2 PASSED (baa2782)

Measured on **both** boards, no scope:

| | nominal | measured | error | duty at the pad |
|---|---|---|---|---|
| tone A | 180000 Hz | 180005 Hz | 28 ppm | **50.0008 %** |
| tone B | 200000 Hz | 200000 Hz | 0 ppm | **50.0000 %** |

Chip alignment is **exact**. Each chip is a whole number of tone periods and
one period is one rising edge, so the edge count across a chip pattern is exact
arithmetic. 12 counts over three runs on 93D1, 4 on 379E, every one EXACT:
all-A 28080, all-B 31200, alternating 29640, a real encoded frame 29640.

### The brief's nine-instruction loop was two cycles long. It is fixed.

Its two `out`s sat outside the period loop, so a chip was 36002 cycles. Two
cycles is 56 ppm of chip rate with the same sign every chip, so it walks the
chip clock down the frame — exactly the accumulating error 2c exists to find.
The shipped program writes the first period out longhand and spends the two
`out` cycles inside that period's own halves. **15 instructions, chip exactly
36000 cycles, duty exactly 50 % over a chip.**

`isr` is unchanged at 396 / 356. **`y` is periods − 2**, not periods − 1,
because two periods leave the loop. So the words are `0x002B_018C` and
`0x0030_0164`. `firmware/test/host/test_fsk.c` walks the program instruction
by instruction and holds all of it.

### THE SELF LOOP SATURATES THE RECEIVER. This matters for step 4.

Driving tone A and walking the bins reads **guard bin 7 at 390 LSB against
tone A's 809** — which is the reading the brief calls the kill switch. It is
**not** the generator, and the proof is three-fold:

| | |
|---|---|
| duty at the pad | 50.0008 % — the generator is clean |
| raw operating point | **mean code 3564 of 4095 at 643 LSB RMS** — railed |
| the **v1** generator, same loop | rails harder: mean code 3638, and its own 2nd-harmonic bin peaks at 431 against its bin's 493 |

So the board has always done this listening to itself; it is what the ~65 dB of
a body path exists to avoid. **A guard-bin reading is only meaningful at a
linear level.** Do not try to pre-empt step 4 with a self-loop bin reading, and
do not read a self-loop guard rise as the kill switch — check the duty first.

## START AT STEP 3 — THE GOERTZEL BANK

Brief §4. Five bins: 7, 8, 9, 10, 11 (140/160/180/200/220 kHz). `gz_t` is
already per-bin, so the bank is an array; the work is the budget, not the maths.

Two things the brief requires:

- **`gz_isqrt64` comes off the hot path.** Every v2 decision is a ratio, and a
  ratio can be taken on `mag²` by integer cross-multiplication. Keep `isqrt`
  for telemetry only.
- **Guards can be decimated.** They are time-averaged noise estimates; every
  4th window costs a quarter and loses nothing. Budget ~2.75 multiplies a
  sample against v1's 1.

**Passes when:** core 1 reports no dropped windows at 500 ksps with the busy
figure printed. `hal_pico_core1_load()` and `hal_pico_overruns()` are there.

**Owes:** host tests for the bank against synthesised two-tone input, including
one where a single guard bin has an interferer and the median must ignore it.

**If it does not fit**, in order: decimate guards harder; drop to two guards
(8 and 11, the tightest bracket); last, reconsider the window length.

### THE BUDGET CANNOT BE SETTLED BY COMPARING TWO IMAGES

Carried from step 1, and it still stands. Same board, quiet bench: v1 code at
150 MHz **29 %**, v2 code at 150 MHz **33 %**, v2 code at 144 MHz **34 %**. The
clock cost the 4 % the design predicted; the other four points came from adding
one unrelated function and shifting the image in XIP.

So a 4-point swing from nothing would swamp the answer. **It needs a
within-image cycle count.** Build the instrument before you trust the number.
Pinning the block loop in RAM with `__not_in_flash_func` would change what
*does* — report it, do not slip it in.

For reference from this session's runs: core-1 load reads **32 %** idle and
**38–44 %** while frames are actually being decoded, on the same image. Compare
like with like.

## STEP 4 IS STILL THE GATE, AND NOW IT OWNS THE GUARD QUESTION

Two boards, passive, on a wire, at a **linear** level. If the guard bins rise
while transmitting *and the duty at the pad is 50 %*, stop and say so — the
noise reference is poisoned and that is v1's floor again. Brief §5.

`pio_carrier_duty_ppm()` is what separates the two cases. Use it before calling
a guard rise fatal.

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal (linktest, handoff) |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken; `y 9` hands the pad back to v1 |
| `y 2` | chip alignment by edge count, four patterns, exact arithmetic |
| `k [bin]` | move the receive Goertzel to any bin, transmitter untouched |
| `b` / `b 1` | walk the five design bins, or every bin below Nyquist |
| `pio_carrier_duty_ppm()` | the pad's duty, from a third state machine |
| `hal_pico_set_rx_bin()` | the `k` command's plumbing |

`y` and `k`/`b` are in **linktest** (both roles where it makes sense). Note
`handoff` already uses `y` for its GP15 sync pulse — do not collide.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port — they move. This session both were on
COM7 and COM8 respectively.

**Both boards boot linktest as the RECEIVER role** — the strap (GP14) is open
on the PCB. For a TX board use the `linktest_tx` target, which compiles the
role in. That is how the v1 regression below was run.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target handoff
picotool load -x build-pcb/handoff.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe` and is not on PATH.

`build-v1/` holds the 150 MHz baseline images. Keep them.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock and resolves serial tails to ports. It opens the port
once, up front, so it **misses a boot banner** — anything that prints only at
startup needs a logger that waits for the port to come back after the reboot.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

**Quiet the other board before any bin measurement.** A board left running
`apps/handoff` shouts on bin 10 every few seconds and will land in a reading.
Putting it on `linktest` (receiver role) makes it silent.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13).

## REGRESSION BASELINE, measured at baa2782

Reproduce these before and after anything in step 3.

| | |
|---|---|
| v1 link, two boards | good 155, crc 0, lost 0, FER 0.0000, **margin 498** |
| `apps/handoff` | **8 complete handshakes each in 22 s**, 0 partial, 0 abort, cards both ways, 0 bad frames, 0 stalls, 0 overruns |
| suite | **25315 checks**, 0 failures |
| core-1 load | 32 % idle, 38–44 % decoding |

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes —
  and on backticks, which silently ate a word of a source comment this session.
  Write a python script with the `Write` tool and run it.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25315 checks**. Keep it green.
- v1 still works on `main`. If this branch dies at step 4, that is a
  **successful outcome** — write up why and stop.
