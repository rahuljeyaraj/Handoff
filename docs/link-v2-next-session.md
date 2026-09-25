# Link v2 — prompt for the next session

Copy everything below the line into a new session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, clean at `3b4e902`.

Read `docs/link-v2-design.md` end to end first, then `docs/link-v2-brief.md`.
The design is the authority; the brief is the build order. Do not re-read the
v1 carrier-floor briefs unless something sends you there.

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
| Guards | **bins 7, 8, 11.** Clear of every odd harmonic. Not open. |
| Scope | radio only for behaviour; **instruments are unrestricted** |
| v1's carrier floor | not being fixed. `carrier.c` gets deleted, not tuned. |

## STEP 1 IS DONE AND PASSED (3b4e902)

The board boots at 144 MHz. This is a boot clock, not a runtime switch: the
SDK gets `SYS_CLK_HZ` plus the `PLL_SYS_*` triple in the root `CMakeLists.txt`,
`config.h` carries the same number, and `hal_pico.c` static-asserts they agree.

Measured on both boards, against the crystal, with the new `f` console command
(`hal_pico_clocks()`, in linktest and handoff):

| Clock | Reads |
|---|---|
| clk_ref | 12001 kHz |
| clk_sys | 144000 kHz |
| clk_usb | 48000 kHz |
| **clk_adc** | **48000 kHz — did not move** |
| clk_peri | 144000 kHz |
| ADC rate | 499947–499996 sps |

`clk_adc` sits on PLL_USB by the hardware reset default, so 500 ksps is
untouched. That was the one thing that could have ended the branch at step 1.

Everything else that was checked: txgen self-measures 200000 Hz and 40000 Hz
at **0 ppm** on both clocks; the v1 link bands-back-to-back went from
FER 0.0013 to **0.0000** (good 728, crc 0, lost 0, margin 488); `apps/handoff`
on both boards completed **6 handshakes each in 20 s**, 0 partial, 0 abort;
the phone bonded to 93D1 is still served; LED and motor walked by the user and
confirmed unchanged. Suite 25263 checks, 0 failures.

`config.h` now rejects 150 MHz at build time — *"tone A is not a whole number
of system cycles"*. `firmware/test/host/test_clock.c` holds design §4's
arithmetic (the 800/720 cycle counts, the harmonic folds, the guard set) so
the hand working and the build check each other.

## START AT STEP 2 — THE TWO-TONE TRANSMITTER

Brief §3. This is the biggest single piece of work on the branch; budget for
it. The old bit-stream generator cannot do two tones (78 kB a frame against a
16 kB buffer). The replacement is the nine-instruction counted-toggle loop in
brief §3, one 32-bit word per chip, 2496 bytes a frame.

Two things the brief is emphatic about, both for the same reason:

- **The `[1]` on the high `set` is not decoration.** Without it the low half
  runs one cycle longer, the duty cycle is not 50 %, and energy lands in the
  EVEN harmonics — one of which aliases straight onto a guard bin. That is
  v1's floor wearing a new hat.
- **Each period must be an even number of cycles**, or the two halves cannot
  be equal. `config.h` already static-asserts this.

Chip words, at 144 MHz, SM divider 1 — check this arithmetic, do not copy it:

| Tone | Cycles/period | `isr` | Periods/chip | `y` | Word |
|---|---|---|---|---|---|
| 180 kHz | 800 | 396 | 45 | 44 | `0x002C_018C` |
| 200 kHz | 720 | 356 | 50 | 49 | `0x0031_0164` |

Both chips are 36 000 cycles = 250 µs exactly.

**Owes:** a host test that builds the chip words and checks both come to
36 000 cycles and that each period is even.

**Verify on the board, no scope needed:**

| | Check | Passes when |
|---|---|---|
| 2a | drive each tone, read `pio_carrier_measure_hz()` | 180000 and 200000 within 0.1 % |
| 2b | self loop, alternating chips, watch the bins | 9 and 10 alternate, guards near zero |
| 2c | self loop, a known 624-chip pattern | no errors piling up down the frame |

**`linktest`'s `x` self loop sends all-ones chips today — energy only, never a
frame.** So "0 good frames" from `x` is correct, not a fault; do not chase it.
2c needs `x` extended to send a real frame. That is instrument work and is in
scope.

## STEP 4 IS THE GATE

Two boards, passive, on a wire. If the **guard bins rise while transmitting**,
stop and say so — the noise reference is poisoned and that is v1's floor
again. Do not build past step 4 on faith. Brief §5.

## TWO THINGS CARRIED FORWARD FROM STEP 1

**1. Core-1 load is more sensitive to flash layout than to the clock.** Same
board, quiet bench: v1 code at 150 MHz **29%**, v2 code at 150 MHz **33%**,
v2 code at 144 MHz **34%**. The clock cost the 4% the design predicted; the
other four points came from adding one unrelated function and shifting the
image in XIP.

**So step 3's budget cannot be settled by comparing two images.** A 4-point
swing from nothing would swamp the answer. It needs a within-image cycle count
(or the block loop pinned in RAM with `__not_in_flash_func`, which would be a
change to what *does* — report it, do not slip it in). Build the instrument
before you trust the number.

**2. The guard bins watch tone A, not tone B.** Design §4 calls bins 7 and 11
the even-harmonic duty-cycle monitor. That is right, but **both are reached by
tone A**: its 2nd harmonic folds to bin 7, its 4th to bin 11. Tone B's 2nd
lands on bin 5 and its 4th on tone B itself, neither of which is a guard. A
duty-cycle slip on the 200 kHz symbol is therefore nearly invisible in the
guards. Know this before step 2b reads them as a monitor.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port — they move. Both currently hold
`apps/handoff` built at 144 MHz for the PCB.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target handoff
picotool load -x build-pcb/handoff.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe` and is not on PATH.

`build-v1/` holds the 150 MHz baseline images. Keep them — they are what
"unchanged" is measured against.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock and resolves serial tails to ports. It opens the port
once, up front, so it **misses a boot banner** — anything that prints only at
startup (txgen) needs a logger that waits for the port to come back after the
reboot.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

Ask the user to put the two bands **back to back** before any link measurement
— apart, the v1 link decodes nothing and the numbers mean nothing. They did
this on request last session.

Body-coupled runs must be **floating on cells**, USB out, TX gated at SW1.
A tethered reading is wrong, not just unsafe (design §13).

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** Not
  "should work". The reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.** If an
  instrument turns up a bug in working code, report it, do not fix it here.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes.
  Write a python script with the `Write` tool and run it.
- Never name TP numbers — the silk has no TP labels. Name the signal or the
  part end.
- `python scripts/test.py` is green at **25263 checks**. Keep it green.
- v1 still works on `main`. If this branch dies at step 4, that is a
  **successful outcome** — write up why and stop.
