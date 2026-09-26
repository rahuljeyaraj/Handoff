# Next session: the plot is a percentage now, and two runs are owed

Written for whoever picks this up. The chart fault in the previous version of
this file is **fixed** — what is left is bench time with the bands coupled, and
a walk of the page on a phone.

## What the plot was doing, measured rather than reasoned

Both boards on the bench, both on USB, the peer's link switched **off** at its
console with `g` — so nothing at all was on the channel:

| reading | value |
|---|---|
| plotted signal (tone B, the peak of the interval) | 400–520 |
| plotted room (the guards, averaged) | 37 |
| plotted `needs` line, `sqrt(k) x room` | 151 |
| the detector's own verdict, over the same windows | **79 busy of 295 021 — 0.03 %** |

The picture said "hearing something" continuously. The detector said it heard
nothing 9 997 times in 10 000, and the detector was right: `peers` was 0 all
run. Two statistics on the same samples, four orders of magnitude apart.

**The leading hypothesis in the old brief was half right.** A peak compared
against a per-window `k` is indeed not a comparison — textbook noise puts the
peak of one interval at 2.9..3.6x the room against a line at 4.09x, so the line
carries none of the million-to-one margin its derivation claims
(`test_presence.c`, check ELEVEN, pins this). But that alone would not have put
the signal *three times* over the line. The rest was **our own transmitter**:

- The blanking added one window past pad-idle plus the turnaround. That covers a
  window straddling the edge, which is all it ever claimed to do, and leaves the
  first whole window after it in the listening pool. That window still holds our
  last chip.
- One window per beacon is one in ten thousand — invisible in a fraction, and
  **enough to own a maximum**. The peak the phone drew was this band's own
  beacon, about eight times a second, which is why tone B (200 kHz, the last
  chip) towered over tone A.

With the band's own link also off, the same board reads tone B at 120..135 —
under the line. That was the control that separated the two halves.

## What changed

**The chart is one line: the percentage of the band's listening time that its
detector called busy.** No threshold, no level axis, nothing to interpret.

- `ble_bank_t` is **version 2**, 20 bytes: the five bins plus `listen_windows`
  and `listen_busy`, cumulative since boot. The phone differences two blocks.
  A version 1 band is refused rather than drawn.
- The pair is counted **only over the windows `core1_tx_deaf()` says were the
  room's**, which is the same set the spectrum is taken from. `presence.c`'s own
  `windows`/`busy_windows` count every window and therefore count our own
  shouts: 24 % of all windows busy with the peer silent, against a beacon duty
  near 10 %.
- `CORE1_TX_TAIL_WINDOWS` is 2, not 1, with the measurement written beside it.
- The page's verdict line no longer compares a peak with `k` either. "Something
  on the skin" is now 0.5 % of listening time — fifteen times the measured empty
  channel, four times under one beacon in one interval.
- The five bins stay in the block and are **printed** under *More link numbers*.
  They are what to look at when the percentage surprises you.
- `scripts/blelog.py` logs the bank line now, because that is where the answer
  is on a worn run.
- Deleted: `BenchSample.threshold`, which multiplied an amplitude by `k` — the
  four-times-too-high bar, in a second place, with no reader.

## What is verified, and on what

Firmware, both boards, final image:

| state | listening | busy | chart |
|---|---|---|---|
| band beaconing, peer silent, 30 s | 446 423 | 53 | 0.0 % |
| band beaconing, peer shouting, 30 s | 444 403 | 92 | 0.0 % |

The second row reads zero **because the bands are not coupled where they sit on
the bench** — `peers` stayed 0 both times. That is the honest answer, and it is
also the hole in the verification: nobody has yet seen this number rise.

`scripts/test.py` is green at 25 868 checks.

## The two runs that are owed

1. **Make it rise.** Bands coupled — plates touching through a wire, or worn on
   two wrists off USB — and watch the chart leave zero before any card is
   exchanged. Expect the tens of per cent: one beacon is 11 ms of a 500 ms
   interval, so a peer that is only beaconing is about 2 % per shout and a
   handshake is far more. If it stays at zero with `peers` climbing, the gating
   is cutting too much and `CORE1_TX_TAIL_WINDOWS` is the first thing to look at.
2. **Walk the page on a phone.** The app builds and the APK is at
   `android/app/build/outputs/apk/debug/app-debug.apk`, but the bench phone
   dropped off USB before it could be installed, so *nothing on the phone side
   has been run against a band* — the version-2 parse, the differencing and the
   chart are unproven on a handset. Install, open Advanced, and check: the line
   sits on zero with the bands apart, *More link numbers* prints `listening` and
   `of them busy` climbing, and the console's own percentage agrees with the
   chart. `python scripts/link2.py --a 93D1 --run 30 --at 2:A:z --at 28:A:s`
   prints the same fraction the phone is drawing — they are the same two
   counters, so a disagreement is a bug in one of them.

## Bench recipe

```
cmake  C:\Users\Rahul Jeyaraj\.pico-sdk\cmake\v4.3.4\bin\cmake.exe
build  cmake --build C:\work\Handoff\build-pcb --target handoff -j 8
flash  picotool load -x build-pcb\handoff.uf2 -f --ser <serial>
       (--ser goes AFTER the filename)
       93D1 = 4904EF1FFA2393D1 (COM7)   379E = 36B9A3C84974379E (COM8)
app    JAVA_HOME=~/.handoff-toolchain/jdk/jdk-17.0.20.1+1
       cd android && ./gradlew.bat assembleDebug
       adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Walk to the page: launch, tap `998 203` (gear), tap `400 1560` (Advanced).
Read the blocks with `adb logcat -d -s BandClient:I`.

**The single most useful instrument** is still silencing one board and watching
the other: `g` on its serial console toggles the link off and on. Every fault in
this file was found that way — including this one, twice over, because `g` on
*both* boards is what separated our own tail from the room.

## Also outstanding, unrelated to the plot

Both phones' own cards should carry the screenshot details before the demo:

- **Rohit Menon** · +91 98765 00001 (Mobile) · rohit.menon@example.com ·
  Arclight Embedded · Firmware engineer
- **Savithri Raghavan** · +91 98765 00112 · savithri@example.com ·
  Expo registration · Front desk lead

Rohit's phone still has no ORG or TITLE and uses `rohit@gmail.com`.
