# Link v2 — where to pick this up

Steps 1, 2, 3, **4 (the gate)**, **5** and **6** have passed. Start at step 7.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 is
`baa2782`, step 3 is `4394180`, step 4 is `ca2e0ea`, step 5 is `281b015` plus
the bench commit `1fd27d0`, step 6 is the commit tagged "link v2 step 6", and
the commits after each are documentation only.

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

Step 6 is the cleanest example of the rule paying out so far: design §8 asked
for an imbalance correction, and the answer was **not to build one**, because
the requirement it would have served does not exist. Deriving that took ten
minutes and deleted a subsystem.

## WHAT IS SETTLED. DO NOT RE-ASK.

| | |
|---|---|
| sys_clk | **144 MHz. Done and on the boards.** |
| Tone pair | **180/200 kHz, bins 9 and 10.** Adjacent. Not open. |
| Guards | **bins 7, 8, 11** = 140/160/220 kHz. Clear of every odd harmonic. Not open. |
| Two-tone transmitter | **built, measured at the pad, and it is now the LINK's transmitter.** Not open. |
| The five-bin bank | **built, costed, and it IS the receiver.** Not open. |
| Guard decimation | **every 4th window**, derived in `config.h`. Not open. |
| **The gate** | **passed. The guards do not rise while transmitting.** Not open. |
| **Presence** | **built, `k` computed, swept on the bench, `carrier.c` deleted.** Not open. |
| **The framer** | **no slicer. A chip is a signed `E_B - E_A`.** Not open. |
| **The imbalance** | **needs no correction, and the derivation is in `frame.h`.** Not open. |
| The core-1 loop | **lives in RAM.** See the hazard section. Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

The pair must be **adjacent** bins (`link-v2-design.md:95`): coupling rises with
frequency and one bin apart is the smallest imbalance the transform allows.

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

## STEP 6 PASSED — and it gave back half of core 1

A chip is `d = E_B - E_A` in mag², signed, one number across the ipc ring.
`frame.c`'s slicer, `link_sm.c`'s OOK bridge and the v1 Goertzel chain are all
deleted. Brief §7 has the full tables; these are the numbers that matter.

| gap, same session | v2 FER | v2 BER | v1 FER | v1 BER |
|---|---|---|---|---|
| 5 cm | **0.0000** | **0** | 0.0000 | 0 |
| 10–12 cm | **0.0000** | **0** | 0.0025 | 0 |
| 25 cm | **0.9683** | **0.374** | 1.0000 | 0.485 |

At 25 cm v2 got **11 frames across where v1 got none**. `apps/handoff`: **15
complete in 42 s on both radios**, 0 partial, 0 abort, 0 overruns — but v2 at
**21 % of core 1 against v1's 35 %**, and against step 5's 47 %.

### Three readings worth keeping

- **Presence reads 88.6 % busy with frames flowing**, and a 156 ms frame every
  176 ms is 88.6 %. The detector is up for exactly the airtime. That is why
  the OOK bridge could be deleted rather than shortened.
- **Tone A is 8.6 % stronger** on `n 2` with frames flowing, against step 4's
  9.1 % from `y 3`. Two instruments, two sessions, same answer.
- **The receiver costs 52.6 cycles a sample** (bank off 7.3, bank on 59.9, one
  image), against step 5's 46.8 for the bank and presence beside a v1 chain.

### THE HAZARD FROM STEP 5, STILL LIVE

**Core 1 runs from flash, and `apps/handoff` has BTstack on core 0 using the
same flash.** At step 5 `handoff` hit 98 % core-1 load with 1709 DMA overruns
in thirty seconds and zero handshakes, where `main` managed 13 complete at
35 % with 0 overruns. The whole core-1 path is `HANDOFF_HOT_FUNC` now and runs
from RAM. Four things to carry:

1. **Every core-1 budget taken in `linktest` is a lower bound, not a budget.**
   Core 0 there is an idle console. Measure in `handoff`.
2. **`hal_pico_core1_load()` is since boot.** Use `hal_pico_core1_busy()` and
   subtract.
3. **Overruns at low load mean a stall, not a shortage.**
4. If you add anything to core 1, mark it `HANDOFF_HOT_FUNC`.

## START AT STEP 7 — RENDEZVOUS BY NONCE

Brief §8, design §7.

Replace the flat-tone shout with a beacon **frame**: preamble 16 chips, marker
8, nonce 16, flags 8, CRC-16. About 30 ms at 4000 chips/s.

| Question | v1, and still today | v2 |
|---|---|---|
| is that a peer? | a tone longer than `SHOUT_MIN_US` | the CRC passed |
| is that my own echo? | sample-time cut, and hope | the nonce is mine |
| who sends? | who heard whose tone first | higher nonce |

**This kills five constants and one whole bug class.** `SHOUT_US`,
`SHOUT_MIN_US`, `LISTEN_MIN_US`, `LISTEN_MAX_US` and `QUIET_WAIT_MAX_US` all
go. The "own shout heard" bug from 24 Sep (`handoff-elects-two-senders`)
becomes unwritable, because identity is in the payload rather than inferred
from timing.

`TRIG_SETTLE_US` stays but becomes physical: the AFE's high-pass RC, measured,
not 6000 µs because 6000 worked.

**Beacon period** is derived from design §2's one-second handshake requirement
and the beacon's own airtime. Show the collision-probability working.

**Passes when:** two boards, 15 minutes, no double-send, no self-trigger, and
every rendezvous accounted for in the counters.

**Owes:** host tests for a nonce tie (both draw the same — must redraw), and
for a beacon arriving with its CRC damaged (must be ignored, not acted on).

### What step 6 leaves you that step 7 should use

The trigger still shouts a flat tone, and under FSK that is an unbroken tone B
— which presence reads perfectly well, so rendezvous works today. Nothing is
broken. What is missing is the CONTENT, and everything needed to put content
there is already built: the framer decodes at 4000 chips/s with no threshold,
and `handoff` completes 15 handshakes in 42 s off it.

Also note **design §2's one-second budget is met on one role and not the
other**: 949 ms as the faster role, 1133 ms as the slower. Step 7 changes how
a contact starts, so it is the step that can move that number.

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken |
| **`y 9`** | hands the pad back to v1 — **and the LINK then cannot transmit** until `y 0/1/3`. It was harmless before step 6; it is not now, and the console says so. |
| **`y 3`** | tone A and tone B on ALTERNATE chips — the imbalance instrument |
| `y 2` | chip alignment by edge count. **Leaves the pad released** |
| `k [bin]` | move the **PROBE** Goertzel to any bin. The link is on bins 9 and 10 and does not move — retuning costs no frames any more. |
| `b` / `b 1` | walk the five design bins, or every bin below Nyquist |
| `n` | the core-1 budget: bank off, then on, in ONE image, cycles a sample |
| `n 0` | bank off — **the RECEIVER is then off**: no chips and no presence. Nothing else scores a bin since step 6. |
| `n 2` | one capture: five bins in the SAME windows, guard median, ratio, **and the operating point with a linear/RAILED verdict** |
| `p [s]` | presence: the busy FRACTION over an interval, with the power ratio |
| `m` | the **probe** bin's level, mean and max per Goertzel window |
| `handoff n [0\|1]` | the bank off / on inside `handoff`, where core 0 is busy |
| `handoff_decode --v1` | replay a v1 OOK capture through the slicer the firmware no longer has |

`y`, `k`, `b`, `n` and `p` are in **linktest**. `handoff` already uses `y` for
its GP15 sync pulse — do not collide.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port — they move. On 25 Sep they were COM7
and COM8 respectively, and `apps/handoff` from this branch is on both.

**Both boards boot linktest as the RECEIVER role** — the strap (GP14) is open
on the PCB. For a link BER run flash `linktest_tx` on one and `linktest` on
the other; for a bin bench two plain `linktest` images are enough.

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
`--at "2:A:n 2"`. It opens the port once, up front, so it **misses a boot
banner**.

**THE v1 YARDSTICK IS A WORKTREE, AND IT IS CHEAP.**
`git worktree add C:\work\handoff-main main`, build there, flash, measure,
flash back. That is how step 6's comparison was taken and it is the only way
to get v1 and v2 at the same coupling — the plate path drifts between
sessions, so a number from another day is not a comparison.

**A gap where both radios read FER 0 proves nothing.** Step 6 wasted a round
learning that: 5 cm gave 0 and 0. Find the knee. On this bench it is between
12 cm and 25 cm, and 10–12 cm is the first place the two differ.

**`z` does not reset `false_syncs`** — that counter is the framer's lifetime
one, so a number carried from a noisy gap follows you into the next run.

**A tone left driving survives the end of a `link2.py` run.** End a run with
`y 9`, or begin the next one with it — and remember `y 9` now stops the link
transmitting.

**The coupling drifts on its own.** Take a matched quiet capture in the **same
run** as the tone capture it is compared against.

**Geometry is the only level control**, and **a hand near a band beats the
transmitter**. Close the gap in steps with hands withdrawn between them.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13). That is step 8.

## REGRESSION BASELINE, measured at step 6 on the plate path at 5 cm

| | |
|---|---|
| v2 link | good 400, crc 0, lost 0, **FER 0.0000**, BER 0, margin 667, overruns 0 |
| `apps/handoff` | **15 complete handshakes in 42 s** on each board, 0 partial, 0 abort, 0 stalls, **0 overruns** |
| core 1 | bank off 7.3 cycles/sample, bank on 59.9 — the receiver costs **52.6** |
| load | 20 % in `linktest`, **21 % in `handoff`** against `main`'s 35 % |
| presence | 88.6 % busy with frames flowing, which is the transmitter's duty |
| suite | **25567 checks**, 0 failures |

`main` on the same bench, same gap: 15 complete in 42 s, 35 % load, FER
0.0000, margin 437.

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- **The bench needs hands sometimes, and the user is not always watching the
  chat.** When a band has to move, STOP and ask, then wait. Do not plan a run
  around a movement you have not been told has happened.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes.
  **Write Python to the scratchpad and run it with PowerShell.** Step 6 found
  a `printf` in `decode.c` whose `\n` had been eaten by exactly that, in a
  file that had not compiled since.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25567 checks**. Keep it green.
- `apps/turnaround` is **not built on this branch** and that is deliberate —
  firmware/CMakeLists.txt says why. It still builds on `main`.
- v1 still works on `main`. If this branch dies at a later step, that is a
  **successful outcome** — write up why and stop.
