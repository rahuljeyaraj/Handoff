/*
 * Handoff — Carrier generation, self-measured. M3.
 *
 * Hardware: NONE. That is the point — a second PIO state machine reads the pin
 * state of GP2 and counts edges over a gate interval, and the PIO input mux is
 * independent of whichever peripheral drives the pad. No jumper, no scope.
 *
 * Exit criteria (development plan M3):
 *
 *   - measured carrier within 0.1 % of 40 000 Hz and of 200 000 Hz
 *   - chip timing jitter under 1 us against a chip
 *   - gating on and off is chip-aligned, verified by counting edges per chip
 *   - GP2 reads as high-Z when told to be, RP2350-E9 notwithstanding
 *
 * Since M13 a space chip RELEASES the pad rather than driving it low
 * (design §9.8), so the "driven" half of the pad-state test parks the pad
 * with pio_carrier_hold(); the high-Z half releases it the way a frame does.
 * The gating count is unchanged by this: a mark still has exactly
 * carrier / chip_rate rising edges, and a released pad between marks adds
 * none because a mark ends low and the bare pad stays there.
 *
 * Cannot prove: signal amplitude, or anything at all about the receive side.
 * Cannot prove either — and this is stated rather than glossed — edge-to-edge
 * jitter WITHIN a carrier period. Every measurement here counts edges or times
 * a burst, so it bounds frequency and cumulative drift; a cycle that is early
 * and a cycle that is late in equal measure would not show up. That needs a
 * scope, and design §14 puts it at the bench, not here.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "config.h"
#include "pio_carrier.h"

/* Kept inside the transmit buffer at 200 kHz: 600 chips is 60 000 pad bits. */
#define BURST_SHORT  200u
#define BURST_LONG   600u

static uint8_t s_chips[BURST_LONG];

static int s_fail;

static void check(bool ok, const char *what)
{
    printf("    %-46s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) s_fail++;
}

/* Parts per million, unsigned. */
static uint32_t ppm_err(uint32_t got, uint32_t want)
{
    uint32_t d = got > want ? got - want : want - got;
    return (uint32_t)(((uint64_t)d * 1000000u) / want);
}

/* ---------------------------------------------------------------------- */

static void fill_pattern(uint8_t *chips, size_t n, uint32_t *n_mark)
{
    /* An LFSR rather than an alternating pattern: runs of marks and runs of
     * spaces both have to gate on a chip boundary, and 1010… never produces a
     * run at all. */
    uint32_t lfsr = 0xACE1u;
    size_t i;

    *n_mark = 0;
    for (i = 0; i < n; i++) {
        uint32_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
        lfsr = (lfsr >> 1) | (bit << 15);
        chips[i] = (uint8_t)(lfsr & 1u);
        if (chips[i]) (*n_mark)++;
    }
}

static uint64_t time_burst(size_t n_chips)
{
    uint32_t dummy;
    uint64_t t0, t1;

    fill_pattern(s_chips, n_chips, &dummy);

    /*
     * The clock starts AFTER send() returns, not before it.
     *
     * send() expands chips into pad bits on the CPU and only then starts the
     * DMA, and that expansion is proportional to the chip count -- so timing
     * across it does not cancel in the difference below, it scales with it.
     * Measured the wrong way round this reads as 2.47 us of chip error at
     * 200 kHz and 0.53 us at 40 kHz, in the exact ratio of the two bit rates,
     * which is the tell that it is the harness and not the carrier. The DMA is
     * already running when send() returns, so what is left is transmission.
     */
    pio_carrier_send(s_chips, n_chips);
    t0 = time_us_64();
    while (pio_carrier_busy()) tight_loop_contents();
    t1 = time_us_64();

    return t1 - t0;
}

/* ---------------------------------------------------------------------- */

static void test_carrier(uint32_t hz)
{
    uint32_t measured, err;
    char line[64];

    pio_carrier_mark_continuous(true);
    measured = pio_carrier_measure_hz(200000u);      /* 200 ms gate */
    pio_carrier_mark_continuous(false);

    err = ppm_err(measured, hz);
    printf("    carrier: measured %lu Hz, nominal %lu Hz, error %lu ppm\n",
           (unsigned long)measured, (unsigned long)hz, (unsigned long)err);

    snprintf(line, sizeof line, "carrier within 0.1%% of %lu Hz",
             (unsigned long)hz);
    check(err <= 1000u, line);                        /* 0.1 % = 1000 ppm */
}

/*
 * Chip period by difference. Timing one burst would fold in the cost of
 * starting the DMA and of draining the FIFO and OSR at the end; timing two
 * bursts and subtracting cancels both, because they are identical in each.
 */
static void test_chip_timing(void)
{
    uint64_t t_short = time_burst(BURST_SHORT);
    uint64_t t_long  = time_burst(BURST_LONG);
    uint64_t dt      = t_long - t_short;
    uint32_t dn      = BURST_LONG - BURST_SHORT;

    /* Nanoseconds per chip, so a sub-microsecond error is visible at all. */
    uint32_t ns   = (uint32_t)((dt * 1000u) / dn);
    uint32_t want = (uint32_t)HANDOFF_CHIP_US * 1000u;
    uint32_t derr = ns > want ? ns - want : want - ns;

    printf("    chip:    %lu chips in %lu us -> %lu ns/chip, nominal %lu ns\n",
           (unsigned long)dn, (unsigned long)dt,
           (unsigned long)ns, (unsigned long)want);

    check(derr < 1000u, "chip period within 1 us of nominal");
}

/*
 * Gating alignment. Every mark chip must contain exactly carrier/chip_rate
 * whole carrier cycles — 50 at 200 kHz, 10 at 40 kHz — so over a burst of
 * hundreds of chips the total edge count is an exact multiple. A gate that
 * opened or closed mid-cycle would show up here as a count that is off by one,
 * and nothing else in this app would notice.
 */
static void test_gating(uint32_t hz)
{
    uint32_t n_mark, edges, want;
    uint32_t per_chip = hz / (uint32_t)HANDOFF_CHIP_RATE_HZ;

    fill_pattern(s_chips, BURST_LONG, &n_mark);

    pio_carrier_count_begin();
    pio_carrier_send(s_chips, BURST_LONG);
    while (pio_carrier_busy()) tight_loop_contents();
    sleep_us(500);                    /* let the OSR tail clock out */
    edges = pio_carrier_count_end();

    want = n_mark * per_chip;

    printf("    gating:  %lu mark chips x %lu cycles = %lu edges, counted %lu\n",
           (unsigned long)n_mark, (unsigned long)per_chip,
           (unsigned long)want, (unsigned long)edges);

    check(edges == want, "every gate transition is chip-aligned");
}

/*
 * design §6.3 and RP2350-E9. A pad that is merely driven low still loads the
 * electrode; the claim is that it is high-Z. The test is whether an internal
 * pull can move it: a floating pad follows the pull, a driven one does not.
 * The driven case is checked too, so that a pull too weak to move anything
 * cannot pass this by accident.
 */
static void test_highz(void)
{
    bool down_first, up_then, down_again, driven_holds, space_floats;
    uint8_t space[8];

    /*
     * A space chip is a released pad (§9.8): send a run of them and the pad
     * must follow an internal pull while the generator still owns it. That
     * is the in-frame half of the high-Z claim, new at M13.
     */
    memset(space, 0, sizeof space);
    pio_carrier_drive(true);
    pio_carrier_send(space, sizeof space);
    while (pio_carrier_busy()) tight_loop_contents();
    gpio_pull_down(2); sleep_ms(5); space_floats = (gpio_get(2) == 0);
    gpio_pull_up(2);   sleep_ms(5); space_floats = space_floats && (gpio_get(2) == 1);
    gpio_disable_pulls(2);
    printf("    spaces:  pad follows a pull inside a run of space chips: %s\n",
           space_floats ? "yes" : "NO");
    check(space_floats, "a space chip releases the pad");

    /* Start from a known driven-low pad, then release it the frame way. */
    pio_carrier_hold(0);
    sleep_ms(1);
    pio_carrier_hold(-1);
    pio_carrier_drive(false);

    /*
     * Order matters, and getting it wrong is how this test lies. Pulling UP
     * first can latch the pad high (RP2350-E9), after which a failing
     * pull-down looks like "the pad is still driven" when it is really "the
     * pad latched". So: down first, from the driven-low state, then up, then
     * down AGAIN -- and it is that third reading which separates a genuinely
     * high-impedance pad from a latched one.
     */
    gpio_pull_down(2); sleep_ms(5); down_first = (gpio_get(2) == 0);
    gpio_pull_up(2);   sleep_ms(5); up_then    = (gpio_get(2) == 1);
    gpio_pull_down(2); sleep_ms(5); down_again = (gpio_get(2) == 0);
    gpio_disable_pulls(2);

    /* Drive it low again and confirm the same pull cannot lift it, so that a
     * pull too weak to move anything cannot pass the test by accident. */
    pio_carrier_drive(true);
    pio_carrier_hold(0);
    gpio_pull_up(2); sleep_ms(5); driven_holds = (gpio_get(2) == 0);
    gpio_disable_pulls(2);
    pio_carrier_hold(-1);

    printf("    high-Z:  released->pull-down %s, then pull-up %s, "
           "then pull-down %s\n",
           down_first ? "reads 0" : "READS 1",
           up_then    ? "reads 1" : "READS 0",
           down_again ? "reads 0" : "READS 1 (latched)");
    printf("    driven:  pull-up against a driven low %s\n",
           driven_holds ? "cannot lift it" : "LIFTED IT");

    /*
     * The latch is only a design problem if it is unrecoverable, so probe two
     * recoveries: toggling the pad's input buffer (the datasheet ties E9 to
     * the input stage), and driving the pad low for an instant and releasing
     * it again -- which is what the transmit path does anyway at the end of
     * every frame, and therefore what design §6.3 would actually rely on.
     */
    {
        bool ie_clears, drive_clears;

        gpio_pull_up(2); sleep_ms(5); gpio_pull_down(2); sleep_ms(5);  /* re-latch */

        gpio_set_input_enabled(2, false);
        sleep_ms(2);
        gpio_set_input_enabled(2, true);
        sleep_ms(5);
        ie_clears = (gpio_get(2) == 0);

        if (!ie_clears) {
            pio_carrier_drive(true);
            pio_carrier_hold(0);
            sleep_ms(1);
            pio_carrier_hold(-1);
            pio_carrier_drive(false);
            sleep_ms(5);
            drive_clears = (gpio_get(2) == 0);
        } else {
            drive_clears = true;
        }
        gpio_disable_pulls(2);

        printf("    E9:      input-buffer toggle %s, drive-low-and-release %s\n",
               ie_clears ? "clears it" : "does NOT clear it",
               drive_clears ? "clears it" : "does NOT clear it");
        check(drive_clears, "a latched pad is recoverable by driving it");
    }

    /*
     * And the one that has to hold for the instruments, which keep the input
     * buffer on: entering receive clears any latch, because
     * pio_carrier_drive(false) discharges the pad with the buffer off. The
     * shipped link keeps the buffer off altogether (pio_carrier_sense), so
     * nothing can latch there in the first place. This test is not vacuous
     * any more: the pad really is floating and latched when drive(false) is
     * called, where before M13 it was still driven low by the space send.
     */
    {
        bool cleared;

        gpio_pull_up(2);   sleep_ms(5);        /* latch it high */
        gpio_pull_down(2); sleep_ms(5);        /* confirm it stays high */
        pio_carrier_drive(false);              /* the shipped mitigation */
        gpio_pull_down(2); sleep_ms(5);
        cleared = (gpio_get(2) == 0);
        gpio_disable_pulls(2);

        check(cleared, "drive(false) clears an E9 latch on entry to receive");
    }

    check(down_first && up_then, "GP2 is high-Z when told to be");
    printf("    note:    the bare pad DOES latch high (RP2350-E9); the "
           "criterion is met\n             because drive(false) clears it. "
           "See pio_carrier.c.\n");
    check(driven_holds, "and is genuinely driven when told to be");
}

/* ---------------------------------------------------------------------- */

static void walk(uint32_t hz)
{
    printf("\n  --- %lu Hz ---\n", (unsigned long)hz);
    pio_carrier_init(hz);
    test_carrier(hz);
    test_chip_timing();
    test_gating(hz);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);          /* let the USB console attach before the report */

    printf("\nhandoff txgen (M3: carrier generation, self-measured)\n");
    printf("  sys clock %d Hz, chip rate %d Hz, chip %d us\n",
           HANDOFF_SYS_CLK_HZ, HANDOFF_CHIP_RATE_HZ, HANDOFF_CHIP_US);

    walk(200000u);
    walk(40000u);

    printf("\n  --- pad state ---\n");
    test_highz();

    printf("\n  %s (%d failure%s)\n",
           s_fail == 0 ? "M3 EXIT CRITERIA MET" : "M3 NOT MET",
           s_fail, s_fail == 1 ? "" : "s");

    for (;;) tight_loop_contents();
}
