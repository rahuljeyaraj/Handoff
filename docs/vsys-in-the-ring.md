# Reading VSYS without taking the ADC away from the receiver

Implementation brief. Written 15 Sep 2026, after `9298bdf` ("power: stop VSYS
sampling from taking the ADC away from the receiver") stopped the damage but
left the band unable to measure its own battery.

This document replaces the "the real fix is..." paragraph at the head of
[`firmware/lib/hal_pico/power.c`](../firmware/lib/hal_pico/power.c). That
paragraph guessed that core 1 would have to hold the CYW43 lock. It does not,
and it must not — see §3.4.

---

## 1. What is broken

`power_vsys_mv()` used to be a straight copy of
`pico-examples/adc/read_vsys/power_status.c`. That was correct until M12 gave
core 1 a free-running DMA ring on the same converter.

The old body did four things, in order:

1. `adc_init()` — reset the whole ADC block.
2. `adc_select_input(3)` — pointed the mux at GP29.
3. `adc_fifo_setup(true, false, 0, false, false)` — reconfigured the FIFO and,
   critically, **turned DREQ off**, so the DMA engine stopped being told there
   was data waiting.
4. `adc_run(false)` at the end — and nothing ever started it again.

Step 4 is the fatal one. With conversions stopped the FIFO stops filling, the
DMA never completes a block, `s_full[]` never goes true, and core 1's loop in
[`hal_pico.c:92`](../firmware/lib/hal_pico/hal_pico.c#L92) spins on a block
that will never arrive. Forever. Even had something restarted it, the mux was
left on channel 3 and DREQ was left off, so three separate things needed
undoing and none of them were.

The tell on the bench was a frozen `chips` count and a *decaying* measured
sample rate — 437 k, 234 k, 160 k, 121 k — which is a frozen numerator over a
still-growing clock, not a slow pipeline.

`9298bdf` made `power_vsys_mv()` return 0 while the ring is running. Nothing
breaks any more, but the product image now never measures its battery, and 0
is being misread downstream as "fine" (§8).

## 2. What the fix has to respect

Four facts, each verified in the tree, that between them determine the design.

### 2.1 Swapping the ring's input channel in flight is already proven

`adc_ring_noise_floor()` at
[`adc_ring.c:212`](../firmware/lib/hal_pico/adc_ring.c#L212) points the running
ring at the on-die temperature sensor, takes a block, and points it back. It
touches nothing but `AINSEL`. The converter never stops, the FIFO is never
reconfigured, DREQ stays on, the DMA keeps draining. It runs at every boot
(`hal_pico.c:293`) and in two bench apps.

That is the shape of the fix, and it is the exact inverse of what the old
`power_vsys_mv()` did.

Caveat to be aware of: `noise_floor()` is only ever called *before* core 1
launches, so "swap while core 1 is consuming" is not yet proven on hardware.
Nothing about the ADC changes; only who is calling `adc_ring_next_block()`.
Under this design core 1 does the swap itself, so there is no concurrency at
all — but it is still the thing to watch for on the bench (§11).

### 2.2 Stopping and restarting the ring is not merely wasteful — it is wrong

`adc_ring_start()` resets `s_t0`, `s_blocks`, `s_seq[]` and `s_next_read` to
zero. `adc_ring_sample_us()` builds the sample clock from `s_t0`, and M14's
own-send cutting in `p_rx_chips` works by sample index. A stop/start would
jump that timebase backwards mid-flight, and the band would go back to hearing
its own shouts and waking on itself every cycle — the one thing
[`handoff.c:35`](../firmware/apps/handoff/handoff.c#L35) says the M14 binding
had to get right.

**So "briefly borrow the converter and give it back" is off the table.** It has
to be a swap in place, with the block counter and the sample clock running
through it untouched.

### 2.3 The CYW43 driver restores GP29 by itself

GP29 is ADC3 *and* the CYW43's SPI clock (`hardware/README.md:327`).
`adc_gpio_init(29)` puts the pad in analogue mode, which clears IE and sets OD
and takes the function away from PIO.

In the SDK, `start_spi_comms()` runs at the head of **every**
`cyw43_spi_transfer()` and does `gpio_set_function(CYW43_PIN_WL_CLOCK, ...)`
plus `gpio_pull_down()`. `gpio_set_function()` also restores IE and clears OD.
So the driver heals the pin on its next transaction, which is why
`pico-examples` never restores it either.

What we must guarantee is only that **no CYW43 transaction overlaps the window
in which GP29 is analogue.** That is what the lock buys, and it is all it
needs to buy.

Do **not** try to restore GP29 by hand: the correct funcsel is
`pio_get_funcsel(bus_data->pio)`, which is private to the driver, and guessing
it is fragile across SDK versions.

### 2.4 Core 1 must never take the CYW43 lock

`pico_cyw43_arch_none` still compiles with
`PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1`, so `cyw43_thread_enter()` is
`recursive_mutex_enter_blocking()` on the async context's mutex. The SDK
explicitly supports calling it from either core
(`async_context_threadsafe_background.c:74` — "called in pensv context and on
either core"), so it *would* work.

It must not be used anyway. Core 1 is the sole consumer of a ring that
overruns in 4.096 ms. Blocking it on a mutex core 0 may hold for milliseconds
of BTstack work is precisely the failure the architecture exists to avoid, and
there is no try-variant of `cyw43_thread_enter()` in the public API.

**The lock stays on core 0**, which is already allowed to block.

---

## 3. The design

### 3.1 The split

> Core 0 owns the **permission**. Core 1 owns the **converter**.

Core 0, from the BTstack context where it already lives, decides that now is a
safe moment, wakes and locks the CYW43, and posts a request. Core 1 sees the
request at its next loop iteration, swaps the ring's input to channel 3, lets a
block go by, averages the next one, swaps back, and publishes the answer. Core
0 sees the answer, releases the lock, and caches the value.

Core 1 never blocks, never takes a lock, never waits. With no request pending
it does exactly what it does today.

This is the same request/ack shape as `hal_pico_set_carrier()`
([`hal_pico.c`](../firmware/lib/hal_pico/hal_pico.c), `s_carrier_req` /
`s_carrier_ack`). Copy that idiom.

### 3.2 Core 1's state machine

Hangs off the existing block loop in `core1_main()`. Costs nothing when idle.

Let **T** be the moment core 1 notices the request. The DMA is partway through
some block X.

| Step | What happens | Block |
|---|---|---|
| T | `adc_gpio_init(29)`, `adc_select_input(3)` | — |
| T + d | X completes. Head is channel 0, tail is channel 3. **Discard.** | X |
| T + d + 4.096 ms | X+1 completes. All channel 3. **Measure.** Then `adc_select_input(0)`. | X+1 |
| T + d + 8.192 ms | X+2 completes. Head channel 3, tail channel 0. **Discard.** | X+2 |
| T + d + 8.192 ms | Publish the answer, `core1_dsp_init()`, resume normally | — |

`d` is however much of block X was left at T, so `d` ∈ [0, 4.096 ms]. Total
blind time is **up to 12.29 ms**, typically around 10 ms.

Three points that are easy to get wrong:

**Keep draining.** Core 1 must still call `adc_ring_next_block_seq()` for the
discarded blocks. It simply does not feed them onward. If it stops draining,
`s_full[]` stays true and the IRQ counts a real overrun — and `overruns` must
keep meaning what it has always meant.

**Skip the first 128 samples of the measured block.** If `d` is near zero, the
settling transient lands at the head of X+1 rather than in X. The Pico's VSYS
divider is 200 kΩ/100 kΩ, about 66 kΩ Thévenin, which is far above what the
ADC's sample-and-hold likes; `pico-examples` deems three discarded conversions
enough. 128 samples is 256 µs, some forty times that, and it costs nothing out
of 2048.

**Undo the DC centring.** `adc_ring_next_block_seq()` returns samples with
`ADC_MIDPOINT` (2048) subtracted. Add it back before converting, exactly as
`noise_floor()` does for `mean_code`.

Conversion is unchanged from the current code:

    mv = (mean_code * 3 * 3300) / 4096

12 bits against 3.3 V through the Pico's own 3:1 divider. VSYS at 3.9 V gives
about code 1614, so the DC-centred value is about −434 — comfortably inside
`int16_t`.

### 3.3 Why the detector must be re-initialised, not just skipped

This is the part that would fail silently if left out.

Feeding the detector a step to a DC level and a step back is a broadband
event. `goertzel.c` rejects steady DC at any bin, but not the edges. Left in
the stream, those two steps would look like signal, and a false chip would
reach the link state machine roughly once per measurement. Dropping the three
blocks removes the DC; calling `core1_dsp_init(&g, &sy, &car, hz)` afterwards
removes the edges' residue from the Goertzel, sync and carrier state.

That function already exists and is already called on a carrier change, so
this is reuse, not a new concept.

Also skip `tlm_usb_raw_feed()` for the three blocks — the raw telemetry stream
should not carry VSYS samples pretending to be pad samples.

### 3.4 Core 0's sequence

Order matters here.

1. Check the gate (§4). If it fails, return the cached value and stop.
2. `cyw43_thread_enter()`.
3. `cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN)` — wakes the CYW43. This is
   itself an SPI transfer, so it **must** happen here, before the request is
   posted. If it ran after the swap it would re-assert GP29 as the SPI clock
   and fight us.
4. Post the request.
5. Spin until core 1 answers, or the timeout expires (§7).
6. `cyw43_thread_exit()`.
7. Cache the value and the time it was taken.

Note that when this is called from a BTstack timer, the async-context lock is
*already* held recursively — the BTstack run loop on this platform is driven by
the same async context (`pico_btstack_cyw43` in
[`firmware/CMakeLists.txt`](../firmware/CMakeLists.txt)). So step 2 mostly
increments a count. The real cost is that BTstack is deferred for the duration,
not lock contention. Keep the call anyway: it is correct for any caller outside
that context, and it documents the requirement.

Is a ~12 ms deferral acceptable? The precedent is in
[`handoff.c:251`](../firmware/apps/handoff/handoff.c#L251): the flash write in
the same context takes "a few milliseconds with interrupts off and core 1
parked, which is within what a connection interval tolerates". 12 ms is longer,
but supervision timeouts are seconds, so the worst case is one missed
connection event and a BTstack retry. It happens at most once a minute (§5).

---

## 4. The gate

**Reuse the predicate that already exists.**
[`handoff.c:604`](../firmware/apps/handoff/handoff.c#L604) asks exactly this
question before re-provisioning the state machine mid-life:

    s_sm.state == LINK_IDLE || s_parked || !s_link_on

That is the repo's existing definition of "safe to disturb the link", and the
VSYS read should use the same one rather than inventing a second. All three
arms are genuinely safe: idle means no exchange is in flight, parked is the
re-idle delay after a handshake, and link-off means nothing is listening at
all.

A 12 ms hole *during* an exchange would kill a handshake — the M14 timing note
at [`handoff.c:415`](../firmware/apps/handoff/handoff.c#L415) shows margins of
about a millisecond.

### The race that remains

Core 0 checks the state, then is blind for ~12 ms. If contact begins inside
that window, the band misses the opening of the handshake and it aborts. The
trigger re-arms and the next cycle succeeds.

Quantified: the trigger repeats every 60–110 ms (`HANDOFF_LISTEN_MIN_US` 50 ms
to `HANDOFF_LISTEN_MAX_US` 100 ms, plus shout and settle), wrists stay together
for hundreds of milliseconds at least, and the window opens at most once a
minute. This is one retried handshake in a rare coincidence. Ship it.

§9 describes how to remove the race entirely, as a later step.

---

## 5. The schedule, and caching

Two changes at the call sites, both of which reduce exposure for free.

**Cache the value.** Today `report_status()`
([`handoff.c:238`](../firmware/apps/handoff/handoff.c#L238)) measures inline,
so every status notification to the phone triggers a conversion — at the
phone's whim, not ours. It should read a cached value instead. Measurement
happens on one schedule; every reader is free.

**Slow the schedule from 10 s to 60 s.** `POWER_SAMPLE_TICKS` at
[`handoff.c:108`](../firmware/apps/handoff/handoff.c#L108) is
`10000u / LINK_TICK_MS`. A lithium cell does not move in ten seconds. Sixty
gives a sixfold reduction in exposure for nothing.

Take one reading at boot as well, so the first minute is not "unknown".

Together: a blind window that used to be triggerable by the phone becomes one
~12 ms window per minute, only while the band is idle. That is about 0.02 % of
listening time.

---

## 6. API changes, file by file

### `firmware/lib/hal_pico/adc_ring.h` / `.c`

Add an auxiliary-channel measurement that runs inside the ring. Suggested
shape, matching the file's existing naming:

    /* Ask the ring to measure another ADC channel between blocks, without
     * stopping the converter. Called from core 0; serviced by core 1. The
     * caller is responsible for the pin (GP29 belongs to the CYW43 — see
     * power.c) and for not asking while the link is busy. */
    void     adc_ring_request_aux(uint8_t channel, uint8_t gpio);
    bool     adc_ring_aux_ready(uint16_t *mean_code);
    void     adc_ring_aux_cancel(void);

    /* Called by core 1's loop once per iteration. Returns true if this block
     * belongs to the aux measurement and must not reach the DSP. Sets
     * *reinit when the detector should be re-initialised. */
    bool     adc_ring_aux_step(const int16_t *blk, size_t n, bool *reinit);

Whether the block-classification lives in `adc_ring.c` behind
`adc_ring_aux_step()` or directly in `core1_main()` is a judgement call for the
implementer. Putting it in `adc_ring.c` keeps all ADC register access in one
file, which is the stronger argument.

Keep `s_t0`, `s_blocks` and `s_seq[]` untouched throughout (§2.2).

### `firmware/lib/hal_pico/hal_pico.h` / `.c`

Add the cross-core plumbing, modelled on `s_carrier_req` / `s_carrier_ack`:

    /* Measure VSYS on ADC3 without disturbing the ring's timebase. Blocks
     * until core 1 has done it, up to a bounded timeout. The caller must
     * already hold the CYW43 lock and must have woken the chip: GP29 is its
     * SPI clock. Returns 0 if core 1 did not answer in time. */
    uint16_t hal_pico_read_vsys_mv(void);

Core 1's loop gains the `adc_ring_aux_step()` call and the `core1_dsp_init()`
re-arm.

### `firmware/lib/hal_pico/power.c`

`power_vsys_mv()` becomes a router:

- Ring running → take the CYW43 lock, wake the chip, call
  `hal_pico_read_vsys_mv()`, release.
- Ring stopped → the **existing** standalone path, unchanged. It is correct
  when nothing else owns the converter, and it is how the bench apps and the
  pre-core-1 boot sequence work. Do not delete it.

Replace the "THE RING OWNS THE ADC" comment block with a description of the
new split. Keep the electrical notes in
[`power.h`](../firmware/lib/hal_pico/power.h) as they are — they are still
true.

### `firmware/apps/handoff/handoff.c`

The gate, the cache, the 60 s period, the boot reading.

### `firmware/apps/handoff/wear.c`

§8.

---

## 7. Failure modes

**Core 1 never answers.** Core 0 is spinning while holding the radio lock, so
this must be bounded. Cap the wait at ~50 ms — four times the worst-case
window — and on expiry call `adc_ring_aux_cancel()`, release the lock, and
report unknown. A wedged core 1 must not be able to take the Bluetooth link
down with it.

**Cancel must be safe.** If core 0 gives up mid-measurement, core 1 may still
be in the middle of the swap. `adc_ring_aux_cancel()` should set a flag that
core 1 honours by finishing the sequence normally (restore channel 0, re-init
the detector) and discarding the result — never by abandoning the ADC on
channel 3.

**A request arriving while one is in flight.** Ignore it. One measurement at a
time; the caller gets the cache.

**The ring is not running.** `adc_ring_request_aux()` should refuse, so the
router in `power.c` has an unambiguous answer.

---

## 8. A defect the workaround introduced

`power_vsys_mv()` returns 0 for "not measurable". The BLE protocol already
handles that correctly — [`ble.h:112`](../firmware/lib/hal_pico/ble.h#L112)
documents `vsys_20mv` as "0 = not read".

The wearer's side does not. Both
[`wear.c:82`](../firmware/apps/handoff/wear.c#L82) and `battery_show()` just
below it begin:

    if (on_usb || mv == 0u || mv >= WEAR_VSYS_USB_MV) return UI_BATT_OK;

So on the product image today, a short button press reports a **good battery**
whether or not the cell is flat, and `wear_set_power()` sets `s_power_valid`
regardless of whether anything was measured.

Unknown and good are not the same thing and must not share a code path. Fix
this independently of everything above — it is a one-line-ish change and it is
the difference between a wearer being told nothing and being told something
false.

Suggested: `s_power_valid` only becomes true on a real reading, and
`battery_show()` gets a distinct "unknown" event rather than folding into
`UI_EV_BATTERY_SHOW_GOOD`. Check whether
[`docs/android-app-implementation-brief.md:79`](android-app-implementation-brief.md)
and the app's battery row make the same mistake.

---

## 9. Optional second step: hide the window inside a shout

The trigger is *already* deliberately deaf for about 11 ms at the top of every
cycle. [`beacon.h:22`](../firmware/lib/proto/beacon.h#L22):

    SHOUT    HANDOFF_SHOUT_US of flat carrier      (deaf — own amp driving)
    SETTLE   HANDOFF_TURNAROUND_US                 (deaf — own amp recovering)

Those samples are discarded anyway — `p_rx_chips()` cuts the band's own sends
out of the stream on the sample clock. Firing the request as a shout begins
makes the measurement genuinely free, and removes the §4 race entirely: if the
band has just started shouting, it cannot begin receiving a frame for at least
SHOUT + SETTLE.

To fit inside ~11 ms the window must come down from three blocks to two
(≈8.2 ms): measure the **tail** of the first post-swap block instead of giving
it a whole block to itself. Because core 1 swaps immediately on seeing the
request and the ping-pong means the DMA is near the *start* of the next block
at that moment, the last 1024 samples of block X are reliably settled channel
3. Swap back at the end of X, discard X+1.

Do the idle gate first. It is simple and obviously correct. This one is a
timing argument and wants bench evidence before it is trusted.

---

## 10. The durable answer is a board change

GP27 / ADC1 is a no-connect on the current board (`hardware/README.md:867`).
A two-resistor divider from VSYS to GP27 removes the CYW43 from this problem
completely: no lock, no pin handover, no core-0 involvement. Core 1 could take
the reading on its own whenever it liked, and everything in §3.4 and §7
collapses to the channel swap alone.

That is a rev-B item. The board is fully routed, DRC-clean and
generator-sourced (`hardware/tools/gen_pcb.py`), so two resistors plus a net is
not free. But it is the version of this with no moving parts, and it belongs on
the list of reasons to want a rev B.

---

## 11. Bench verification

Board **93D1 on COM7**. Start the serial logger *before* flashing (see the
Pico bench recipe); `picotool ... --ser` to address the board by serial rather
than by COM port.

The heartbeat and `s` currently print overruns, own-chips cut, false syncs,
stalls and core-1 load
([`handoff.c:702`](../firmware/apps/handoff/handoff.c#L702)). `hal_pico_chips()`
exists; `adc_ring_measured_sps()` exists but is **not** exposed through
`hal_pico.h` — add a `hal_pico_sps()` accessor and put both on the heartbeat
line. Those are the two numbers that caught the original bug and they are the
two that prove the fix.

### Acceptance criteria

1. **The pipeline never stops.** `chips` climbs steadily across a measurement;
   measured sample rate stays at 500.0 ksps and does not decay. A frozen count
   or a decaying rate is the original failure returning.
2. **Core-1 load unchanged**, steady around 60 %.
3. **`overruns` does not increase** across a measurement. If it does, core 1
   stopped draining the discarded blocks (§3.2).
4. **`false syncs` does not increase** across a measurement. If it does, the
   detector re-init is missing or the blocks are reaching the DSP (§3.3).
5. **`own-chips cut` behaves as before.** A change here means the sample clock
   moved, i.e. something restarted the ring (§2.2).
6. **The number is right.** Within ~50 mV of a meter on J1 pin 2, remembering
   that GP29 reads *after* D1 — add ~0.35 V for the cell voltage
   (`hardware/README.md:327`).
7. **The link is unharmed.** Fifty handshakes with sampling enabled, on the M13
   passive divider two-board bench (93D1 and 379E, addressed by label not COM
   port). Complete/abort ratio indistinguishable from the M14 baseline.
8. **BLE survives it.** A phone stays connected across many measurements with
   no disconnections.
9. **A flat battery reads as flat**, and an unmeasured one reads as unknown
   rather than good (§8). Force values through the console to check both.

Point 7 is the one that needs patience. Points 1, 3 and 4 are the ones that
catch a subtly wrong implementation.

---

## 12. Rejected alternatives, and why

**Stop the ring, read, restart it.** Breaks the sample clock that M14's
own-send cutting depends on. §2.2. This is the decisive objection, not a
performance one.

**Let core 1 take the CYW43 lock.** Blocks the only consumer of a ring that
overruns in 4 ms, and there is no try-variant of `cyw43_thread_enter()`. §2.4.

**ADC round-robin across channels 0 and 3.** Halves the body-link sample rate
to 250 ksps, which breaks `HANDOFF_ADC_FS_HZ`, the Goertzel bin spacing and the
static assertions in `config.h`. Not viable.

**Make the measurement fully asynchronous — post and collect 250 ms later.**
Attractive, because it removes the 12 ms spin from the BTstack context
entirely. Does not work: someone has to hold the CYW43 lock across the whole
window, core 0 cannot hold it across returns, and core 1 must not hold it at
all. The spin is the price of keeping lock ownership simple.

**Restore GP29's function by hand after the swap.** Needs
`pio_get_funcsel(bus_data->pio)`, which is private to the driver. The driver
restores it on its next transfer anyway. §2.3.

---

## 13. Order of work

1. §8 — the unknown-vs-good defect. Independent, small, and the current
   behaviour is actively misleading.
2. `adc_ring` aux measurement plus core 1's step and re-init. Provable alone
   with a bench app before the link is involved.
3. `hal_pico_read_vsys_mv()` and the `power.c` router.
4. The gate, the cache, the 60 s period, the boot reading.
5. Instrumentation: `hal_pico_sps()`, chips and rate on the heartbeat.
6. Bench, §11.
7. §9 only if §11 point 7 shows the idle gate is not enough.

---

## 14. Outcome, 15 Sep 2026

Implemented as written, through §5, with two departures: core 1 swaps the
input the moment it sees the request rather than at its next block, and
classifies blocks by their ordinal against the swap rather than by counting
phases (`adc_ring_aux_poll` / `adc_ring_aux_step`). The window measured
10–13 ms on both boards.

Bench, 93D1 and 379E on the M13 divider, Release image:

- §11 points 1–5: ~250 measurements forced every 4 s across two runs;
  `chips` climbed throughout, sample rate 499.99 ksps, core-1 load 35 %
  unchanged, overruns and false syncs unchanged, own-chips cut as before.
- §11 point 7: 143/0 and then 81/0 + 79/0 complete/abort with sampling
  forced every 4 s; mean handshake 900–910 ms against the M14 890 ms.
- §11 point 6: 4.56–4.60 V on USB on both boards, as expected after the
  Pico's Schottky; no meter reading yet — take one on J1 pin 2 on the
  battery.
- §11 point 9: `p <mv>` walked unknown / low / critical / good through the
  wearer's side; 0 now answers the button white, never green.
- §11 point 8, later the same day, once the phone could connect at all (see
  below): phone bonded to 379E, link free-running against 93D1, a
  measurement forced every 4 s for 605 s — 235/235 complete, 0 abort on both
  boards, 158 measurements and none failed on each, false syncs 0, sample
  rate 499.997 ksps, no disconnection. Mean handshake 949/954 ms: every
  received card now also goes to the phone as a 154-byte notify. The phone's
  status dump shows the 20 mV value the band caches. The provisioning path
  fixed in the commit before this one was exercised five times from the
  phone (Advanced, "Erase the card on the band", which the app answers by
  re-pushing the card): `60 bytes of text -> 9 compact, flash written` each
  time, core 1 alive, the record present after a reboot.
- §9 was not needed.

The phone bench also found that no phone could connect to a Release image:
core 0's stack, the 4 kB scratch-Y bank shared by the main loop and the whole
BTstack context, overflowed inside the BTstack interrupt on every connection
(the stack guard's STKOF fault, silent on the bench until a fault recorder
that first moves MSP back to the top of the stack was built). At -O3
`on_done`'s 2 kB record is inlined into `main`, and the measured need with a
phone connected is ~3.9 kB. Core 0's stack is now 32 kB at the top of main
RAM (`firmware/ld/`, top-level CMakeLists.txt).

The bench also turned up two defects in the flash write that neither this
work nor M14 had caused and that took the receiver down on every `w`: the
DMA ring marched through RAM while the erase held interrupts off, and core
0's stack overflowed into core 1's under the card encoder. Both are fixed
in the commit before this one; see `adc_ring.c` and `store.h`.
