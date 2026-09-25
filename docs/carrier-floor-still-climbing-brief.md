# The carrier floor still climbs into the signal

A brief for the next session. Read this before touching `firmware/lib/dsp/carrier.c`.

**Status: NOT FIXED.** Three fixes were made on 25 Sep 2026 and all three are
real improvements, but the fault is still plainly visible on both boards and on
the phone's Advanced plot. Everything below is uncommitted on `main` at
`412c50d`.

---

## 0. How to work this. Read this part first.

**Start over. Do not start from the last session's conclusions.**

The last session fixed three real things and still did not fix the fault, and
twice announced a fix that the user could plainly see was not one. That is the
signature of patching a mechanism instead of understanding the chain. So:

1. **Read the whole signal chain top down before judging anything.** From the
   ADC ring through the Goertzel chip energies, into `carrier.c`, into the
   presence test, into `beacon.c`'s trigger, into `link_sm.c`. Sections 1 to 3
   below tell you what was measured and what was changed — they are evidence,
   not a diagnosis. Section 2 in particular is one session's hypothesis and it
   has already been wrong once. **Do not anchor on it.** It is placed after the
   measurements on purpose.

2. **Then name the fault to the user in plain English, before changing any
   code.** One or two short sentences, no jargon, no code. What is going wrong
   and why. Wait for them to agree it matches what they are seeing on the
   phone. They have watched this on real hardware for longer than you have, and
   both of the last session's wrong turns would have been caught here.

3. **The user is dyslexic. Keep every reply short and plain.** Tables over
   paragraphs. No long explanations unless asked.

4. **Measure before and after every change, and measure the trend, not the
   jitter.** See section 4. The last session's worst error was reading a
   per-second `floor ranged lo..hi` as stable while the floor climbed steadily
   underneath it.

5. **Do not claim a fix until the user confirms it on the plot.** The bench
   numbers and the phone plot disagreed all session, and the phone was right.

---

## 1. What you are being asked to fix

The floor is supposed to be an estimate of the channel with nobody
transmitting. Instead it climbs until it sits at a large fraction of the signal
it is meant to be measuring, and then the detector is deaf.

The last measurement of the session, both boards tethered, 1-second windows:

| | peak level | floor | gate¹ | can it hear? |
|---|---|---|---|---|
| 93D1 | 85–105 | 14–23 | ~69 | yes |
| 379E | 133–150 | **61–83** | **228** | **no** |

¹ gate = `max(3 × floor, floor + 24)`, from the presence test at the bottom of
`carrier_push()`.

379E's floor has reached more than half of its own peak level. Its gate is 228
against a level of 140, so it is deaf, and a deaf detector reads a frame as
quiet and averages it into the floor.

**Which board is in the bad state changes between runs.** Earlier the same
afternoon it was 93D1 at floor 51..130 while 379E sat at 5..5. So this is not
board-specific hardware — it is whichever board falls into the bad equilibrium.

---

## 2. One session's hypothesis, offered only after you have your own

Read section 0 point 1 again before you read this. What follows is where the
last session had got to when it ran out of road. It explains the measurements
in section 1, but it did not lead to a fix, so treat it as a lead and not as a
finding. If your own top-down read says something different, trust that.

**A floor that has climbed too high has no recovery path. Every backstop in the
file is built for the opposite fault.**

- `freeze_chips` needs `present` to arm. Presence needs the gate. The gate is
  unreachable once the floor is high. Dead.
- §4.3's quiet-wait cap (the only caller of `carrier_reprime()`) fires when the
  channel reads **busy** for too long. A floor that is too high reads
  **quiet**. It never fires. Dead.
- The trim added this session (below) has a threshold built out of the floor,
  so the higher the floor the less it trims. It fades out exactly when needed.

The file's own comment already states the asymmetry — *"a floor that is too low
is over-sensitive, one that is too high is deaf, and only one of those recovers
by itself"* — but nothing in the code enforces it. That is the gap.

**Suggested direction, not a decision: minimum statistics.** Track the running
minimum of `level` over a window of a few seconds and pull the floor down to
it, corrected up by a shift. Signal can only ever raise `level`, so a minimum
cannot be poisoned by signal, and it self-corrects downward from any state. The
existing prime already uses exactly this idea over 256 chips; the proposal is to
make it continuous rather than only at reprime. Check it against the five things
`the_floor_is_the_room_not_the_carrier()` in `test/host/test_beacon.c` pins
before committing to it.

---

## 3. What is already in the working tree

All uncommitted. `python scripts/test.py` is green: 25202 checks, 0 failures.

**`firmware/lib/dsp/carrier.c` / `.h` — three changes to the floor:**

1. **Trim.** A chip that is itself carrier-shaped is not averaged in. The test
   is the presence test applied to the raw `e` instead of the smoothed `level`,
   no hysteresis, no new constant. The `held >= freeze_chips` backstop is
   exempt.
   *Effect: real but partial. 379E (floor 5) went to 5..5 per second; 93D1
   (floor 55) still ranged 51..130, because its threshold had risen with it.*

2. **`rise_shift` = 10 against `slow_shift` = 8.** The floor rises 4x slower
   than it falls. 4x and not more, because a large asymmetry settles the floor
   near the room's MINIMUM again, which is the fault the file was rewritten to
   fix (see `carrier-floor-brief.md`).
   *Effect: 93D1's per-second range fell from 79 to 3..29.*

3. **The prime holds the old floor for the first quarter of its window.**
   `prime_min` starts at `0xFFFF`, so the first chip of a window set the floor
   to 4x that chip. Caught as `floor ranged 22..800` one second after a reboot.
   64 chips is 16 ms, longer than `HANDOFF_SHOUT_US`, so a shout cannot be the
   whole sample.
   *Effect: the 800 spike is gone.*

**`carrier_take_peak()` / `carrier_peak_t` — the instrument, and keep it.**
Returns peak `level`, the floor at that instant, and the floor's `hi`/`lo` over
the window, then clears. A single instantaneous reading cannot show any of this:
a shout is ~11 ms and a telemetry block leaves the band twice a second, so an
instantaneous plot is a flat line under the gate while the link works perfectly.
**This is how the session's earlier wrong conclusion was reached and then
corrected — measure the excursion, never the instant.**

**`firmware/test/host/test_beacon.c`** — test SIX added, one chip in eight at
70, sized so `level` never reaches the gate. Verified it fails with the trim
removed.

**`firmware/apps/handoff/handoff.c`** — `s` now prints a second carrier line:
`since last s: peak level N (floor M then); floor ranged lo..hi`.

**`firmware/lib/hal_pico/ble.h` — HALF DONE, nothing sends it yet.** Adds
`ble_trig_t` (tag `0xB2`, 20 bytes: the trigger counters plus peak level and
peak floor) and two control opcodes, `BLE_CTRL_ZERO_STATS` (0x0A) and
`BLE_CTRL_LINK` (0x0B). `handoff.c` has no `report_trig()` and does not handle
either opcode. Either finish it or revert that file; do not leave it ambiguous.

---

## 4. How to measure

**`waits` minus `short_carriers` is REAL SHOUTS HEARD**, from the `t` command.
It is the only number that answers "can these two hear each other", and
comparing it in both directions is what exposed a one-way link earlier in the
session (379E heard 2 of 93D1's 107 shouts; 93D1 heard 20 of 379E's 60). Do not
use `present` from a status print for this.

**The deaf test.** Toggle one band's link off with `g`, sample the other's `t`
counters over 15 s, toggle it back on, sample again. The difference in real
shouts heard is the answer. A scratch script that does this was written in the
session scratchpad and is not in the repo; it is ten lines of pyserial.

**Board id to COM port on Windows, without rebooting:**

```powershell
Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.Name -match 'USB Serial Device \(COM' } |
  ForEach-Object { "$($_.Name) -> $((Get-PnpDeviceProperty -InstanceId $_.DeviceID -KeyName 'DEVPKEY_Device_Parent').Data)" }
```

93D1 = `4904EF1FFA2393D1`, 379E = `36B9A3C84974379E`.

**Build and flash:**

```
python scripts/build.py --config Release --tx-pin 11 --build-dir build-pcb --target handoff
picotool load -x build-pcb/handoff.uf2 --ser <serial> -f
```

`-x` goes BEFORE the filename, device selection after. `scripts/build.py
--flash` cannot target one of two attached boards.

---

## 5. Two things that will mislead you

**Every reading in this brief is USB-tethered, and design §13 says that makes
it wrong as well as unsafe.** Both boards share the PC ground and that wire is
the return path under test. Numbers that look fine tethered have already been
measured as meaningless on 24 Sep 2026. The floor fault is visible tethered, so
tethered is good enough to iterate on — but nothing is proven until it runs on
cells.

**The phone plot samples every 500 ms** (`BENCH_TICK_MS`, `handoff.c`). The
floor's time constant is 256 chips at ~4000 chips/s, which is 64 ms. The floor
finishes moving between two dots, so it can only ever render as steps. The
level trace has the same problem and worse: it shows the instantaneous value,
so an 11 ms event is caught about one sample in twelve. Wiring
`carrier_take_peak()` into the bench block is what fixes the plot, and it is
part of the half-done BLE work above.

---

## 6. Where the rest of the story is

`docs/carrier-floor-brief.md` is the record of the previous floor redesign
(`5b8b55d`, 25 Sep 2026), which fixed the floor ratcheting DOWN onto the
minimum. This brief is the same day's discovery that it then ratcheted UP. The
two failure modes are opposite and the fix has to hold both: **a floor on the
room's mean, not on its minimum, and not on the carrier.**
