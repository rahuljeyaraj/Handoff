# The carrier detector's noise floor — what it was, and what it is now

Written 25 Sep 2026, replacing the redesign brief of the same name. The brief
asked for a floor that does not collapse, does not climb into a carrier, and
survives being thrown away mid-frame. That is done and the suite is green. What
is below is the answer, the measurements behind it, and the one thing left,
which is the bench.

---

## What was wrong

`carrier.c` computed its floor as

    floor += (e - floor) >> slow_shift          /* slow_shift = 11 */

and its own comment called that a slow symmetric average. It was not one.
Ambient chip energies sit a few LSB either side of the floor, so the shifted
delta was `-1` or `0` and never `+1`: a dip of one LSB subtracted a whole 1
while a rise added nothing until the chip was 2048 above. **The floor could only
walk downward**, and it walked down until it sat on the *minimum* of whatever it
was watching — on ambient noise, and on a loud carrier too. Where ambient
reaches zero it reached the clamp at 1, the ratio test `level * 8 > floor * 24`
became `level > 3`, and `min_delta` was the only gate left: a fixed threshold at
`level > 25` wearing an ambient tracker's clothes.

The ratchet was also load-bearing, which is why it could not simply be fixed.
`carrier_reset()` is called at four sites during a handshake, and `push()`
re-primed both level **and floor** from the next chip after one. Mid-frame that
chip is a Manchester chip, high half the time, so the floor primed at the
carrier's own level. An honest average then needed ~2048 chips — 512 ms —
against a rendezvous budget of 464 ms, and never recovered. The downward ratchet
dragged a poisoned floor back in about 200 chips, and that accident was what
made rendezvous work.

## What it is now

Four changes, and they only work together.

1. **The floor carries fractional bits.** `floor_acc` holds it shifted left by
   `FLOOR_FRAC` (12), so the average can move by less than one LSB per chip
   instead of rounding the move away. This is the actual fix for the ratchet.
   The rounding *direction* stops mattering once the fraction exists: rounding
   the step toward zero instead was tried and no test can tell the two apart, so
   the plain shift stands. `slow_shift` must stay at or below `FLOOR_FRAC`.
2. **The floor is a property of the room, not of the state machine.**
   `carrier_reset()` clears presence and re-primes `level` alone — `level` is
   meant to be what is on the channel right now, and after a transmit turn its
   last value is our own shout. The floor is untouched. A new
   `carrier_reprime()` is the only way to throw it away, and §4.3's quiet-wait
   cap is its only caller.
3. **The floor learns only while nothing is present.** An average that runs
   through a frame converges on the carrier's own mean, and a detector whose
   floor is the carrier's mean cannot hear it. The freeze is bounded at
   `freeze_chips` = 12000 as a backstop against a latched detector, which is
   far above the ~2000 chips of unbroken presence a whole handshake produces.
4. **The prime is a minimum over a window, corrected up by `prime_shift`.** A
   Manchester high cannot set a minimum, because the chip after it is a low one
   — mid-frame the window is down at the carrier's quiet half within two chips.

`slow_shift` came 11 → 8 (~256 chips, ~64 ms). It no longer has to be slow
enough to avoid meeting a carrier, because the freeze does that job.

### The window length is the part that is easy to get wrong

`prime_chips` is **256**, and it is set by needing silence in the window, not by
any settling argument. **A shout is flat tone, not Manchester** — about 40 chips
of it with no quiet chip anywhere. With a 32-chip window the phase sweep lost
three offsets outright: the band whose prime window landed inside its peer's
shout primed its floor at 772, went deaf, and its floor then settled on the
frames it could no longer hear. 256 chips is longer than a shout and than the
quiet-wait cap, and a peer's listen window is 200 chips at its shortest, so a
peer cannot fill one however the two cycles line up.

## Measured

`python scripts/test.py` on `main`:

| | before | after |
|---|---|---|
| failures | 1 of 25193 | **0 of 25200** |
| rendezvous, worst | 131100 us | **130900 us** |
| phases past one cycle | 6 of 117 | 6 of 117 |
| simultaneous start | 399/400 within two rounds | 399/400 within two rounds |
| handshake after rendezvous | worst 3 frames | worst 3 frames |

The BER sweep is unchanged (it does not run the detector). The Pico build is
clean.

**§5.1 is answered by making the question stop mattering.** Adding
`carrier_reset()` back to the receive path was measured against the whole
suite: 0 failures either way, the same worst rendezvous, the same 399/400. It
is left out because it buys nothing, not because it is dangerous.

`test_beacon.c`'s "re-priming the carrier detector mid-frame is not safe" was
pinning the old asymmetry and has been replaced by "the floor tracks the room,
not the carrier", which pins the five properties above. Two negative controls
were run against the new test:

- the old whole-number floor: check one fails with *floor settled at 6 on
  ambient whose mean is 13* — the ratchet lands on the minimum.
- the fraction kept but the step rounded toward zero: passes, which is how the
  ternary was shown to earn nothing.

## On the boards — done 25 Sep 2026, tethered

Both boards flashed with `build-pcb/handoff.uf2` and run through
`scripts/link2.py --a 93D1 --b 379E`.

**The floor moves both ways, which is the whole point.** Sampled over two runs:

| | floor, in order |
|---|---|
| 93D1 | 70, 73, 82, 79, 77, 80, 74, 77 |
| 379E | 53, 49, 49, 53, 57, 50, 56, 57 |

Against the old behaviour on 379E, which only ever fell: 15, 11, 9, 8, 7, 6.
The two boards settle at different floors, and they are the right ones — 93D1
is the noisy one and sits near 77, 379E near 53.

**The ratio test is doing real work for the first time.** 93D1 printed `level 78
floor 74 present 0`. With the old floor pinned at 1 the gate was `level > 25`,
so 93D1's own idle read as a carrier; now it does not. `short` — carriers heard
but rejected by `HANDOFF_SHOUT_MIN_US` — stayed at 0 on 93D1 across the runs.

**The link is not regressed.** 9 handshakes in 26 s, every one `COMPLETE`, roles
alternating, `frags 3/3` and `2/2`, `bad 0` on 8 of 9, `retries 0` on 8 of 9,
about 1.0–1.2 s each, both cards decoded each time.

`ratio_num` and `min_delta` were live for the first time here and **24/24 still
fit**: at a floor of 77 the gates are 231 and 101, against a carrier reading
426. No retune needed.

## Open — the worn run

Everything above was USB-tethered, so those floors are the tethered ambient and
not the worn one, and the coupling was the bench's rather than a body's. Design
§13 rule 1 still has to be run: both bands on their own cells, USB out of both,
one band per wrist. A tethered run cannot answer it.

One thing the host suite cannot see: the PCB's amplifier is still above the
detector's threshold about 3.2 ms after the pad goes idle. The floor no longer
climbs during that tail, so presence now holds for longer after our own
transmission than it used to. `HANDOFF_TRIG_SETTLE_US` (6 ms) covers the trigger
path; the exchange's `HANDOFF_TURNAROUND_US` is still 1 ms and still the
breadboard's M8 number. Watch for a receiver that thinks the channel is busy
right after its own turn.

## Traps that carry forward

- **`picotool`'s device selection goes AFTER the filename.**
  `picotool load -x build-pcb/handoff.uf2 -f --ser 36B9A3C84974379E`.
  It lives at `~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`.
- Board one = 93D1, serial `4904EF1FFA2393D1`. Board two = 379E, serial
  `36B9A3C84974379E`. Address them by serial tail, not COM port.
- Turn the phone's Bluetooth off or the app wipes 93D1's card.
- `z` does not reset `false_syncs`; read every false-sync figure as a difference.
- **Do not re-open R1/R2.** They stay at 1 MΩ.
- **JP7 on 379E was unsoldered** and that cost 25 Sep. All of JP1–JP8 are real
  and on the built board; only `floorplan.svg` is out of date. For normal
  operation JP1–JP7 must be bridged; JP8 is the M6 role strap and stays open.
- **`linktest`'s `x` self loop cannot see an open JP7.** A loud self loop proves
  R1, the pad node, the amplifier, the ADC and the DSP — not that the pad
  reaches the amplifier.
- **Never name a probe point as TP1..TP8.** The silk does not carry the numbers.
  Say the signal (GND, ADC0, OUT1, VREF, PAD, VSYS, AFE_3V3) or the part and
  which end (J2's square hole, the outer end of R1, Pico pin 39).

## The other deadline

The element14 contest closes **23:59 UK on 27 Sep 2026** and still needs a video
and photos. That outranks the bench items above. The handshake works today; the
floor was correctness, not a blocker.
