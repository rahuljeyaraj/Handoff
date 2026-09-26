# Handoff — prompt for the next session

Copy everything below the line into a new session.

---

Continue the Handoff PCB work. Repo C:\work\Handoff, branch main, clean at
4b7dc66.

Read the memory files skin-link-passed, handoff-elects-two-senders, pcb-bringup
and element14-blog first.

## WHERE IT STANDS

Two assembled boards. Board one = Pico 93D1, serial 4904EF1FFA2393D1, COM7.
Board two = 379E, serial 36B9A3C84974379E, COM8. Both hold apps/handoff built
for the PCB (`scripts/build.py --build-dir build-pcb --tx-pin 11 --target
handoff`). Both have a cell, SW1, JP5 bridged and their own insulated plate.

**The band works on a body.** 24 Sep 2026: both boards on cells, USB out of
both, plates on the left and right inner wrist — **17 complete handshakes on
each board**, both roles, white blinks then green on both bands. That settles
R1/R2 at 1 MOhm for good. Read the memory file; do not re-derive it and do not
re-open the resistor question.

**The handshake itself works** (801a756, four bugs, 24 Sep). Do not re-open the
election.

Bring-up steps 0-10 pass on both boards. Board one's motor buzzes — the old
"motor does not buzz" item was wrong and is closed.

The doc edits that were owed are done (4b7dc66).

## THE REAL DEADLINE

**element14 contest closes 23:59 UK on 27 Sep 2026.** It needs a **video and
photos**, and neither exists. Three days. The blog chapters, figures and
appendix are all committed already — see the element14-blog memory. Unless the
user says otherwise, this outranks everything below.

## OPEN, IN PRIORITY ORDER

1. **Video and photos for the contest.** Nothing captured yet.
2. **Step P, the mechanical half.** Print both enclosure halves
   (`hardware/enclosure/*.3mf`), press the four M3 inserts, screw the board
   down, fit the 22 mm strap. The plates are already made and taped; what is
   missing is the box. This is also what the video needs.
3. **Step 11 meter checks.** The user skipped these on 24 Sep as not worth the
   time, and the boards demonstrably run on their cells, so this is paperwork.
   Board one owes: cell in, SW1 on, USB plugged, **D1's un-banded end** stays
   at the cell while Pico pin 39 reads ~4.7 V. Then all of step 11 on board
   two.

## CARRIED, NOT CHASED

- `HANDOFF_TURNAROUND_US` (1 ms) is still the exchange's between-frame gap and
  is still the breadboard's M8 number. Never measured on the PCB.
- `overruns` is rarer than it looked. Board two showed 17 over 1924 s, but both
  boards showed **0** across the whole skin run after a `z`. Not chased.

## HOW TO WORK THE BENCH

- **`scripts/link2.py` is the instrument.** `--a`/`--b` take a COM port or a
  Pico serial tail (`--a 93D1`), `--at SECONDS:A|B:TEXT` schedules commands,
  `--log` saves. One board is fine with only `--a`.
- **Reading a test that runs with USB out.** Plugging USB back into a board
  already running on its cell does **not** reset it — VSYS just rises through
  D1 and the Pico keeps running. So `z` both boards on USB, unplug, run the
  test, plug back in **without touching SW1**, and read `s`. That is how the
  skin test was measured.
- **Prove a complete belongs to the test, not to the bench.** Read `s` twice a
  few seconds apart afterwards. If aborts climb while `complete` stays frozen,
  the bench is completing nothing now and every complete belongs to the test.
- **Confirm the channel before blaming firmware.** Run linktest_tx into
  linktest first.
- **`t 0|1`** hands out the roles by hand. **`v`** gives `trig silent/carrier`,
  **`s`** gives `carrier level/floor` and `framer syncs`.

## TRAPS

- **`r` is not "the last card received".** handoff.c clears the buffer at the
  start of every handshake end, so an abort wipes it, and two free-running
  boards abort within seconds. `r` printing "nothing received yet" after a good
  run is expected. Read the counters.
- **Turn the phone's Bluetooth OFF.** 93D1 is bonded to the Android app; the
  app connects ~20 s after boot and sends BLE_CTRL_FORGET, wiping the card.
  `w` rewrites a bench card in seconds.
- **`g` is a toggle**, and it is now the OPERATOR's half only: the link is on
  whenever a card is stored and `g` has not been used to turn it off, so a
  wiped band arms itself the moment the app provisions one and `w` alone is
  enough on the bench — a `g` after `w` turns it back OFF. Read the true state
  with `h`: it prints `link on / off (now ...)`, and "off: no card" is the
  wanted-on-but-empty state rather than a park.
- **A board can sit silent because its link is parked.** Board two answered
  nothing for two runs and looked dead; it was simply `now off`.
- **picotool's device selection goes AFTER the filename.**
  `picotool load -x <file> -f --ser <full serial>`. `scripts/build.py --flash`
  has no serial selector, so with two boards attached build without `--flash`
  and run picotool by hand. picotool lives at
  `~/.pico-sdk/picotool/2.3.1/picotool/picotool.exe`.
- **Swapping images costs the bench card**, and the bring-up image and the
  product image are different apps. Flash apps/bringup only when a meter check
  needs it, and put handoff back after.
- **LED backgrounds mask by priority.** Reboot between patterns.
- Board one does not carry the TP numbers on silk. Name probes by VREF, OUT1,
  PAD, or by Pico pin number.

## DECIDED, DO NOT REOPEN WITHOUT NEW EVIDENCE

**R1 and R2 stay at 1 MOhm.** A real skin path completed seventeen handshakes
per board on 24 Sep. Nothing is asking for more signal, and 100k would cost the
body-current limit that design section 13 rule 1 names.

## HOW TO WORK WITH ME

I am dyslexic. Short, plain replies. Small messages.
