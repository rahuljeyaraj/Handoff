/*
 * Handoff — hal.h on RP2350. M5. See hal_pico.h.
 *
 * Nothing here is new signal processing. Transmit is pio_carrier.c (M3),
 * receive is adc_ring.c and ipc.c (M4), and the DSP between them is the same
 * lib/dsp the host simulator runs. What this file adds is the core split of
 * architecture §3.3, made real:
 *
 *   core 1   block loop: ADC ring -> the link v2 five-bin bank -> presence,
 *            and the two tone bins -> symbol sync -> ipc ring. Nothing
 *            slower than the chip rate lives here, and nothing here calls
 *            printf.
 *   core 0   everything the HAL hands out: chips popped from the ipc ring,
 *            chips queued to the PIO, time, telemetry, randomness.
 *
 * Transmit never touches core 1 at all. Core 0 fills a chip buffer, DMA clocks
 * it into the PIO, the PIO gates the carrier. TX is essentially free.
 */
#include "hal_pico.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "config.h"
#include "goertzel.h"
#include "gz_bank.h"
#include "ipc.h"
#include "pio_carrier.h"
#include "presence.h"
#include "sync.h"
#include "tlm.h"

/* Deliberately not in tlm.h: only the owner of the block loop may feed the
 * raw burst, and after M5 that owner is this file. See tlm_usb.c. */
void tlm_usb_raw_feed(const int16_t *samples, size_t n, uint64_t first_idx);

/*
 * config.h and the SDK must agree about the system clock, or every derived
 * number in config.h — the PIO divider, the link v2 tone periods — is
 * computed against a clock the board is not running. The SDK is told in the
 * root CMakeLists; config.h carries the same figure. Disagreement is a build
 * error here rather than a silent detuning on the bench. (Link v2 §4.)
 */
HANDOFF_STATIC_ASSERT((uint32_t)HANDOFF_SYS_CLK_HZ == (uint32_t)SYS_CLK_HZ,
    "HANDOFF_SYS_CLK_HZ and the SDK SYS_CLK_HZ disagree");

static hal_iface_t s_iface;
static bool        s_inited;

static uint32_t s_noise_tenths;
static int32_t  s_noise_mean = -1;

/* ---- core 1 <-> core 0 ------------------------------------------------- */

/*
 * Each of these is written by exactly one core. The carrier request is the
 * one handshake: core 0 writes s_carrier_req, core 1 re-tunes and echoes it
 * into s_carrier_ack, and core 0 spins on the echo. The DSP state itself
 * never leaves core 1.
 */
static volatile uint32_t s_carrier_req = HANDOFF_CARRIER_HZ;
static volatile uint32_t s_carrier_ack;
static volatile uint32_t s_level;         /* presence signal score, telemetry */
static volatile uint32_t s_noise;         /* the reference it was judged against */
static volatile uint32_t s_peak_level;    /* ...and the same pair at the peak  */
static volatile uint32_t s_peak_noise;
static volatile bool     s_peak_req;      /* core 0 asks; the take resets it  */
static volatile uint32_t s_chips;
static volatile uint32_t s_windows;
/*
 * SAMPLES CONSUMED, which is not derivable from s_windows any more.
 * Before step 6 a Goertzel ran on every sample whatever else was
 * switched on, so windows times GZ_N was the sample count. The bank can
 * be switched off now, and with it the only thing counting windows -- so
 * a cycles-per-sample budget taken across that switch would divide by
 * zero on one of its two legs. This counts what core 1 was actually
 * handed.
 */
static volatile uint32_t s_samples;
static volatile uint64_t s_busy_us;
static volatile bool     s_core1_up;
static uint64_t          s_core1_t0;

/*
 * ---- THE PROBE: one retunable Goertzel, and it is an INSTRUMENT ---------
 *
 * Until step 6 this was the LINK: one bin, symbol sync, chip energies into
 * the ipc ring. The link is now the five-bin bank's two tone bins, and this
 * single Goertzel has exactly one job left — answering "how much energy is
 * in bin k", for any k, from the console. `b` walks the design bins, `b 1`
 * walks the whole comb below Nyquist, `k` parks it, `m` reads it.
 *
 * TWO THINGS FOLLOW FROM IT BEING AN INSTRUMENT.
 *
 * It is OFF unless someone is looking. A capture switches it on, runs for the
 * windows asked for, and switches it off — so a board doing its job pays
 * nothing for it. At step 5 the whole core-1 loop cost 132 cycles a sample
 * with core 0 idle and stalled outright beside BTstack, and the lesson there
 * (design §10 correction 4) was that headroom on core 1 is not decoration.
 *
 * And it no longer produces CHIPS. It produces a mean and a max over an
 * interval, which is all any of those four commands ever wanted; putting it
 * through symbol sync would be integrating a bin nothing is framing.
 *
 * Same request/ack handshake as everything else here: core 0 writes, core 1
 * obeys and echoes.
 */
static volatile uint16_t s_rx_bin_req = HANDOFF_GZ_BIN;
static volatile uint16_t s_rx_bin_ack;

static volatile uint32_t s_probe_req;     /* windows wanted, 0 = idle */
static volatile bool     s_probe_done;
static uint32_t          s_probe_windows;
static uint64_t          s_probe_sum;
static uint32_t          s_probe_max;

/*
 * ---- link v2: the five-bin bank IS THE RECEIVER -------------------------
 *
 * At step 3 the bank was an instrument running beside the v1 chain, off until
 * asked, so that switching it on and off inside ONE image measured what it
 * costs. Step 5 made it the presence front end and deleted dsp/carrier.c.
 * STEP 6 MADE IT THE WHOLE RECEIVER: the chip stream is its two tone bins,
 * scored in the same window, and there is no other receiver left.
 *
 * So the default is ON, and `linktest n` still toggles it because the budget
 * instrument is still worth having. WITH THE BANK OFF THE BOARD IS DEAF — no
 * presence and no chips, because nothing else scores a bin. The console says
 * so, and the budget instrument counts SAMPLES rather than windows for
 * exactly that reason.
 *
 * Same request/ack handshake as the carrier and the bin: core 0 writes,
 * core 1 obeys and echoes.
 */
static volatile bool s_bank_req = true;
static volatile bool s_bank_ack;

/*
 * Presence, published across the core boundary. Two flags, because hal.h's
 * rx_busy contract has two halves and the reasons are there:
 *
 *   s_busy_latch   any window since core 0 last asked. Core 1 raises it,
 *                  core 0 clears it.
 *   s_busy_now     the most recent window's verdict. Core 1 owns it outright,
 *                  and it is what answers a caller polling faster than
 *                  windows close.
 *
 * Each is written by one core with a single aligned store, and the worst a
 * race can do is lose one latch out of twenty thousand a second.
 */
static volatile bool     s_busy_latch;
static volatile bool     s_busy_now;
static volatile uint32_t s_busy_windows;
static volatile uint32_t s_pres_windows;
static volatile bool     s_pres_ready;

/* Core 0 asking core 1 for the two telemetry scores. See
 * core1_presence_window(): they cost a square root each and the hot path does
 * not pay for them. */
static volatile bool     s_tlm_req;

/*
 * A capture, rather than a live mirror of the bank, because a 64-bit read is
 * not atomic between the cores: core 0 asks for N windows, core 1 fills the
 * accumulator and says done, and only then does core 0 read it. Nothing is
 * torn because nothing is written while it is being read.
 */
static volatile uint32_t s_cap_req;
static volatile bool     s_cap_done;
static hal_pico_bank_t   s_cap;

static void core1_dsp_init(sync_t *sy)
{
    sync_init(sy, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);
}

/* The probe's Goertzel. Bin spacing equals the window rate, so the bin index
 * is just the frequency divided by it — hal_pico_set_rx_bin() checks it. */
static void core1_probe_init(gz_t *g, uint16_t bin)
{
    gz_init(g, HANDOFF_GZ_N, bin);
}

/*
 * One completed bank window, on core 1. Accumulates into the capture if core
 * 0 has asked for one, and does nothing at all otherwise — a capture is the
 * only reason the numbers leave this core.
 */
static void core1_bank_window(const gz_bank_t *b)
{
    uint64_t noise;
    int i;

    if (!s_cap_req || s_cap_done) return;

    for (i = 0; i < GZB_BINS; i++) {
        const uint64_t m = b->mag2[i];
        /* A decimated guard holds its last value between guard windows, so
         * summing it every window would weight one reading four times. Only
         * a fresh number is counted, and guard_windows is its divisor. */
        if (i >= GZB_G_LO && !b->guards_fresh) continue;
        s_cap.sum[i] += m;
        if (m > s_cap.max[i]) s_cap.max[i] = m;
    }
    if (b->guards_fresh) s_cap.guard_windows++;

    noise = gzb_noise(b);
    s_cap.noise_sum += noise;
    if (noise > s_cap.noise_max) s_cap.noise_max = noise;

    if (++s_cap.windows >= s_cap_req) s_cap_done = true;
}

/*
 * One completed bank window, the decision side. Separate from the capture
 * above because they are different jobs: the capture is an instrument that
 * only runs when core 0 asks, this runs always and is what the link consumes.
 */
static HANDOFF_HOT_FUNC void core1_presence_window(presence_t *pr, const gz_bank_t *b)
{
    const bool busy = presence_push_bank(pr, b);

    s_busy_now = busy;
    if (busy) {
        s_busy_latch   = true;
        s_busy_windows = pr->busy_windows;
    }
    s_pres_windows = pr->windows;
    s_pres_ready = presence_ready(pr);

    /*
     * THE TWO SCORES ARE COMPUTED ONLY WHEN SOMEONE IS LOOKING, and that is
     * not an optimisation — it is design §6's "no square roots on the hot
     * path", which the first version of this function broke.
     *
     * presence_signal_score() and presence_noise_score() each take an
     * isqrt64, and the noise one also divides the 64-bit accumulator by the
     * cell count on a part with no 64-bit divider. Taken every window that is
     * four expensive operations twenty thousand times a second, for numbers
     * nobody reads more than once a second. MEASURED: core 1 at 73 % against
     * a 55 % baseline, on a board doing nothing else.
     *
     * So core 0 raises a flag, the next window answers it, and the reading
     * core 0 gets is at worst one window — fifty microseconds — old.
     */
    if (s_tlm_req) {
        s_level = presence_signal_score(pr);
        s_noise = presence_noise_score(pr);
        s_tlm_req = false;
    }

    /*
     * The peak is TAKEN, not read, so it has its own request rather than
     * riding s_tlm_req: a console `s` and a phone block must not reset each
     * other's window. Same discipline — the roots happen in the window that
     * answers the flag, never in every window.
     */
    if (s_peak_req) {
        uint32_t sig = 0, noi = 0;
        presence_take_peak(pr, &sig, &noi);
        s_peak_level = sig;
        s_peak_noise = noi;
        s_peak_req = false;
    }
}

/*
 * The chip stream, link v2 step 6. One bank window has just closed, so both
 * tone bins hold a magnitude taken over the SAME samples, through the same
 * gain and the same body. Their difference is the chip decision, and symbol
 * sync integrates it over HANDOFF_WINDOWS_PER_CHIP windows and decides where
 * the chip boundary is from where that difference MOVES.
 *
 * mag^2, not amplitude: a square root here would run twenty thousand times a
 * second for a number every consumer only ever compares against another one.
 * Design §6, and step 5 measured what breaking it costs.
 *
 * The subtraction is 64-bit because mag^2 reaches 2^31 and the difference of
 * two of them does not fit a signed 32 — but the RESULT does, because one of
 * the two is the off-tone bin sitting at the noise floor. The saturation
 * below is the arithmetic being honest rather than a case that occurs.
 */
static HANDOFF_HOT_FUNC int32_t core1_chip_d(const gz_bank_t *b)
{
    const int64_t d = (int64_t)b->mag2[GZB_B] - (int64_t)b->mag2[GZB_A];

    if (d >  0x7FFFFFFFll) return  0x7FFFFFFF;
    if (d < -0x7FFFFFFFll) return -0x7FFFFFFF;
    return (int32_t)d;
}

static HANDOFF_HOT_FUNC void core1_main(void)
{
    gz_t       g;
    sync_t     sy;
    gz_bank_t  bank;
    presence_t pres;
    bool       bank_on = s_bank_req;
    uint32_t   hz = s_carrier_req;
    uint16_t   bin = s_rx_bin_req;
    uint64_t   busy = 0;

    /* This loop runs out of XIP. A flash erase on core 0 — the record store
     * being provisioned, the bond store on a fresh pairing — would hang it
     * mid-fetch unless core 0 can park it first; flash.c takes the lockout
     * path only if this has been called (see run_write there). */
    flash_safe_execute_core_init();

    core1_dsp_init(&sy);
    core1_probe_init(&g, bin);
    gzb_init(&bank);
    presence_init(&pres);
    s_carrier_ack = hz;
    s_rx_bin_ack  = bin;
    s_bank_ack    = bank_on;
    s_core1_up = true;

    for (;;) {
        const int16_t *blk;
        size_t n = 0, i;
        uint64_t t0, base;
        uint32_t seq = 0;
        bool reinit;

        /*
         * Two requests, and the split is only safe because the bin is derived
         * from the carrier: a carrier change ALWAYS moves the bin, so the
         * branch below still rebuilds the DSP for it. Re-issuing the same
         * carrier rebuilds nothing, which is what it did before as well --
         * the carrier command re-inits the frame receiver on core 0,
         * not here.
         */
        if (s_carrier_req != hz) {
            hz = s_carrier_req;
            s_carrier_ack = hz;
        }
        if (s_rx_bin_req != bin) {
            bin = s_rx_bin_req;
            core1_probe_init(&g, bin);
            s_rx_bin_ack = bin;
        }
        if (s_bank_req != bank_on) {
            bank_on = s_bank_req;
            gzb_reset(&bank);
            /* The reference describes the room and the room has not changed,
             * but the bank it was measured with has just been restarted, so
             * the cells behind the boundary are from a different run. Start
             * it again rather than mixing the two. */
            presence_init(&pres);
            s_bank_ack = bank_on;
        }

        /* A VSYS request is acted on the moment it is seen, block or not:
         * this loop is idle-spinning most of the time and the swap costs
         * nothing. */
        adc_ring_aux_poll();

        /* Same accounting as M4's adcbench: the DC-centring copy inside
         * next_block() is core-1 work, a failed poll is not. */
        t0  = time_us_64();
        blk = adc_ring_next_block_seq(&n, &seq);
        if (blk == 0) {
            tight_loop_contents();
            continue;
        }
        base = (uint64_t)seq * ADC_RING_BLOCK;

        /*
         * A VSYS measurement borrows the converter for three blocks
         * (hal_pico_read_vsys_mv). They are still taken from the ring —
         * otherwise the IRQ would count them as overruns, and that number
         * has to keep meaning what it means — but they carry a step to a DC
         * level and back, which is broadband, so they reach neither the
         * detector nor the raw telemetry, and the detector is rebuilt once
         * the input is back on the pad. Never blocks, never takes a lock:
         * this loop is the ring's only consumer and the ring overruns in
         * 4 ms.
         */
        if (adc_ring_aux_step(blk, n, seq, &reinit)) {
            /*
             * The VSYS blocks carry a step to a DC level and back, which is
             * broadband and would land in all five bins at once. They never
             * reach the bank — the continue below drops them — but a window
             * straddling the swap would, so the bank is rebuilt with the rest
             * of the detector.
             *
             * PRESENCE IS DELIBERATELY NOT REBUILT. Its reference describes
             * the room, and the converter being borrowed for three blocks
             * says nothing about the room. Throwing it away would cost one
             * preamble of deafness every time the phone asks for a battery
             * reading, to re-measure something that has not changed. The one
             * window that spans the swap is lost either way.
             */
            if (reinit) {
                core1_dsp_init(&sy);
                core1_probe_init(&g, bin);
                gzb_reset(&bank);
            }
            busy += time_us_64() - t0;
            s_busy_us = busy;
            continue;
        }

        s_samples += (uint32_t)n;

        tlm_usb_raw_feed(blk, n, base);

        /*
         * THE LINK, since step 6. The bank is no longer beside the v1 chain —
         * it IS the receiver. Presence comes off all five bins, the chip
         * stream off the two tone bins, and both out of the same window.
         *
         * A run at a time, window by window, so the five filters stay in
         * registers across a window instead of being reloaded every sample.
         * That shape is what made the bank affordable at step 3: same
         * arithmetic a call at a time cost 155 cycles a sample, and this
         * costs 46.
         */
        if (bank_on) {
            size_t off = 0;
            while (off < n) {
                bool done = false;
                const size_t took = gzb_push_run(&bank, blk + off, n - off, &done);
                off += took;
                if (!done) break;

                s_windows++;
                core1_presence_window(&pres, &bank);
                core1_bank_window(&bank);

                {
                    int32_t chip;
                    if (sync_push_d(&sy, core1_chip_d(&bank), &chip)) {
                        s_chips++;
                        /* The number of the last sample in the chip, so core 0
                         * can place it on the sample clock — p_rx_chips cuts
                         * our own transmission out by it. */
                        ipc_push_chip(chip, (uint32_t)(base + off - 1u));
                    }
                }
            }
        }

        /* The probe, and only while someone is looking. See s_probe_req. */
        if (s_probe_req && !s_probe_done) {
            for (i = 0; i < n; i++) {
                uint32_t score;
                if (!gz_push(&g, blk[i], &score)) continue;
                s_probe_sum += score;
                if (score > s_probe_max) s_probe_max = score;
                if (++s_probe_windows >= s_probe_req) { s_probe_done = true; break; }
            }
        }

        busy += time_us_64() - t0;
        s_busy_us = busy;
    }
}

/* ---- hal_iface_t bindings --------------------------------------------- */

/*
 * When the PIO will have finished clocking out the last chip. pio_carrier_busy()
 * clears when the DMA is done and the TX FIFO is empty, but the OSR can still
 * hold up to 32 stream bits — the released word every send ends in, 40 us at
 * 200 kHz, 200 us at 40 kHz. The last chips of a frame are the CRC, so busy
 * has to cover them: it is held until the chips have had their airtime plus
 * that tail. The airtime is counted from the DMA start the generator
 * reports, not from when send() returned, so the pad-idle instant is known
 * to a slot and M13 can time settling from it.
 */
static uint64_t s_tx_until;
static uint64_t s_tx_started;    /* DMA start of the last send               */
static uint64_t s_tx_pad_idle;   /* when the last chip of the last send ends */
static uint32_t s_rx_cut;        /* chips dropped as our own, see p_rx_chips */

/*
 * A transmit that is still busy well after its airtime has stalled: the
 * state machine stopped consuming, or the DMA never finished. Seen once at
 * M5, after 48 good frames. Until the cause is pinned this is counted,
 * the hardware state at the moment is kept for the console, and the path is
 * reset so the link carries on rather than refusing every frame after it.
 */
#define TX_STALL_GRACE_US 20000u

static uint32_t            s_tx_stalls;
static pio_carrier_state_t s_tx_stall_state;

static bool tx_stalled_and_reset(void)
{
    if (!pio_carrier_busy() || time_us_64() < s_tx_until + TX_STALL_GRACE_US)
        return false;
    pio_carrier_state(&s_tx_stall_state);
    s_tx_stalls++;
    pio_carrier_reset();
    return true;
}

static void p_tx_drive(void *ctx, bool on)
{
    (void)ctx;
    pio_carrier_drive(on);
}

static size_t p_tx_chips(void *ctx, const uint8_t *chips, size_t n)
{
    uint32_t tail_us;
    (void)ctx;

    if (n == 0 || n > pio_carrier_fsk_max_chips()) return 0;
    if (tx_stalled_and_reset()) { /* recovered: fall through and send */ }
    else if (pio_carrier_busy() || time_us_64() < s_tx_until) return 0;

    /*
     * THE TAIL IS ZERO UNDER FSK, and that is a deletion rather than an
     * omission. v1 appended a released word to park the pad high-Z, and the
     * OSR could still hold up to 32 stream bits of it after the DMA finished
     * — 40 us at 200 kHz — so busy had to cover them or the last chips of a
     * frame, which are the CRC, were cut off. The two-tone stream carries no
     * direction bits and appends nothing: one word is one whole chip, the
     * last chip's airtime is the last of it, and releasing the pad is
     * pio_carrier_drive()'s job (pio_carrier.h).
     */
    tail_us = 0;

    pio_carrier_fsk_send(chips, n);
    s_tx_started  = pio_carrier_started_us();
    s_tx_pad_idle = s_tx_started + (uint64_t)n * HANDOFF_CHIP_US;
    s_tx_until    = s_tx_pad_idle + tail_us;
    return n;
}

static bool p_tx_busy(void *ctx)
{
    (void)ctx;
    if (tx_stalled_and_reset()) return false;
    return pio_carrier_busy() || time_us_64() < s_tx_until;
}

/*
 * The chip stream, with our own transmissions cut out of it ON THE SAMPLE
 * CLOCK. The ipc ring is up to a DMA block late — 4 ms, four turnaround
 * budgets — so a consumer that discards "whatever arrives while I am
 * transmitting" and trusts "whatever arrives after" (link_sm's
 * drain_discard, and the trigger's deaf phases) would be handed the tail of
 * its own shout for the first few milliseconds of every listen, and would
 * wake on it every cycle. The far end's chips are not affected: they were
 * sampled after our pad went quiet, and only the sample number is looked at.
 *
 * The cut runs from the start of the send to the pad-idle instant plus the
 * settling window, so the amplifier's recovery is deaf here too, placed by
 * when it was sampled — link_sm's TURNAROUND can only apply it by arrival.
 * A chip straddling either edge is dropped with the rest. Before any send
 * s_tx_pad_idle is 0 and nothing is cut.
 */
static uint64_t s_idx_hi;
static uint32_t s_idx_last;

static size_t p_rx_chips(void *ctx, int32_t *dst, size_t max)
{
    uint32_t idx[64];
    size_t n, i, kept = 0;
    (void)ctx;

    if (max > 64) max = 64;
    n = ipc_pop_chips(dst, idx, max);

    for (i = 0; i < n; i++) {
        uint64_t t_end, t_start;

        /* Low 32 bits of the sample number wrap every 2.4 hours; chips only
         * ever arrive in order. */
        if (idx[i] < s_idx_last) s_idx_hi += 1ull << 32;
        s_idx_last = idx[i];
        t_end   = adc_ring_sample_us(s_idx_hi | idx[i]);
        t_start = t_end - (uint64_t)HANDOFF_CHIP_US;

        if (s_tx_pad_idle &&
            t_end > s_tx_started && t_start < s_tx_pad_idle + HANDOFF_TURNAROUND_US) {
            s_rx_cut++;
            continue;
        }
        dst[kept++] = dst[i];
    }
    return kept;
}

static uint32_t p_rx_carrier_level(void *ctx)
{
    (void)ctx;
    return s_level;
}

/*
 * hal.h's listen-before-talk. The latch since the last call, OR the most
 * recent window if no window has closed since — the contract and both reasons
 * are in hal.h, and the deaf callers that must still drain it are in
 * link_sm.c's drain_discard().
 */
static bool p_rx_busy(void *ctx)
{
    const bool was = s_busy_latch || s_busy_now;
    (void)ctx;
    s_busy_latch = false;
    return was;
}

/*
 * Ask core 1 for a fresh pair of scores and wait for it, which takes one
 * window — fifty microseconds. Bounded, because with the bank switched off
 * nobody is going to answer, and an instrument must not be able to hang the
 * caller. On a timeout the last published pair stands; the console says the
 * bank is off beside it.
 */
#define TLM_WAIT_US 2000u

static void tlm_refresh(void)
{
    const uint64_t deadline = time_us_64() + TLM_WAIT_US;

    s_tlm_req = true;
    while (s_tlm_req && time_us_64() < deadline) tight_loop_contents();
}

static void p_rx_presence(void *ctx, uint32_t *signal, uint32_t *noise)
{
    (void)ctx;
    tlm_refresh();
    if (signal) *signal = s_level;
    if (noise)  *noise  = s_noise;
}

static uint64_t p_now_us(void *ctx)
{
    (void)ctx;
    return time_us_64();
}

static void p_random(void *ctx, void *dst, size_t n)
{
    uint8_t *b = (uint8_t *)dst;
    (void)ctx;

    while (n) {
        uint32_t v = get_rand_32();
        size_t k = n < 4 ? n : 4;
        memcpy(b, &v, k);
        b += k;
        n -= k;
    }
}

/* ---- construction ------------------------------------------------------ */

const hal_iface_t *hal_pico_init(void)
{
    if (s_inited) return &s_iface;

    ipc_init();
    adc_ring_init();
    adc_ring_start();

    /* M4's reference figure, taken bare: nothing consuming the ring yet. */
    s_noise_tenths = adc_ring_noise_floor(&s_noise_mean);

    /* Restart so the rate and overrun counters start from a known zero. */
    adc_ring_stop();
    adc_ring_start();

    /*
     * THE TRANSMITTER IS THE TWO-TONE GENERATOR, since step 6. One word a
     * chip, a tone for every chip, and no per-chip release — the envelope is
     * constant and high-Z is pio_carrier_drive()'s job alone (design §4).
     *
     * pio_carrier_init() still runs first because it is what claims the pad,
     * sets the divider and loads the edge and duty counters the instruments
     * read; fsk_init() then swaps the one data state machine onto the
     * two-tone program. linktest's `y 9` swaps it back, for a v1 reading.
     *
     * The resting state of a wristband is listening, and design §6.3 says
     * listening means high-Z. The pad's input buffer goes off for good: the
     * link never reads GP2, and with it off RP2350-E9 cannot latch a
     * released pad (§9.8).
     */
    pio_carrier_init(s_carrier_req);
    pio_carrier_fsk_init();
    pio_carrier_sense(false);
    pio_carrier_drive(false);

    tlm_usb_init(0);

    s_iface.ctx              = 0;
    s_iface.tx_drive         = p_tx_drive;
    s_iface.tx_chips         = p_tx_chips;
    s_iface.tx_busy          = p_tx_busy;
    s_iface.rx_chips         = p_rx_chips;
    s_iface.rx_carrier_level = p_rx_carrier_level;
    s_iface.rx_busy          = p_rx_busy;
    s_iface.rx_presence      = p_rx_presence;
    s_iface.now_us           = p_now_us;
    s_iface.telemetry        = tlm_sink;
    s_iface.random           = p_random;

    s_core1_t0 = time_us_64();
    multicore_launch_core1(core1_main);
    while (!s_core1_up) tight_loop_contents();

    s_inited = true;
    return &s_iface;
}

/* ---- instrumentation --------------------------------------------------- */

uint8_t hal_pico_core1_load(void)
{
    uint64_t elapsed = time_us_64() - s_core1_t0;
    uint64_t busy    = s_busy_us;

    if (!s_inited || elapsed == 0) return 0;
    if (busy > elapsed) return 100;
    return (uint8_t)((busy * 100u) / elapsed);
}

uint32_t hal_pico_overruns(void) { return adc_ring_overruns(); }

/*
 * Link v2 step 1. The RP2350 frequency counter gates each clock against
 * clk_ref, so these are measurements, not a read-back of what we asked for.
 * The two *_cfg fields are the read-back, deliberately, so the console can
 * print the pair and the bench can see them agree.
 */
void hal_pico_clocks(hal_pico_clocks_t *m)
{
    if (!m) return;

    m->sys_khz  = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    m->usb_khz  = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    m->adc_khz  = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    m->peri_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    m->ref_khz  = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_REF);

    m->sys_cfg_khz = clock_get_hz(clk_sys) / 1000u;
    m->adc_cfg_khz = clock_get_hz(clk_adc) / 1000u;
}

uint32_t hal_pico_noise_floor(int32_t *mean_code)
{
    if (mean_code) *mean_code = s_noise_mean;
    return s_noise_tenths;
}

bool hal_pico_set_carrier(uint32_t hz)
{
    uint32_t bin;

    /* The same three conditions config.h static-asserts for the default. */
    if (hz == 0 || hz % (uint32_t)HANDOFF_WINDOW_RATE_HZ != 0) return false;
    bin = hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ;
    if (bin == 0 || 2u * bin >= (uint32_t)HANDOFF_GZ_N) return false;
    if ((uint32_t)HANDOFF_SYS_CLK_HZ % (2u * PIO_CARRIER_SLOT_CYCLES * hz) != 0) return false;

    if (!s_inited) {
        s_carrier_req = hz;
        s_rx_bin_req  = (uint16_t)bin;
        return true;
    }

    while (p_tx_busy(0)) tight_loop_contents();

    /* pio_carrier_init() swaps the state machine back to the v1 program to
     * set the divider, so the two-tone program has to be put back or the
     * link would transmit v1 after any carrier change. The v1 carrier is a
     * bring-up instrument on this branch; the data path is not. */
    pio_carrier_init(hz);
    pio_carrier_fsk_init();
    pio_carrier_sense(false);
    pio_carrier_drive(false);

    s_carrier_req = hz;
    s_rx_bin_req  = (uint16_t)bin;
    while (s_carrier_ack != hz || s_rx_bin_ack != (uint16_t)bin)
        tight_loop_contents();
    return true;
}

uint32_t hal_pico_carrier_hz(void) { return s_carrier_req; }

bool hal_pico_set_rx_bin(uint16_t bin)
{
    /* Bin 0 is DC, and the transform folds above Nyquist. Nothing else to
     * check: a receive-only bin owes the PIO nothing. */
    if (bin == 0 || 2u * (uint32_t)bin >= (uint32_t)HANDOFF_GZ_N) return false;

    if (!s_inited) { s_rx_bin_req = bin; return true; }

    s_rx_bin_req = bin;
    while (s_rx_bin_ack != bin) tight_loop_contents();
    return true;
}

uint16_t hal_pico_rx_bin(void) { return s_rx_bin_req; }

/*
 * The probe, run for a stated number of windows. Mean and max of the
 * Goertzel score on whichever bin hal_pico_set_rx_bin() last parked on.
 *
 * Bounded like every other core-1 request here: an instrument must never be
 * able to hang the console. On a timeout it returns what it has.
 */
bool hal_pico_probe(uint32_t windows, uint32_t timeout_us, hal_pico_probe_t *out)
{
    uint64_t deadline;

    if (!out || windows == 0) return false;
    if (!s_inited) return false;

    s_probe_windows = 0;
    s_probe_sum     = 0;
    s_probe_max     = 0;
    s_probe_done    = false;
    s_probe_req     = windows;         /* last, so core 1 sees a zeroed one */

    deadline = time_us_64() + timeout_us;
    while (!s_probe_done && time_us_64() < deadline) tight_loop_contents();

    s_probe_req = 0;
    out->windows = s_probe_windows;
    out->mean_tenths = s_probe_windows
        ? (uint32_t)((s_probe_sum * 10u + s_probe_windows / 2u) / s_probe_windows)
        : 0u;
    out->max = s_probe_max;
    return s_probe_done;
}

/* ---- link v2 step 3: the bank, switched and captured -------------------- */

bool hal_pico_set_bank(bool on)
{
    if (!s_inited) { s_bank_req = on; return true; }

    s_bank_req = on;
    while (s_bank_ack != on) tight_loop_contents();
    return true;
}

bool hal_pico_bank_on(void) { return s_bank_req; }

uint32_t hal_pico_busy_windows(void) { return s_busy_windows; }

void hal_pico_presence(hal_pico_presence_t *out)
{
    if (!out) return;
    tlm_refresh();
    out->windows      = s_pres_windows;
    out->busy_windows = s_busy_windows;
    out->signal       = s_level;
    out->noise        = s_noise;
    out->busy         = s_busy_now;
    out->ready        = s_pres_ready;
}

void hal_pico_take_peak(uint32_t *signal, uint32_t *noise)
{
    const uint64_t deadline = time_us_64() + TLM_WAIT_US;

    s_peak_req = true;
    while (s_peak_req && time_us_64() < deadline) tight_loop_contents();

    /* If core 1 never answered — the bank is off, so no window will ever
     * close — the peak is genuinely nothing rather than stale. */
    if (s_peak_req) {
        s_peak_req = false;
        s_peak_level = 0;
        s_peak_noise = 0;
    }
    if (signal) *signal = s_peak_level;
    if (noise)  *noise  = s_peak_noise;
}

bool hal_pico_bank_capture(uint32_t windows, uint32_t timeout_us,
                           hal_pico_bank_t *out)
{
    uint64_t deadline;

    if (!out || windows == 0) return false;
    if (!s_inited || !s_bank_req) return false;

    memset(&s_cap, 0, sizeof s_cap);
    s_cap_done = false;
    s_cap_req  = windows;              /* last, so core 1 sees a zeroed one */

    deadline = time_us_64() + timeout_us;
    while (!s_cap_done && time_us_64() < deadline) tight_loop_contents();

    *out = s_cap;
    s_cap_req = 0;
    return s_cap_done;
}

void hal_pico_core1_busy(uint64_t *busy_us, uint64_t *now_us)
{
    /* Both readings, together, so a caller can take two of them and divide.
     * hal_pico_core1_load() averages over everything since boot, which is
     * the wrong instrument for a change made a second ago. */
    if (now_us)  *now_us  = time_us_64();
    if (busy_us) *busy_us = s_busy_us;
}

uint32_t hal_pico_tx_stalls(pio_carrier_state_t *last)
{
    if (last) *last = s_tx_stall_state;
    return s_tx_stalls;
}
uint32_t hal_pico_chips(void)      { return s_chips; }
uint32_t hal_pico_windows(void)    { return s_windows; }
uint32_t hal_pico_samples(void)    { return s_samples; }
uint32_t hal_pico_sps(void)        { return adc_ring_measured_sps(); }

/*
 * The core-0 half of the VSYS measurement (see adc_ring.h for the block
 * sequence). The spin is deliberate: someone has to hold the CYW43 lock
 * across the whole window, core 0 cannot hold it across a return, and core
 * 1 must not hold it at all — so core 0 waits, and the wait is bounded so
 * a wedged core 1 cannot take the Bluetooth link down with it. On a
 * timeout the request is cancelled, and core 1 still completes the swap
 * back on its own; the caller gets 0 = not read.
 */
uint16_t hal_pico_read_vsys_mv(void)
{
    int32_t code = -1;
    uint64_t deadline;

    if (!s_core1_up) return 0;
    if (!adc_ring_aux_request(HANDOFF_VSYS_CHANNEL, HANDOFF_PIN_VSYS)) return 0;

    deadline = time_us_64() + HAL_PICO_VSYS_WAIT_US;
    while (!adc_ring_aux_ready(&code)) {
        if (time_us_64() > deadline) {
            adc_ring_aux_cancel();
            return 0;
        }
        tight_loop_contents();
    }
    if (code < 0) return 0;

    /* 12-bit against a 3.3 V reference, through the Pico's own 3:1 divider. */
    return (uint16_t)(((uint32_t)code * 3u * 3300u) / 4096u);
}

size_t hal_pico_rx_chips_at(int32_t *dst, uint32_t *idx, size_t max)
{
    return ipc_pop_chips(dst, idx, max);
}

uint64_t hal_pico_sample_us(uint64_t idx)  { return adc_ring_sample_us(idx); }
uint64_t hal_pico_tx_pad_idle_us(void)     { return s_tx_pad_idle; }
uint64_t hal_pico_tx_started_us(void)      { return s_tx_started; }

bool hal_pico_tx_abort(void)
{
    uint64_t now = time_us_64();
    bool cut = s_tx_pad_idle > now;

    pio_carrier_reset();
    if (cut) s_tx_pad_idle = now;   /* released early */
    s_tx_until = now;
    return cut;
}
uint32_t hal_pico_rx_cut(void)             { return s_rx_cut; }
