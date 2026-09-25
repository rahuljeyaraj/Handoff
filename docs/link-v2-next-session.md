# Link v2 — where to pick this up

Steps 1, 2, 3, **4 (the gate)** and **5** have passed. Start at step 6.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 is
`baa2782`, step 3 is `4394180`, step 4 is `ca2e0ea`, step 5 is `281b015` plus
the bench commit `1fd27d0`, and the commits after each are documentation only.

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
| The five-bin bank | **built, costed, running by default.** Not open. |
| Guard decimation | **every 4th window**, derived in `config.h`. Not open. |
| **The gate** | **passed. The guards do not rise while transmitting.** Not open. |
| **Presence** | **built, `k` computed, swept on the bench, `carrier.c` deleted.** Not open. |
| The core-1 loop | **lives in RAM.** See the hazard section. Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

The pair must be **adjacent** bins (`link-v2-design.md:95`): coupling rises with
frequency and one bin apart is the smallest imbalance the transform allows.

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

## STEP 5 PASSED — the sweep, and what it cost to get there

`busy = max(E_A,E_B) > k × mean(median of the three guards)`, with `k` computed
from a stated false-busy rate. `dsp/carrier.c` is deleted.

| gap | quiet | tone A | power ratio | against `k` = 16.76 |
|---|---|---|---|---|
| 1 m | 0 % | **0 %** | 1:1 | out of range |
| 30 cm | 0 % | **0 %** | 2:1 | out of range |
| **15 cm** | 0 % | **58.5 %** | 12:1 | **ON THE LINE** |
| 10 cm | 0 % | **100 %** | 77:1 | 4x clear |
| 5 cm | 0 % | **100 %** | 651:1 | 38x clear |

Quiet, both settled: **0 busy in 100024 windows**, both boards. The crossover
landed on the derived `k` without anything being adjusted.

### THE HAZARD THAT NEARLY KILLED IT, AND IT IS NOT IN §11

**Core 1 runs from flash, and `apps/handoff` has BTstack on core 0 using the
same flash.** `handoff` hit **98 % core-1 load with 1709 DMA overruns in
thirty seconds** and zero handshakes, where `main` on the same bench managed
13 complete at 35 % load with 0 overruns.

The obvious reading — "the bank does not fit" — was **wrong**. A live
two-second interval read **46 %**, not 98 %, while overruns kept arriving. You
do not drop DMA blocks at 46 % load; you drop them when something **stalls**
core 1 rather than keeping it busy.

Four things to carry:

1. **Every core-1 budget taken in `linktest` is a lower bound, not a budget.**
   Core 0 there is an idle console. Measure in `handoff`, which now has an
   `n [0|1]` bank toggle for exactly that.
2. **`hal_pico_core1_load()` is since boot.** Use `hal_pico_core1_busy()` and
   subtract, or a long-broken board reads 98 % while it is currently at 46 %.
3. **Overruns at low load mean a stall, not a shortage.** Different fault,
   different fix.
4. The whole core-1 path is now `HANDOFF_HOT_FUNC` (config.h) — a plain GCC
   `.time_critical.*` attribute, not the SDK macro, because `lib/dsp` must not
   see an SDK header. If you add anything to core 1, mark it.

### The other two, both self-inflicted

- **Two `isqrt64` and a 64-bit divide per window**, for telemetry nobody reads
  more than once a second: 18 points of core 1. Design §6's "no square roots
  on the hot path" is a requirement. Core 0 raises a flag; the next window
  answers it.
- **The console printed an amplitude ratio next to a `k` in power.** A 19x
  margin read as if it were scraping past 16.76. Both consoles now print the
  power ratio.

## START AT STEP 6 — THE FRAMER, WITHOUT A SLICER

Brief §7.

```
a chip is E_A > E_B.  There is nothing to slice.
```

| | |
|---|---|
| delete | `frame.c`'s `hi`, `lo`, `primed`, `step_toward` — all four |
| promote | `FRAME_ALT_WINDOW` 24 / `FRAME_ALT_MIN` 22 from typed to computed; the derivation is already written in `frame.h` |
| keep | **Manchester.** FSK retires its threshold job, not its timing job. Design §8. |
| the imbalance | take it from the preamble, never from a constant. Tone A measured **9.1 % STRONGER** — design §8 has the sign backwards, and it does not matter, because §8 takes the ratio from the signal. |

**Passes when:** BER through the self loop and over the wire is **at least as
good as v1 on the same bench**. Not "close to". The v1 yardstick is alive and
was measured at step 5 — see the regression below.

### AND STEP 6 DELETES THE OOK BRIDGE

`link_sm.c` carries `LINK_OOK_BRIDGE_US`, which treats the channel as occupied
for `MANCHESTER_MAX_RUN_CHIPS + 1` chips after the last busy reading. It exists
because **v1 switches the carrier off for a zero**, so half of every v1 frame
is silence and a detector with no memory answers "nobody is transmitting" in
each gap — truthfully. Without it no rendezvous completed at any phase.

FSK has no spaces. Once the framer is two tones, nothing exercises the bridge.
**Delete it, and `MANCHESTER_MAX_RUN_CHIPS` with it if nothing else wants it.**

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken; `y 9` hands the pad back to v1 |
| **`y 3`** | tone A and tone B on ALTERNATE chips — the only way to ask an imbalance question |
| `y 2` | chip alignment by edge count. **Leaves the pad released** — `y 0` after it re-takes it. |
| `k [bin]` | move the receive Goertzel to any bin, transmitter untouched |
| `b` / `b 1` | walk the five design bins, or every bin below Nyquist |
| `n` | the core-1 budget: bank off, then on, in ONE image, cycles a sample |
| `n 2` | one capture: five bins in the SAME windows, guard median, ratio, **and the operating point with a linear/RAILED verdict** |
| **`p [s]`** | **presence: the busy FRACTION over an interval, with the power ratio and the operating point. This is the step 5 instrument.** |
| `m` | chip energy and the raw operating point on one bin |
| `handoff n [0\|1]` | the bank off / on inside `handoff`, where core 0 is busy |
| `hal_pico_presence()` | the detector read without clearing its latch |

`y`, `k`, `b`, `n` and `p` are in **linktest**. `handoff` already uses `y` for
its GP15 sync pulse — do not collide.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port — they move. This session both were on
COM7 and COM8 respectively.

**Both boards boot linktest as the RECEIVER role** — the strap (GP14) is open
on the PCB. So **two plain `linktest` images are enough for a two-board bin
bench**: `y 0` on one board transmits, `p` or `n 2` on the other reads.
`linktest_tx` is only needed for a v1 link BER run.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target linktest
picotool load -x build-pcb/linktest.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`, is not on PATH, and the
home directory has a space in it — call it from PowerShell with `& $pt`, not
from the Bash wrapper.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock. A command with an argument must be **quoted**:
`--at "2:A:n 1"`. It opens the port once, up front, so it **misses a boot
banner**.

**A tone left driving survives the end of a `link2.py` run.** End a run with
`y 9`, or begin the next one with it.

**The coupling drifts on its own.** Take a matched quiet capture in the **same
run** as the tone capture it is compared against.

**Geometry is the only level control**, and **a hand near a band beats the
transmitter**. Close the gap in steps with hands withdrawn between them.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

**When a change breaks `handoff`, flash `main` to both boards and measure the
same thing.** `git worktree add C:\\work\\handoff-main main` takes a minute and
it is what turned "the bench is marginal" into "I broke it" at step 5.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13). That is step 8, not
step 6.

## REGRESSION BASELINE, measured at `1fd27d0` on the plate path at 5 cm

| | |
|---|---|
| v1 link | good 200, crc 0, lost 0, **FER 0.0000**, BER 0, margin 437, overruns 0, false syncs 1 |
| `apps/handoff` | **12 and 13 complete handshakes in 35 and 36 s**, 0 partial, 0 abort, 0 stalls, **0 overruns** |
| core 1 | bank off 85.6 cycles/sample, bank on 132.4 — bank AND presence cost **46.8**, against step 3's 46.0 for the bank alone |
| load | 46–47 % in `linktest`, 47 % in `handoff` against `main`'s 35 % |
| suite | **25536 checks**, 0 failures |

`main` on the same bench, for comparison: 13 complete in 34 s, 35 % load,
0 overruns.

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- **The bench needs hands sometimes, and the user is not always at the table.**
  Ask before planning a run around a physical change, and say plainly what has
  to move.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes.
  **Write Python to the scratchpad and run it with PowerShell** — a heredoc ate
  the `\\n` out of six printf strings this session, twice.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25536 checks**. Keep it green.
- `apps/turnaround` is **not built on this branch** and that is deliberate —
  firmware/CMakeLists.txt says why. It still builds on `main`.
- v1 still works on `main`. If this branch dies at a later step, that is a
  **successful outcome** — write up why and stop.
