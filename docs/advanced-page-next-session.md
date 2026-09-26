# Next session: the body-link plot is still wrong

Written for whoever picks this up. Everything below is committed at `2b0281e`.

## The job

The Advanced page's chart does not represent the link honestly. Debug it
properly, then decide what the right picture is — you are **not** obliged to
keep the three lines that are there now. The page is being used to demonstrate
the link to a viewer, so the answer has to be readable by someone who has
never seen it before, and it has to be true.

## The symptom, exactly as observed

Bands are two PCBs on the bench, both on USB to the same PC.

| bands | what the chart does |
|---|---|
| far apart | signal sits **above** the `needs` line, continuously |
| close together | signal **zigzags** above and below the line |
| USB in or out | makes no difference to the above |

That is backwards. Far apart should be quiet — the signal under the line —
and close should be a clean crossing.

Handshakes **do** only complete when the bands are close, so the link itself
behaves correctly. It is the plotted statistic that is wrong, not the radio.

Do not repeat this session's mistake of claiming distance cannot matter on a
USB-tethered bench. It does. The operator has watched it.

## Leading hypothesis: a maximum compared against a per-window threshold

This is where to start, and it explains the "far apart is always above" half
directly.

`ble_bank_t` reports **the loudest window of the whole interval**. At
`HANDOFF_WINDOW_RATE_HZ` = 20 000 windows/s and two blocks a second, each
reported number is the maximum of about **10 000 windows**.

The threshold it is drawn against is `sqrt(k) × room`, where `k` comes from
`HANDOFF_CFAR_K_NUM / _K_DEN` — and that k is solved in `presence.h` from a
false-busy rate **per window decision**. A k chosen so that a *single* window
crosses rarely says nothing about whether the *maximum of ten thousand* of
them crosses. The extreme value of a noise distribution sits well above its
mean by construction, so a peak-versus-mean comparison at a per-window k will
read "busy" on an empty channel more or less always.

Measured just before the handover, bands close: signal 490, needs 254,
room 62. Note 490/62 ≈ 8 in amplitude with no peer contribution assumed —
check what that ratio is with the peer's link switched off (`g` on its
console) and see whether it stays near 8. **If it does, the hypothesis is
confirmed and the rest of the chart discussion is moot until it is fixed.**

### The second half of the symptom

Why *close* is worse than far is probably the blanking. When the bands are
close they handshake, so the band transmits far more, and
`core1_tx_deaf()` removes every window where our own pad was driven. The peak
is then taken over a smaller and differently-distributed pool of windows, so
it moves around. That would make the close-together case noisier than the
far-apart case, which is what is seen.

Both halves come from the same root: **a peak is being compared against a
threshold that was never derived for a peak.**

## The instrument I would reach for instead

`presence_t` already counts `windows` and `busy_windows`, and
`hal_pico_presence()` already hands both out. The **busy fraction over an
interval** — the delta of `busy_windows` divided by the delta of `windows` —
is:

- the detector's own per-window verdict, so the k it uses is the k it was
  solved for, with no statistical mismatch to reason about;
- a number between 0 and 100 %, which needs no threshold line drawn at all
  and no axis a viewer has to interpret;
- monotonic with how well the two bands are coupled, which is exactly the
  thing the demo is trying to show.

Bands apart should read near 0 %. Bands together should climb. One line, one
axis, no second series to explain. Consider it seriously before rebuilding the
three-line plot.

It is not carried in `ble_bench_t` today — that block is full at 20 bytes —
but `ble_bank_t` is only 12 and is the natural place for it, or for the two
counters it is computed from. **Send the counters, not the fraction**, so the
reader can difference them over whatever window it chose; that is the rule
every other cumulative counter on this band follows.

## What is already fixed — do not re-debug these

Three real faults were found and fixed this session. Each was verified on the
bench, and all three are load-bearing for whatever you build next.

1. **Only the first telemetry notify per tick landed.** `att_server_notify` is
   not queued (`ble.c` says why), so bench + trig + bank sent back to back
   took the one free ACL buffer and dropped the other two silently. Measured:
   63 bench blocks in thirty seconds, **zero** trigger blocks. The Rendezvous
   section had therefore never filled in on any bench in the project's
   history. Now one block per tick, rotating, `BENCH_TICK_MS` 500 → 167.

2. **The band was hearing its own transmitter.** Tone A sat at ~960 with the
   peer's link switched off. The blanking asked "is the pad driven *now*", but
   core 1 scores samples up to a DMA block old. It is now placed on the sample
   clock via `adc_ring_sample_us()`, the way `p_rx_chips()` has cut chips
   since M13. Tone A fell to ~80. See `core1_tx_deaf()` in `hal_pico.c` — the
   comment there is the full account.

3. **The threshold was drawn 4× too high.** `presence.c` decides on mag²
   (`signal² > k · noise²`); what leaves the band is the amplitude, root
   already taken. The app multiplied the amplitude by k, drawing the bar
   √16.76 = 4.09× too high. From the v2 merge until now the band would call a
   channel busy while the phone showed the signal far below its own line —
   and that is why v1's level-and-floor plot visibly crossed and v2's never
   did. `BandBench.amplitudeThreshold` / `powerRatioClears` in `Gatt.kt`.

A fourth, smaller one: taking all five bins at the loudest-*tone* window made
the room pulse **inversely** to the signal, because in a quiet interval the
loudest-tone window is simply the noisiest window and its guards are high too.
Tones are now a peak over the interval and guards a mean. Keep that split
whatever else changes — see the `s_binmean` comment.

## Where things are

**Firmware**
- `firmware/lib/hal_pico/ble.h` — `ble_bank_t`, tag `0xB3`, version 1, 12 bytes
- `firmware/lib/hal_pico/hal_pico.c` — `s_binpeak` / `s_binmean`,
  `core1_tx_deaf()`, the seqlock on `s_binpeak_seq`
- `firmware/lib/hal_pico/hal_pico.h` — `hal_pico_take_bin_peak()`, and why it
  does not block where `hal_pico_take_peak()` does
- `firmware/apps/handoff/handoff.c` — `report_bank()`, `bench_tick()`'s rotation

**App**
- `ble/Gatt.kt` — `BandBank`, and the amplitude/power threshold helpers
- `ble/BenchTrace.kt` — `BenchSample.bank`, `withBank()`, the `measured` gate
- `ui/components/BenchChart.kt` — the three-line chart
- `ui/screens/AdvancedScreen.kt` — the page, the verdict states, `More`

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

**The single most useful instrument** is silencing one board and watching the
other: `g` on its serial console toggles the link off and on. Every fault
above was found that way — if a number does not move when the peer goes
silent, that number is not measuring the peer.

`scripts/test.py` is green at 25 860 checks; keep it that way.

## Also outstanding, unrelated to the plot

Both phones' own cards should carry the screenshot details before the demo:

- **Rohit Menon** · +91 98765 00001 (Mobile) · rohit.menon@example.com ·
  Arclight Embedded · Firmware engineer
- **Savithri Raghavan** · +91 98765 00112 · savithri@example.com ·
  Expo registration · Front desk lead

Only Rohit's phone was attached this session, and its card currently has no
ORG or TITLE and uses `rohit@gmail.com`.
