# Link v2 — where to pick this up

Steps 1, 2, 3 and **4, the gate** have passed. Start at step 5.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 is
`baa2782`, step 3 is `4394180`, step 4 is `ca2e0ea`, and the commits after each
are documentation only.

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
| Guard decimation | **every 4th window**, derived in `config.h`. Not open. |
| **The gate** | **passed. The guards do not rise while transmitting.** Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |
| v1's carrier floor | not being fixed. `carrier.c` gets deleted, not tuned. |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

The pair must be **adjacent** bins (`link-v2-design.md:95`): coupling rises with
frequency and one bin apart is the smallest imbalance the transform allows.
160/200 would also have cost a 20 % clock cut instead of 4 %.

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

## STEP 4 PASSED (`ca2e0ea`) — and it settled four things beyond its own rows

Two boards, passive, **coupled plate to plate through air with no wire**. Ten
matched quiet/tone rounds, every capture linear, signal swept 64 → 716 LSB.
Brief §5 has the tables.

| Measure | Result |
|---|---|
| guards while transmitting | **DO NOT RISE.** Quiet median 79 → tone A 76 → tone B 74 → alt 71, at the loudest of an 11× sweep |
| `E_A` / `E_B` separation | each tone raises its own bin only; the off-tone bin is indistinguishable from silence |
| the imbalance | **tone A 9.1 % STRONGER**, ±0.5 % over a 2.3× level range |
| guards, silent | 116 / 88 / 52 on bins 7 / 8 / 11 — a stable 2.3:1 slope, so the median always lands on bin 8 |

CFAR margin at the loudest linear point: **94 in power for tone B, 52 for tone
A**, against the `k ≈ 10` §6 derives. Seven to ten dB — on a plate path, not a
skin path.

### The four things to carry forward

1. **A railed capture answers nothing, and the guards will not tell you it is
   railed.** Read the operating point printed under every `n 2`: the mean code
   walking (2309 → 3624 at contact) is the saturation tell that assumes no crest
   factor. Tone B's harmonic comb closes on bins 5, 10 and DC — 200 kHz is
   exactly `fs / 2.5` — so **driven into saturation on tone B the guards read
   flat**. Half the symbols are invisible to the even-harmonic monitor.
2. **Bins 7 and 11 monitor the receiver's compression, not the generator.** At
   contact they carry tone A's 2nd and 4th harmonics, made after the coupling —
   the duty at the pad is 50.0000 %.
3. **A hand near a band beats the transmitter.** Moving the boards by hand gave
   1100–1240 LSB raw RMS with the tone bins at ~300. Hands off and settled, or
   the reading is the operator.
4. **Geometry is the only level control on this bench.** No gain knob, no
   attenuator. Sweep by closing the gap in steps with hands withdrawn between
   them, alternating silence and tones so every hold gives a matched pair.

## START AT STEP 5 — PRESENCE, AND THE DEATH OF `carrier.c`

Brief §6. This is the first step that **deletes** something.

```
signal = max(E_A, E_B)                    /* mag², no sqrt */
noise  = median(E_140, E_160, E_220)      /* time-averaged */
busy   = signal > k × noise
```

| | |
|---|---|
| `k` | **computed, not chosen** — from a stated false-busy rate. Put the derivation in the source next to it, with its working. §6 has the cells-to-`k` table. |
| the reference | in practice this is **bin 8**, because the noise sits on a fixed 2.3:1 slope. Write that down; the median is outlier protection, not an average. |
| delete | `firmware/lib/dsp/carrier.c` and `carrier.h`. Not deprecate. Rewire `link_sm.c`'s callers, brief §7's table. |
| `test_beacon.c` | its floor tests go with the floor; its trigger tests do not |

**Passes when** `busy` tracks reality with the level swept — and on this bench
the level is swept by geometry, so plan for the hands-off stepped sweep above.
The 379E state from `carrier-floor-still-climbing-brief.md` — peak 133–150,
floor 61–83, deaf — must be unreproducible, because there is nothing to climb.

**Watch the core-1 load while you are in there.** It reads 48–49 % in `handoff`
against a 38 % baseline and 55 % in `linktest` against 44 %. Overruns are 0 and
more frames are being decoded on this louder path, but it is unexplained and
unattributed. Step 5 takes v1's Goertzel and `carrier.c` out, so it is the step
that should make the number fall.

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken, pad taken explicitly and reported; `y 9` hands the pad back to v1 |
| **`y 3`** | **tone A and tone B on ALTERNATE chips, unbroken — the only way to ask an imbalance question, because one capture then holds both bins in the same windows** |
| `y 2` | chip alignment by edge count, four patterns, exact arithmetic. **Leaves the pad released** — `y 0` after it re-takes it. |
| `k [bin]` | move the receive Goertzel to any bin, transmitter untouched |
| `b` / `b 1` | walk the five design bins one at a time, or every bin below Nyquist — `b 1` is what turns "a guard rose" into a harmonic comb |
| `n` | the core-1 budget: bank off, then on, in ONE image, cycles a sample |
| `n 0` / `n 1` | bank off / on and leave it |
| `n 2` | one capture: five bins in the SAME windows, guard median, ratio, **and the operating point with a linear/RAILED verdict** |
| `m` | chip energy and the raw operating point on one bin |
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
on the PCB. So **two plain `linktest` images are enough for a two-board bin
bench**: `y 0` on one board transmits, `n 2` on the other reads. `linktest_tx`
is only needed for a v1 link BER run.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target linktest
picotool load -x build-pcb/linktest.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`, is not on PATH, and the
home directory has a space in it — call it from PowerShell with `& $pt`, not
from the Bash wrapper.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock and resolves serial tails to ports. A command with an
argument must be **quoted**: `--at "2:A:n 1"`. It opens the port once, up front,
so it **misses a boot banner**. For a long sweep, generate the `--at` list in
PowerShell rather than typing it.

**A tone left driving survives the end of a `link2.py` run.** One run started
with the far board still shouting from the previous one and read a level twice
what it should have. End a run with `y 9`, or begin the next one with it.

**The coupling drifts on its own.** Untouched, the received level moved 826 →
1590 LSB over a few minutes and the quiet floor tripled. Take a matched quiet
capture in the **same run** as the tone capture it is compared against; a quiet
reference from an earlier run is not a control.

**Quiet the other board before any bin measurement.** A board left running
`apps/handoff` shouts on bin 10 every few seconds. `linktest` in the receiver
role is silent.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13) — and step 4 saw why:
tethered, the shared PC ground is a return path strong enough that at cm scale
the level barely tracks the gap.

## REGRESSION BASELINE, measured at `ca2e0ea` on the plate path

| | |
|---|---|
| v1 link, bank ON at the receiver | good 104, crc 0, lost 0, **FER 0.0000**, BER 0, margin 1412, overruns 0 |
| v1 link, bank off | good 49, FER 0.0000, margin 1415 |
| `apps/handoff` | **12 complete handshakes each in 33 s**, 0 partial, 0 stalls, 0 overruns |
| suite | **25483 checks**, 0 failures |
| core-1 load | 48–49 % in `handoff`, 55 % in `linktest` — **up on the 38 / 44 % baseline, unexplained** |

Margin here is 1412 against the wire bench's 490, so these are not comparable
to the step 3 figures except in shape. One abort on 93D1 at its first
rendezvous is still carried and still one.

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- **The bench needs hands sometimes, and the user is not always at the table.**
  Ask before planning a run around a physical change, and say plainly what has
  to move. Three runs were wasted this session measuring an unchanged bench.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes —
  it ate the escapes out of a Python replacement string this session. Use the
  `Write`/`Edit` tools for source.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25483 checks**. Keep it green.
- v1 still works on `main`. If this branch dies at a later step, that is a
  **successful outcome** — write up why and stop.
