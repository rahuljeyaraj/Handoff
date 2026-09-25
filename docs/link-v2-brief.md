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
