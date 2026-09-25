# Link v2 — where to pick this up

Steps 1, 2 and 3 have passed. Start at step 4, the gate.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 is
`baa2782`, step 3 is `4394180`, and the commits after each are documentation
only.

Read `docs/link-v2-design.md` end to end first, then `docs/link-v2-brief.md`.
The design is the authority; the brief is the build order. Both carry a "what
step N actually measured" section — read those, they are where the plan was
wrong. Do not re-read the v1 carrier-floor briefs unless something sends you
there.

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
| The five-bin bank | **built, costed, running.** 46 cycles a sample. Not open. |
| Guard decimation | **every 4th window** — coprime with the 5 windows a chip, derived in `config.h`. Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |
| v1's carrier floor | not being fixed. `carrier.c` gets deleted, not tuned. |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

160/200 kHz was the **first** proposal and the design rejected it
(`link-v2-design.md:95`): the pair must be **adjacent** bins, because coupling
rises with frequency and one bin apart is the smallest imbalance the transform
allows (~1 dB). 160/200 would also have forced a 20 % clock cut instead of 4 %.

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

160 kHz is listened to and never transmitted.

## STEPS 1 AND 2 PASSED (3b4e902, baa2782)

The board boots at 144 MHz and `clk_adc` did not move — 48000 kHz, 500 ksps
untouched. `f` prints the whole tree measured against the crystal.

The transmitter is exact at the pad: tone A 180005 Hz at duty **50.0008 %**,
tone B 200000 Hz at **50.0000 %**, chip alignment exact by edge count on both
boards. `y` / `y 0` / `y 1` / `y 2` / `y 9` are its instruments.

**The self loop saturates the receiver** — driving tone A reads guard bin 7 at
391 against tone A's 814, with the raw operating point railed at mean code
3564 of 4095. The v1 generator through the same loop rails harder. Do not read
a self-loop guard rise as the kill switch; check `pio_carrier_duty_ppm()`
first. A guard reading is only meaningful at a **linear** level — which is
step 4.

## STEP 3 PASSED (4394180)

`dsp/gz_bank.c`: five bins, mag² only, no square root on the hot path, guards
every 4th window. It runs **beside** the v1 chain on core 1 and **starts off**,
which is what makes it costable in one image.

| 93D1, idle, ONE image | load | cycles/sample | overruns |
|---|---|---|---|
| bank off | 31 % | 91.9 | 0 |
| bank on | 47 % | **137.9** | 0 |

**The bank is 46.0 cycles a sample, 16 points of core 1.** Reproduced four
times, and on 379E. The first cut was 155 cycles and 84 % on identical
arithmetic — `gzb_push_run()` takes a run up to the window boundary and keeps
the filters in registers; a call a bin a sample does not.

Regression with the bank ON on the receiver: v1 link **FER 0.0000**, margin
490, **overruns 0**. `apps/handoff` 12 handshakes each in 30 s, 0 stalls,
0 overruns. Suite **25483 checks**.

**47 % is the bank plus the v1 chain it replaces.** The lower steady-state
figure is a projection until steps 5 and 6 take v1's Goertzel and `carrier.c`
out. Do not quote it as a reading. `__not_in_flash_func` was deliberately not
used — report it if you want it, do not slip it in.

## START AT STEP 4 — THE GATE

Brief §5. **Two boards, passive, on a wire, at a linear level.** Do not build
past it on faith.

| Measure | Passes when |
|---|---|
| `E_A` / `E_B` separation | the bins are orthogonal in practice, not just in theory |
| the 180/200 imbalance | ~10 % expected; wild or unstable kills §8's preamble normalisation |
| guards with nothing transmitting | noise, and the three agree inside their own spread |
| guards **while** transmitting | **must not rise** — this is the kill switch |

`n 2` is the instrument this step was waiting for: it reads all five bins in
the **same** windows, with the guard median and the ratio. `b` walks them one
at a time and cannot answer a ratio question at all.

**Before calling a guard rise fatal, read `pio_carrier_duty_ppm()`.** Bins 7
and 11 are the even-harmonic monitor; a duty error and a poisoned reference
look identical until you check. If the duty is exact and the guards still
rise, **stop** — that is v1's floor again and the brief says say so.

Two readings already in hand for comparison:

| generator | bin 9 A | bin 10 B | g 7 | g 8 | g 11 | median |
|---|---|---|---|---|---|---|
| quiet, both boards silent | 17 | 18 | 29 | 24 | 19 | 23 |
| tone A driven, **self loop, railed** | 814 | 2 | 391 | 2 | 23 | 23 |

The second row is the railed case, not a verdict. Step 4 repeats it at a level
where the amplifier is linear.

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken; `y 9` hands the pad back to v1 |
| `y 2` | chip alignment by edge count, four patterns, exact arithmetic |
| `k [bin]` | move the receive Goertzel to any bin, transmitter untouched |
| `b` / `b 1` | walk the five design bins one at a time, or every bin below Nyquist |
| **`n`** | **the core-1 budget: bank off, then on, in ONE image, cycles a sample** |
| **`n 0` / `n 1`** | **bank off / on and leave it** |
| **`n 2`** | **one capture: five bins in the SAME windows, guard median, ratio** |
| `pio_carrier_duty_ppm()` | the pad's duty, from a third state machine |
| `hal_pico_core1_busy()` | busy and wall clock together, for an interval rather than since boot |
| `hal_pico_bank_capture()` | core 1 fills N windows, then core 0 reads — nothing torn |

`y`, `k`, `b` and `n` are in **linktest**. Note `handoff` already uses `y` for
its GP15 sync pulse — do not collide.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port — they move. This session both were on
COM7 and COM8 respectively.

**Both boards boot linktest as the RECEIVER role** — the strap (GP14) is open
on the PCB. For a TX board use the `linktest_tx` target.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target linktest
picotool load -x build-pcb/linktest.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe` and is not on PATH.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock and resolves serial tails to ports. A command with an
argument must be **quoted**: `--at "2:A:n 1"`, or argparse eats the number.
It opens the port once, up front, so it **misses a boot banner**.

**Quiet the other board before any bin measurement.** A board left running
`apps/handoff` shouts on bin 10 every few seconds — it landed in a capture
this session and read 174 LSB on bin 10 against a 37 LSB median. Putting it on
`linktest` (receiver role) makes it silent.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13).

## REGRESSION BASELINE, measured at 4394180

Reproduce these before and after anything in step 4.

| | |
|---|---|
| v1 link, bank ON at the receiver | good 166, crc 0, lost 0, **FER 0.0000**, margin 490, overruns 0 |
| v1 link, bank off | good 94 in 17 s, FER 0.0000, margin 488 |
| `apps/handoff` | **12 complete handshakes each in 30 s**, 0 partial, 0 stalls, 0 overruns, load 38 % |
| suite | **25483 checks**, 0 failures |
| core-1 load | 31 % bank off, 47 % bank on, both idle |

Two one-off events carried through this session and did not recur across 100 s
of handshakes: one abort on 93D1 at its first rendezvous, and one bad frame on
379E. Watch whether they come back; they are not attributed to anything yet.

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes —
  it ate the `\n` out of a printf string this session and the build caught it.
  Use the `Write`/`Edit` tools for source.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25483 checks**. Keep it green.
- v1 still works on `main`. If this branch dies at step 4, that is a
  **successful outcome** — write up why and stop.
