# The carrier detector's noise floor — redesign brief

Written for the next session. Copy everything below the line into a fresh
session. Everything under **Measured** is data with the run that produced it;
everything under **Open** is not.

---

Continue the Handoff work. Repo `C:\work\Handoff`, branch `main`, clean at
`9350cb8`.

Read the memory files `carrier-floor-ratchet`, `linktest-self-loop`,
`handoff-elects-two-senders`, `never-say-tp-numbers` and `short-replies` first.
Then read `firmware/lib/dsp/carrier.c` — the long comment in `carrier_push()` is
the finding, written beside the line it is about.

## The job

`python scripts/test.py` is **red on `main`: 1 failure in 25193 checks.** It has
been red since `dede949`, which was committed with it already failing.

    FAIL trigger: re-priming the carrier detector mid-frame is not safe
         (firmware/test/host/test_beacon.c:427)
         re-primed on a high chip, presence came back after 324 chips —
         carrier.c's floor has changed, so §5.1 is worth revisiting

Get the suite green **without** losing rendezvous. That means redesigning the
noise floor and the re-prime together. They are one problem, not two, and that
is the whole reason this brief exists.

## What is wrong

`carrier.c` computes its floor as

    floor += (e - floor) >> slow_shift          /* slow_shift = 11 */

and its own comment calls this a "slow SYMMETRIC average". It is not. `>>`
rounds toward minus infinity, so a dip of one LSB below the floor subtracts a
whole 1, while a rise adds 1 only at 2048 above. **The floor can only ever walk
downward**, and it walks to the clamp at 1 from any starting point — on ambient
noise, and on a loud carrier too.

With the floor pinned at 1 the ratio test `level * 8 > floor * 24` becomes
`level > 3`, always true. `min_delta` is the only gate left standing, so the
detector is in practice a **fixed threshold at `level > 25`**.

`dede949` moved `slow_shift` 7 → 11, which made the ratchet 16× stronger.

## Measured, 25 Sep 2026

### The ratchet is real, on hardware

Board 379E running `apps/handoff`, `s` sampled once a second. The floor column,
in order, never rising:

    15, 11, 9, 8, 7, 6, 8, 7, 6, 8, 7, 6 ...

### The ratchet is real, in simulation

The update rule replayed against this board's own measured chip energies
(379E idle: mean 48, peaks to 439; a loud carrier: mean 400, peaks to 850):

| Input | Floor after a few thousand chips |
|---|---|
| Ambient noise | **1** |
| A loud carrier | **1** |
| Same ambient, average done properly | 52 |

### Fixing it properly breaks rendezvous — 1 failure becomes 25

Tried and reverted, both of them:

1. A `floor_acc` carried at 2^`slow_shift` times the floor, so the fraction that
   `>>` throws away is kept instead. A true symmetric average.
2. The same, **plus** freezing the average while `present` is true, so a shout
   cannot become the floor it is measured against.

Both give identical damage:

| | Before | After either fix |
|---|---|---|
| Failures | 1 | **25** |
| Rendezvous, worst | 131100 us | **464000 us (the bound — it never happens)** |
| Phases with no rendezvous | 0 of 117 | **10 of 117** |
| `test_link` failures | 0 | 4 |

### Why — this is the part that matters

`carrier_reset()` is called at four sites in `link_sm.c` during a handshake:
becoming sender (`enter_exchange`), `link_sm_idle`, `suspect_collision`, and the
trig quiet-wait cap. After a reset, `carrier_push()` re-primes the floor **from
the next chip it sees**. Mid-frame that chip is a Manchester chip, so it is high
half the time, and the floor primes at the carrier's own level.

An honest average then needs ~2048 chips to come back down. At 4000 chips/s that
is **512 ms**, against a rendezvous budget (`RENDEZVOUS_BOUND_US`) of **464 ms**.
It never recovers, so the peer's shout is never heard.

The downward ratchet drags a poisoned floor back in about 200 chips — 50 ms.
**That accident is what makes rendezvous work today.** The failing test is the
thing that watches this exact behaviour, and it fails precisely because the
ratchet lets presence return mid-frame after 324 chips.

## The constraints any design has to satisfy at once

1. The floor must not collapse to 1, or the ratio test is dead and only
   `min_delta` is protecting the detector from ambient noise bursts. 379E's idle
   peaks reached 439 LSB today.
2. The floor must not rise to meet a carrier either, or the detector goes deaf to
   the thing it exists to hear. A purely symmetric average does exactly this —
   simulated, the floor reaches 400 on a mean-400 carrier and `present` never
   fires.
3. A floor poisoned by a mid-frame re-prime must recover in **well under 464 ms**,
   and ideally inside one frame.
4. It must barely move across a 32-chip preamble, which is the window
   listen-before-talk has to decide in. That is the original comment's argument
   and it still stands.

## Directions worth trying — none of these has been tested

- **Do not prime the floor from a single chip.** Prime it from a short median or
  a minimum over N chips, so a Manchester high cannot set it. This attacks the
  root rather than the symptom, and constraints 1 and 3 stop fighting.
- **Do not reset the floor at all** on the paths that currently do. Reset
  `present`, `hold` and the framer, and let the floor carry across. Check each of
  the four sites in `link_sm.c` separately — `enter_exchange` says in a comment
  that the decision is deliberately not the same on both paths.
- **Separate rise and fall rates honestly**, both with scaled accumulators so
  neither ratchets: slow up, faster down. Note this converges to a low quantile
  rather than the mean, which is the min-chaser the original comment warns
  against — so it needs the freeze from constraint 2 to be safe.
- **Retune `ratio_num` and `min_delta`.** They were tuned against a floor that
  is effectively always 1, so they are really one fixed threshold. With a real
  floor they are both live for the first time and almost certainly wrong.
- **Consider whether the floor should exist.** The detector demonstrably works
  as `level > 25`. If the answer is a fixed threshold plus a slow calibration,
  say so in the design doc and delete the rest rather than keeping a number that
  does nothing.

## How to validate

1. `python scripts/test.py` — must reach **0 failures**, and `rendezvous: worst`
   must not regress past 131100 us. Watch `simultaneous start` too; it is
   currently 399/400 within two rounds.
2. Then the bench, because the host channel model is not the room.
   `python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb`,
   flash both boards, and check the `s` line's floor actually tracks — it should
   move **both ways** now, and sit near the idle chip energy `m` reports.
3. Then a worn run. Design §13 rule 1: both bands on their own cells, USB out of
   both, one band per wrist. A tethered run cannot answer this.

## Traps

- **`picotool`'s device selection goes AFTER the filename.**
  `picotool load -x build-pcb/handoff.uf2 -f --ser 36B9A3C84974379E`.
  It lives at `~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`.
- Board one = 93D1, serial `4904EF1FFA2393D1`. Board two = 379E, serial
  `36B9A3C84974379E`. Address them by serial tail, not COM port.
- **`scripts/link2.py` is the instrument.** `--a 93D1 --b 379E --run 40
  --at "2:A:s"`, and `--log` saves.
- Turn the phone's Bluetooth off or the app wipes 93D1's card.
- `z` does not reset `false_syncs`; read every false-sync figure as a difference.
- **Do not re-open R1/R2.** They stay at 1 MΩ. Seventeen handshakes per board
  through real skin on 24 Sep settled it.
- **JP7 on 379E was unsoldered** and that cost 25 Sep. All of JP1–JP7 are real
  and on the built board; only `floorplan.svg` is out of date. For normal
  operation JP1, JP2, JP3 (centre–pad 3), JP4, JP5, JP6 and JP7 must be bridged;
  JP8 is the M6 role strap and stays open.
- **`linktest`'s `x` self loop cannot see an open JP7.** The transmitter sits on
  the pad side of the gap, so stray capacitance across an open jumper plus a
  gain of 121 still reads loud. It proves R1, the pad node, the amplifier, the
  ADC and the DSP — not that the pad reaches the amplifier.

## Working with this owner

- **The owner is dyslexic.** Short, plain, numbered replies. No code in
  explanations.
- **Never name a probe point as TP1..TP8.** The silk does not carry the numbers.
  Say the signal (GND, ADC0, OUT1, VREF, PAD, VSYS, AFE_3V3) or the part and
  which end (J2's square hole, the outer end of R1, Pico pin 39).
- **You run the tests, not the owner.** Flash, launch and read the boards
  yourself; the owner does the physical steps only.
- Ask before a timed run and wait — runs have been wasted with the bands sitting
  on the bench.

## Open

1. All of it. Nothing in **Directions** has been tried.
2. Whether architecture §5.1's question — should the receive path reset the
   carrier detector — changes answer once the floor is honest. The failing test
   says to revisit it, and it is the same question as this brief.

## The other deadline

The element14 contest closes **23:59 UK on 27 Sep 2026** and still needs a video
and photos. Unless the owner says otherwise, that outranks this brief. The
handshake works today; this is correctness, not a blocker.
