/*
 * Handoff — the RP2350 binding of hal.h. architecture §4, §12.
 *
 * M5. This is the file that first ties the whole HAL together, and the
 * development plan is deliberate about not doing that until the pieces it
 * binds have each been proven alone:
 *
 *   M3  pio_carrier.c   carrier generation, self-measured
 *   M4  adc_ring.c      500 ksps into a DMA ring, and ipc.c across the cores
 *   M5  hal_pico.c      the two of them, wired to lib/
 *
 * Everything above lib/hal is already tested against test/host's simulator, so
 * what M5 adds is only the binding — which is the entire point of the seam.
 */
#ifndef HANDOFF_HAL_PICO_H
#define HANDOFF_HAL_PICO_H

#include "adc_ring.h"
#include "gz_bank.h"
#include "hal.h"
#include "pio_carrier.h"

/*
 * Pins, from design §6 and §10.2. Collected here rather than scattered through
 * the drivers, because the netlist in design §7 is the authority and one file
 * should be diffable against it.
 */
/*
 * The transmit pin is a build option: HANDOFF_TX_PIN in firmware/CMakeLists.txt
 * (scripts/build.py --tx-pin). The breadboard benches of M3-M14 were wired
 * on GP2; the PCB routes TX to GP11 (hardware/README.md, deviation table),
 * and scripts/bringup.py always builds for that.
 */
#ifndef HANDOFF_PIN_TX
#define HANDOFF_PIN_TX      2    /* GP2 (bench) or GP11 (PCB) -> R1 -> pad, design §6.3 */
#endif
#define HANDOFF_PIN_ADC     26   /* GP26 = ADC0,       design §10.2 */
#define HANDOFF_ADC_CHANNEL 0
#define HANDOFF_PIN_VSYS    29   /* GP29 = ADC3, VSYS/3 — and the CYW43 SPI clock */
#define HANDOFF_VSYS_CHANNEL 3

/*
 * How far behind the ADC hal_rx_chips() can run: one DMA block. Core 1 only
 * sees a block once the DMA has filled it, so a chip can surface this long
 * after it was sampled. The link's own timings assume chips arrive as they
 * are sampled (the simulator's HAL does), so anything in link_cfg_t that
 * waits for the far end to be heard has to allow for this — M14 found
 * rx_idle_us at its 6 ms default giving up on a reply that was still in
 * the ring, and the two boards talking over each other every turn.
 */
#define HAL_PICO_RX_LATENCY_US \
    ((uint32_t)ADC_RING_BLOCK * 1000000u / (uint32_t)HANDOFF_ADC_FS_HZ)

/*
 * How long hal_tx_chips() takes to get a frame onto the pad: pio_carrier_send
 * packs every stream bit of every chip before the DMA starts, and a frame is
 * 624 chips of 200 bits at 200 kHz. Measured at M14 from the console logs,
 * 789 frames on two boards: 5.4 ms minimum, 5.5 median, 6.2 maximum. The far
 * end is silent for this long between deciding to reply and being audible,
 * on top of its turnaround, and a receiver that gave up before then talked
 * over the reply (M14, both boards clocking frames out together for three
 * frames at a time). A packer that copied precomputed chip patterns would
 * make this near zero; until then it is budgeted.
 */
#define HAL_PICO_TX_SETUP_US 7000u

/*
 * Fills in the interface and starts core 1. Idempotent: a second call returns
 * the same interface and starts nothing.
 *
 * Boot order matters and is fixed here: the ring is started, the bare-ADC
 * noise floor is taken with nothing else consuming it (M4's reference figure,
 * see hal_pico_noise_floor), the ring is restarted from zero, the carrier is
 * brought up and released to high-Z, and only then is core 1 launched.
 */
const hal_iface_t *hal_pico_init(void);

/* Core-1 busy time as a percentage of wall time since core 1 started.
 * Headroom is 100 minus this. M4 exit criterion. */
uint8_t hal_pico_core1_load(void);

/*
 * DMA blocks dropped since boot. M4 requires this to stay at zero for ten
 * minutes, and it is the number that closes design §17's second open item —
 * or sends the analogue mixer back into the design.
 */
uint32_t hal_pico_overruns(void);

/* Bare-ADC noise floor in tenths of an LSB RMS, taken at init on the on-die
 * temperature sensor. *mean_code gets the raw 12-bit mean. This is M4's
 * reference figure and the one M5's leakage criterion is compared against. */
uint32_t hal_pico_noise_floor(int32_t *mean_code);

/*
 * Bring-up only: move both ends of the link to another carrier at run time.
 * The PIO divider changes on core 0 and core 1 re-tunes its Goertzel bin, so
 * one image can walk 40 kHz and 200 kHz the way txgen does. Returns false and
 * changes nothing if hz is not an exact PIO divider on a Goertzel bin centre.
 * Blocks until core 1 has re-tuned; chips in flight across the change are
 * meaningless and the caller should reset its frame receiver.
 */
bool hal_pico_set_carrier(uint32_t hz);
uint32_t hal_pico_carrier_hz(void);

/* Transmits that stayed busy past their airtime and were reset, with the
 * hardware state captured at the last one. Should be zero; see hal_pico.c. */
uint32_t hal_pico_tx_stalls(pio_carrier_state_t *last);

/* Chips produced by core 1 since start, Goertzel windows scored, and the
 * ring's measured sample rate. chips and sps are the pair that caught the
 * ADC being taken from the ring (a frozen count over a decaying rate) and
 * the pair that prove it is not: both belong on any heartbeat. */
uint32_t hal_pico_chips(void);
uint32_t hal_pico_windows(void);
uint32_t hal_pico_sps(void);

/*
 * VSYS on ADC3, measured inside the ring: core 1 swaps the converter's
 * input between blocks and back, the block counter and the sample clock
 * run through it, and the detector is rebuilt afterwards
 * (adc_ring_aux_step). Blocks — up to HAL_PICO_VSYS_WAIT_US — until core 1
 * has answered; the link is blind for the three blocks it borrows, ~12 ms,
 * so the caller chooses a moment when nothing is in flight.
 *
 * The caller must hold the CYW43 lock and have woken the chip: GP29 is its
 * SPI clock, and no transfer may overlap the window in which the pin is
 * analogue. power.c is the one caller. Returns 0 if core 1 did not answer
 * in time; the request is then cancelled and core 1 finishes it on its own.
 */
#define HAL_PICO_VSYS_WAIT_US 50000u
uint16_t hal_pico_read_vsys_mv(void);

/*
 * M13 instrumentation. The chip stream with each chip's place on the ADC
 * sample clock (the number of its last sample), and that clock in
 * time_us_64() terms, so a bench app can say which chips were sampled
 * inside a window rather than which arrived during it -- the ring's latency
 * is up to a DMA block, 4 ms, which is four turnaround budgets. Same ring
 * as hal_rx_chips(); pop from one or the other.
 */
size_t   hal_pico_rx_chips_at(uint16_t *dst, uint32_t *idx, size_t max);
uint64_t hal_pico_sample_us(uint64_t idx);

/* When the last send's final chip ended on the pad (the DMA start plus the
 * airtime), which is when the generator released it — and when its DMA
 * started. Between them the pad was ours; M14 logs both for every send so
 * two boards' logs can be checked for overlap. */
uint64_t hal_pico_tx_pad_idle_us(void);
uint64_t hal_pico_tx_started_us(void);

/* Cut a send short and release the pad now. hal_tx_busy() clears at once,
 * so a fresh send is accepted straight after. M14's forced simultaneous
 * start uses it: both boards drop whatever they were doing on the same
 * edge and restart the trigger together. True if a send was actually in
 * flight — its pad-idle time is now the moment of the cut. */
bool     hal_pico_tx_abort(void);

/*
 * Chips hal_rx_chips() dropped because they were sampled while our own pad
 * was driven or settling — placed on the sample clock, since the ipc ring
 * runs up to 4 ms behind it (M14; see p_rx_chips). Not counted for
 * hal_pico_rx_chips_at(), which hands over the raw stream.
 */
uint32_t hal_pico_rx_cut(void);

/*
 * ---- link v2 step 1: the clock tree, measured ---------------------------
 *
 * An instrument, not behaviour. Moving sys_clk 150 -> 144 MHz (link v2 §4)
 * is only safe if the ADC and USB clocks do not move with it, and the brief
 * says verify that rather than assume it. clock_get_hz() would only repeat
 * what the SDK was told; this counts each clock against the crystal with the
 * RP2350 frequency counter, so a clock that is not where it is supposed to
 * be shows up as a number rather than as a dead link.
 *
 * Fills *m and returns it. Every field is kHz as measured, except the two
 * *_cfg fields, which are what the SDK believes — print both and compare.
 */
typedef struct {
    uint32_t sys_khz;       /* measured clk_sys  */
    uint32_t usb_khz;       /* measured clk_usb  — must stay 48000 */
    uint32_t adc_khz;       /* measured clk_adc  — must stay 48000 */
    uint32_t peri_khz;      /* measured clk_peri */
    uint32_t ref_khz;       /* measured clk_ref  — the crystal path */
    uint32_t sys_cfg_khz;   /* what the SDK was told clk_sys is */
    uint32_t adc_cfg_khz;   /* what the SDK was told clk_adc is */
} hal_pico_clocks_t;

void hal_pico_clocks(hal_pico_clocks_t *m);

/*
 * ---- link v2 step 2: read any bin -------------------------------------
 *
 * An instrument, not behaviour. hal_pico_set_carrier() moves the transmitter
 * and the receiver together, which is right for the link and useless for the
 * bin bank: the guards at 140, 160 and 220 kHz are RECEIVE-ONLY and are not
 * PIO dividers at all, so set_carrier() refuses them.
 *
 * This retunes core 1's Goertzel alone, to any bin the window can hold, and
 * leaves the generator exactly where it is. With the two-tone generator
 * driving one tone continuously, walking the bank is then five reads of `m`
 * — and the one that matters is that bins 7, 8 and 11 stay at the noise while
 * a tone is being transmitted. A guard that rises with our own transmitter is
 * v1's floor again (design §4).
 *
 * Blocks until core 1 has re-tuned. Chips in flight across the change are
 * meaningless; reset the frame receiver after it. Returns false, changing
 * nothing, for a bin at or above Nyquist for the window.
 */
bool     hal_pico_set_rx_bin(uint16_t bin);
uint16_t hal_pico_rx_bin(void);

/*
 * ---- link v2 step 3: the five-bin bank ---------------------------------
 *
 * Also an instrument, for now. The bank (dsp/gz_bank.h) is the v2 receiver's
 * front end, but nothing consumes it yet, so it runs BESIDE the v1 chain on
 * core 1 and starts switched off.
 *
 * That is what makes the budget answerable. Step 1 found a 4-point swing in
 * core-1 load from adding one unrelated function and moving the image in
 * XIP — so two images cannot be compared, and the only measurement worth
 * anything is the bank switched on and off inside ONE image. That is exactly
 * what these do.
 *
 * hal_pico_set_bank() blocks until core 1 has obeyed.
 */
bool hal_pico_set_bank(bool on);
bool hal_pico_bank_on(void);

/*
 * One capture of the bank: core 1 accumulates `windows` windows and core 0
 * reads the result afterwards, so nothing is read while it is being written.
 *
 * sum[] and max[] are mag^2 — no square root is taken anywhere near the hot
 * path (link v2 §5). gz_bank.h's gzb_score() turns one into the amplitude a
 * human reads. The guard entries are summed only over the windows they
 * actually ran in, which is what guard_windows divides by.
 *
 * False if the bank is off, or if core 1 did not finish inside timeout_us —
 * *out then holds however far it got.
 */
typedef struct {
    uint64_t sum[GZB_BINS];
    uint64_t max[GZB_BINS];
    uint64_t noise_sum;      /* the guard median, per window */
    uint64_t noise_max;
    uint32_t windows;
    uint32_t guard_windows;
} hal_pico_bank_t;

bool hal_pico_bank_capture(uint32_t windows, uint32_t timeout_us,
                           hal_pico_bank_t *out);

/*
 * Core-1 busy time and the clock it is measured against, read together.
 *
 * hal_pico_core1_load() averages over everything since boot and cannot see a
 * change made a second ago. Two of these, subtracted, give the load over an
 * interval the caller chooses — and multiplied by the system clock and
 * divided by the samples in that interval, they give cycles per sample, which
 * is the number the budget is actually about.
 */
void hal_pico_core1_busy(uint64_t *busy_us, uint64_t *now_us);

#endif /* HANDOFF_HAL_PICO_H */
