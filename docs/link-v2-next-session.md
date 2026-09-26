# Link v2 — where to pick this up

**All eight steps have passed and the branch is merged to `main`.** Step 8's
stated conditions were met on 26 Sep 2026 — 1015 good frames worn against the
447 asked for, FER 0.1625 against 0.296. Brief §9 has the tables.

**Two things step 8 owes, and both are bench runs, not code:**

| owed | why |
|---|---|
| the control run — bands apart, same spacing, nobody touching | it was skipped, so nothing yet attributes the 1015 frames to the body path by measurement |
| `apps/handoff` worn, on cells, both roles | 93D1 did not come up on its cell, so the SYSTEM has never completed a handshake on a wrist on v2. v1 did, 17 on each board |

Until that second one is taken, v2 is proven as a radio and not as a product.
It is the one gap against the thing it replaced.

Also open, from the same bench: **`worst gap` went 3 → 18** and the BER column
went 1.87e-02 → 3.09e-02. Neither is a pass condition and brief §9 explains
both, but the dropout instrument is worse than it was and nothing has
explained why.

The rest of this file is the step-8 briefing as it stood, kept because the
bench recipes and the hazards are still current.

Paste everything from the line below into a fresh session.

---

Continue the Handoff link v2 redesign. Repo `C:\work\Handoff`, branch
`redesign/link-v2`, working tree clean. Step 1 is `3b4e902`, step 2 `baa2782`,
step 3 `4394180`, step 4 `ca2e0ea`, step 5 `281b015` plus the bench commit
`1fd27d0`, step 6 `cca9bd5`, step 7 `aeb227a` plus `99f9d54`. The commits after
each are documentation only.

Read `docs/link-v2-design.md` end to end first, then `docs/link-v2-brief.md`.
The design is the authority; the brief is the build order. Both carry a "what
step N actually measured" section — read those, they are where the plan was
wrong. Do not re-read the v1 carrier-floor briefs unless something sends you
there.

Also read the memory files `link-v2-redesign`, `never-say-tp-numbers`,
`bash-no-single-quotes`, `pico-bench-recipe`, `skin-link-passed` and
`pad-noise-and-intermittency`.

## THE RULE, WHICH IS THE WHOLE POINT

Every number in the link must be **physical, structural, derived, or computed
from a stated error rate**. Nothing is allowed in because it worked on a
bench. If you find yourself picking a value because it worked, stop and write
down what requirement it should come from instead. Design §1.

The rule has now paid out three times, and each time by DELETING something:

| step | design asked for | what the rule said |
|---|---|---|
| 6 | an imbalance correction | the requirement does not exist — Manchester cancels it |
| 7 | election by higher nonce | at most one band can decode the other. Nothing to elect |
| 7 | a 16-chip beacon preamble | no hunt window meets the stated false-sync rate. Share the card's 32 |

## WHAT IS SETTLED. DO NOT RE-ASK.

| | |
|---|---|
| sys_clk | **144 MHz.** Done and on the boards. |
| Tone pair | **180/200 kHz, bins 9 and 10.** Adjacent. Not open. |
| Guards | **bins 7, 8, 11.** Clear of every odd harmonic. Not open. |
| Two-tone transmitter | **built, measured at the pad, it IS the transmitter.** |
| The five-bin bank | **built, costed, it IS the receiver.** |
| **The gate** | **passed. Guards do not rise while transmitting.** |
| **Presence** | **built, `k` computed, `carrier.c` deleted.** |
| **The framer** | **no slicer. A chip is a signed `E_B - E_A`.** |
| **The trigger** | **a frame with a nonce and a CRC. `TRIG_WAIT` deleted.** |
| The core-1 loop | **lives in RAM.** See the hazard section. |
| Scope | radio only for behaviour; **instruments are unrestricted** |

### 160 kHz is a GUARD, not tone A. Do not "correct" this.

| | 140 | **160** | **180** | **200** | 220 |
|---|---|---|---|---|---|
| bin | 7 | 8 | **9** | **10** | 11 |
| role | guard | **guard** | **tone A** | **tone B** | guard |

## STEP 7 PASSED — 308 handshakes, zero self-triggers

The flat-tone shout is a beacon **frame**: 32-chip preamble, marker `11110101`,
16-bit nonce, CRC-16. 112 chips, 28 ms. Brief §8 has the full tables.

| 930 s, two boards, 5 cm | 93D1 | 379E |
|---|---|---|
| handshakes | **308 COMPLETE** | **308 COMPLETE** |
| partial / abort | 0 / 0 | 0 / 0 |
| **both-sender** | **0 of 308** | |
| **self-echoes** | **0** | **0** |
| overruns during the run | 0 | 0 |
| core-1 load | 21–22 % | 21–22 % |

And the test v1 failed: a band alone, 87 s, **774 beacons, 0 sends, 0 receives,
0 framer syncs.** v1 elected itself sender on 60 shouts out of 60.

774 beacons in 87 s is one every 112.4 ms, against the 112 ms the period
derivation gives. The arithmetic lands on the bench.

### Six constants and one state are gone

`SHOUT_US`, `SHOUT_MIN_US`, `LISTEN_MIN_US`, `LISTEN_MAX_US`,
`QUIET_WAIT_MAX_US`, `DETECT_US`, and the whole `TRIG_WAIT`. The listen window
is now derived by minimising the rendezvous (`C = 4T`), and the settle is
derived from the ADC block plus the amplifier's own figure.

### The settle is the RING, not the AFE

`y 4` in linktest measures it. The signal bin reads ~670 at 0, 1 and 2 ms and
~20 by 4 ms — a step at exactly one DMA block, not a decay. The amplifier is
back inside **123 µs**; everything else is the ring draining, and it **does not
grow with the beacon**. So `HANDOFF_TRIG_SETTLE_US` is
`HANDOFF_RX_LATENCY_US + HANDOFF_TURNAROUND_US` = 5096 µs.

**The first reading of this was wrong.** The far board was still running and
its transmissions came back as 400-LSB spikes through the watch window, which
one number reported as a 40 ms settle. The trace is what caught it. **Silence
the other board before any one-board measurement.**

### THE HAZARD FROM STEP 5, STILL LIVE

**Core 1 runs from flash, and `apps/handoff` has BTstack on core 0 using the
same flash.** The whole core-1 path is `HANDOFF_HOT_FUNC` and runs from RAM.

1. **Every core-1 budget taken in `linktest` is a lower bound.** Measure in
   `handoff`.
2. **`hal_pico_core1_load()` is since boot.** Use `hal_pico_core1_busy()`.
3. **Overruns at low load mean a stall, not a shortage.**
4. If you add anything to core 1, mark it `HANDOFF_HOT_FUNC`.

## START AT STEP 8 — SKIN, ON CELLS

Brief §9, design §13. **This is the last step, and nothing above is proven
until it passes**: every reading so far is USB-tethered, both boards share the
PC ground, and that wire is the return path under test.

- Both boards **floating, on cells. USB out. TX gated at SW1.**
- Plug USB back in **without touching SW1** to read the counters.
- Keep the bands apart when not deliberately in contact — the 24 Sep table
  result was 0 frames at the same spacing, and that is the proof the coupling
  is real.
- The ground electrode on J2 pin 2 is what fixed the dropouts. It must be on.

**Passes when:** the 447-frame worn result from 24 Sep is matched or beaten,
and the ~30 % FER is no worse.

### What a worn bench can read

`r` is wiped by every handshake, so everything below comes off the phone.

Both blocks now leave the band by radio and the app parses both — done
26 Sep 2026, after step 8, because the diagnostics the page was showing still
described v1's carrier floor:

- `ble_bench_t` — completions, frames good and bad, and the CFAR pair. The pair
  is **signal against `k * noise`**, not a level against a floor; there is no
  floor, no gate and no `min_delta` in this design.
- `ble_trig_t` — `beacons`, `peers`, `self_echoes`, the elections, and the
  **peak** signal of each interval. Read `peers` against `beacons`, and read
  one band's pair against the other's: the step-7 fault was an asymmetry, and
  no level reading would ever have shown it.

**Read the peak, not the instantaneous signal.** A beacon is on air 11 ms and
these blocks arrive twice a second, so `signal` samples the empty room roughly
two hundred windows out of two hundred and one — which is exactly how 25 Sep
drew a flat line under the threshold while the band was tripping its detector
seven times a second. `hal_pico_take_peak()` is a take and has one caller.

`scripts/blelog.py` reads both lines off the phones' logcat.

### What step 7 leaves you

Nothing is broken and nothing is half-built. The rendezvous number on the
bench (median 142 ms, mean 153 ms) is **pessimistic and should be read as
one**: the two boards re-arm within ~150 ms of each other after every
handshake, so they are partly in lockstep, which is exactly the collision the
random listen draw exists to break. The simulator's steady-state figure — two
bands already free-running, touched at every offset — is 81 ms mean.

Against v1, same simulator, same method, v1 from a `main` worktree: mean 64 →
86 ms, worst 152 → 145 ms. **22 ms on the mean for a trigger 2.8× the airtime**,
because a beacon is decided on its last chip.

**Design §2's one-second budget is still missed on one role**, 946 ms as the
faster and 1124 ms as the slower. That is the EXCHANGE, not the trigger — step
7 did not move it and was not going to. If that budget matters, the next thing
to look at is `frames_per_turn` and the carousel, not the rendezvous.

## INSTRUMENTS THAT NOW EXIST

| | |
|---|---|
| `f` | the clock tree, measured against the crystal |
| `y` | 2a: both tones measured, with the duty and the chip-word arithmetic |
| `y 0` / `y 1` | drive tone A / tone B unbroken |
| **`y 4`** | **the settle: drive, release, watch presence, with a trace of signal and noise. Silence the far board first.** |
| **`y 9`** | hands the pad back to v1 — **the LINK then cannot transmit** until `y 0/1/3` |
| `y 3` | tone A and tone B on ALTERNATE chips — the imbalance instrument |
| `y 2` | chip alignment by edge count. **Leaves the pad released** |
| `k [bin]` | move the **PROBE** Goertzel to any bin |
| `b` / `b 1` | walk the five design bins, or every bin below Nyquist |
| `n` | the core-1 budget: bank off, then on, in ONE image |
| `n 0` | bank off — **the RECEIVER is then off** |
| `n 2` | five bins in the SAME windows, guard median, ratio, linear/RAILED verdict |
| `p [s]` | presence: the busy FRACTION over an interval |
| `m` | the **probe** bin's level, mean and max per Goertzel window |
| `handoff n [0\|1]` | the bank off / on inside `handoff` |
| **`handoff s`** | **a trigger line: nonce, peers, self-echoes, redraws, refused draws** |
| **`trig nonce …`** | printed on leaving the trigger, with `v` on |
| `handoff_decode --v1` | replay a v1 OOK capture through the deleted slicer |

**The framer's counters now survive a re-arm.** `link_sm.c` calls
`frame_rx_reset()` where it used to call `frame_rx_init()`, which was zeroing
every counter on every contact and made `false_syncs` and the beacon counters
useless on a bench that re-arms every two seconds.

## BENCH

Boards: **93D1** = `4904EF1FFA2393D1`, **379E** = `36B9A3C84974379E`.
Identify by serial, never by COM port. On 25 Sep they were COM7 and COM8, both
carrying `apps/handoff` from this branch, plate to plate at 5 cm.

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target linktest
picotool load -x build-pcb/linktest.uf2 -f --ser 4904EF1FFA2393D1
```

`-x` goes **before** the filename, `--ser` **after**. picotool lives at
`~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`, is not on PATH, and the
home directory has a space in it — call it from PowerShell with `& $pt`.

**`scripts/link2.py --a 93D1 --b 379E --run N --at SEC:A:cmd`** drives both
consoles on one clock. A command with an argument must be **quoted**:
`--at "2:A:n 2"`. It opens the port once, so it **misses a boot banner**. Use
`--log` for anything over a minute and analyse the file, not the scrollback.

**`v` on at least one board for a long run.** The `trig nonce …` line is how
a rendezvous is accounted for, and without it the log says only that
handshakes happened.

**THE v1 YARDSTICK IS A WORKTREE, AND IT IS CHEAP.**
`git worktree add C:\work\handoff-main main`, build there, flash, measure,
flash back. It is the only way to get v1 and v2 at the same coupling. It works
for the SIMULATOR too — step 7's rendezvous comparison was taken by porting one
test into the worktree and running `scripts/test.py` there.

**A gap where both radios read FER 0 proves nothing.** Find the knee. On this
bench it is between 12 cm and 25 cm.

**`z` does not reset `false_syncs`** — it is the framer's lifetime counter, and
as of step 7 it really is lifetime rather than per-contact.

**A tone left driving survives the end of a `link2.py` run.** End with `y 9`,
and remember `y 9` stops the link transmitting.

**The coupling drifts on its own.** Take a matched quiet capture in the **same
run** as the tone capture it is compared against.

**Geometry is the only level control**, and **a hand near a band beats the
transmitter**.

**Bench order: `linktest` first, then `handoff`.** A DSP fault and a state
machine fault look identical from the `handoff` console.

## REGRESSION BASELINE, measured at step 7 on the plate path at 5 cm

| | |
|---|---|
| v2 link, `linktest` | good 465, crc 0, lost 0, **FER 0.0000**, BER 0, margin 584, overruns 0 |
| `apps/handoff` | **308 complete in 930 s** on each board, 0 partial, 0 abort, 0 overruns |
| alone | 774 beacons, 0 sends, 0 receives, 0 self-echoes |
| core 1 | 21–22 % in `handoff` |
| rendezvous | median 142 ms, mean 153 ms, max 385 ms (lockstep bench) |
| handshake | 946 ms faster role, 1124 ms slower |
| suite | **25850 checks**, 0 failures |

## HOUSE RULES

- **The user is dyslexic. Short, plain replies. Tables over paragraphs.**
- **Do not claim a step passed until the numbers are in the transcript.** The
  reading, or nothing.
- **Do not build past a step that has not passed.**
- Instruments anywhere are free. **Change what observes, not what does.**
- **A single number is not a measurement.** Step 7's settle was read as 40 ms
  until a trace showed the far board transmitting into the watch window, and
  its marker byte was picked on the right-sounding criterion until the phase
  sweep failed on it. Print the shape, not just the summary.
- **The bench needs hands sometimes, and the user is not always watching the
  chat.** When a band has to move, STOP and ask, then wait.
- The Bash wrapper mangles backslashes in heredocs and breaks on apostrophes.
  **Write Python to the scratchpad and run it with PowerShell.** It bit this
  session twice.
- Never name TP numbers — the silk has no TP labels.
- `python scripts/test.py` is green at **25850 checks**. Keep it green.
- `apps/turnaround` is **not built on this branch** and that is deliberate.
- v1 still works on `main`. If this branch dies at step 8, that is a
  **successful outcome** — write up why and stop.
