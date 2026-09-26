# Next session: the plot is a percentage now, and one run is owed

Written for whoever picks this up. The chart fault in the previous version of
this file is **fixed**, on the band and on the phone. What is left is one bench
run with the two bands actually coupled.

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
the bench** — `peers` stayed 0 both times. That is the honest answer, and the
hole it leaves in the verification is closed only halfway below.

`scripts/test.py` is green at 25 868 checks.

## The phone, walked

Installed on the bench phone and walked to Advanced against board one:

- The verdict reads **Nothing on the skin**, where the old chart's own test read
  *Something on the skin* forever.
- The line sits on zero, `heard 0.0% of the time`, axis top 20 %.
- *More link numbers* prints `listening 5 893 922 windows`, `of them busy 1057`,
  and `heard 0.01 % of the time`, beside the five bins — tones 200 / 108 against
  guards 47 / 38 / 39, which is what a band that is no longer hearing its own
  tail looks like.
- Cross-checked against the console five seconds earlier: `1082 / 6 107 559`
  there, `1095 / 6 175 635` on the phone. Same two counters, same scale.

**And the chain was proved to move.** A throwaway image with the blanking
disabled (`s_tx_deaf_span = 0`, never committed) makes the band hear its own
beacon: the chart went to **27.2 %**, the axis stepped to 40 %, the line drew a
live series, and the verdict flipped to *Something on the skin, but no card in
it* — which is exactly right, the energy is real and carries no card this band
can decode. The honest image was flashed straight back and reads 0.01 % again.

So the differencing, the scaling, the axis and the verdict floor are all
verified with a signal of known size. What is **not** verified is the one thing
hands are needed for:

## The run that is owed

**Make it rise off the other band.** Bands coupled — plates bridged, or worn on
two wrists, off USB and gated at SW1 — and watch the chart leave zero before any
card is exchanged. Expect the tens of per cent: one beacon is 11 ms of a 500 ms
interval, so a peer that is only beaconing is about 2 % per shout and a
handshake is far more. If it stays at zero while `peers` climbs, the gating is
cutting too much and `CORE1_TX_TAIL_WINDOWS` is the first thing to look at.

`python scripts/link2.py --a 93D1 --run 30 --at 2:A:z --at 28:A:s` prints the
same fraction the phone is drawing — the same two counters, so a disagreement
is a bug in one of them.

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
