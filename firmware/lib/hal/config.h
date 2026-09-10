/*
 * Handoff — every milestone-varying constant, in one place.
 * firmware-architecture.md §10.
 *
 * Nothing here is commented-as-derived: the derived rates below are computed
 * and then _Static_assert-ed. Setting HANDOFF_CARRIER_HZ to a frequency that
 * is not an exact PIO divider, or not on a Goertzel bin centre, fails the
 * build rather than quietly degrading the link.
 */
#ifndef HANDOFF_CONFIG_H
#define HANDOFF_CONFIG_H

/* ---- the knobs -------------------------------------------------------- */

#ifndef HANDOFF_CARRIER_HZ
#define HANDOFF_CARRIER_HZ        200000  /* 40000 during M3/M10 bring-up      */
#endif

#ifndef HANDOFF_SYS_CLK_HZ
#define HANDOFF_SYS_CLK_HZ        150000000  /* RP2350, design §10.1           */
#endif

#ifndef HANDOFF_ADC_FS_HZ
#define HANDOFF_ADC_FS_HZ         500000  /* design §10.2, adc_set_clkdiv(0)   */
#endif

/*
 * Goertzel window length. Decided at M1 by simulator sweep — see
 * tools/sweep or `handoff_sweep gz_n`. 50 buys 3 dB of processing gain;
 * 25 buys 2x the data rate, which architecture §8.1 argues is worth more
 * because it is the difference between one carousel pass and two inside a
 * one-second handshake.
 */
#ifndef HANDOFF_GZ_N
#define HANDOFF_GZ_N              25
#endif

#ifndef HANDOFF_WINDOWS_PER_CHIP
#define HANDOFF_WINDOWS_PER_CHIP  5
#endif

/*
 * Windows dropped at each end of a chip before integrating. The chip boundary
 * window is a mixture of both chips whenever timing is not perfect, so it
 * carries no information and some noise. Costs processing gain, buys immunity
 * to residual timing error. 0 disables guarding.
 */
#ifndef HANDOFF_CHIP_GUARD
#define HANDOFF_CHIP_GUARD        1
#endif

#ifndef HANDOFF_FRAG_PAYLOAD
#define HANDOFF_FRAG_PAYLOAD      32      /* bytes, architecture §8.3          */
#endif

#ifndef HANDOFF_TURNAROUND_US
#define HANDOFF_TURNAROUND_US     1000    /* design §9.7, measured at M8       */
#endif

/* ---- derived ---------------------------------------------------------- */

/* Goertzel window rate = one score every N samples. */
#define HANDOFF_WINDOW_RATE_HZ    (HANDOFF_ADC_FS_HZ / HANDOFF_GZ_N)

/* Bin spacing equals the window rate, so this is the bin index of the carrier. */
#define HANDOFF_GZ_BIN            (HANDOFF_CARRIER_HZ / HANDOFF_WINDOW_RATE_HZ)

#define HANDOFF_CHIP_RATE_HZ      (HANDOFF_WINDOW_RATE_HZ / HANDOFF_WINDOWS_PER_CHIP)
#define HANDOFF_BIT_RATE_BPS      (HANDOFF_CHIP_RATE_HZ / 2)   /* Manchester   */
#define HANDOFF_CHIP_US           (1000000 / HANDOFF_CHIP_RATE_HZ)

/*
 * How long carrier detection takes to raise its flag — about four chips, from
 * carrier.c's fast EMA. Derived, because it scales with the chip period.
 */
#define HANDOFF_DETECT_US         (4 * HANDOFF_CHIP_US)

/*
 * Role-election backoff range. DERIVED, and deliberately NOT design §9.6's
 * flat 0-5 ms.
 *
 * Two ends collide when their draws land within the detection latency of each
 * other, because until then neither can hear the other. So the collision rate
 * is 1 - (1 - detect/range)^2 — a function of the ratio, not of the range. A
 * fixed 5 ms therefore means something quite different at one Goertzel window
 * length than at another, and measurably so: at HANDOFF_GZ_N 25 a flat 5 ms
 * gives a 1-in-3 first-attempt collision, and at 50 it gives 2 in 3.
 *
 * Sixteen times the latency holds that near 12 % whatever the chip rate, for a
 * mean backoff of eight chips — well under 1 % of R1's one-second contact.
 * Ties still resolve by redraw exactly as §9.6 specifies; only the range it
 * draws from is now a property of the link rather than a constant.
 */
#ifndef HANDOFF_BACKOFF_MAX_US
#define HANDOFF_BACKOFF_MAX_US    (16 * HANDOFF_DETECT_US)
#endif

/* Windows actually summed into a chip energy, after guarding. */
#define HANDOFF_CHIP_INTEGRATE    (HANDOFF_WINDOWS_PER_CHIP - 2 * HANDOFF_CHIP_GUARD)

/* PIO toggles the pad, so it needs two ticks per carrier period. */
#define HANDOFF_PIO_DIVIDER       (HANDOFF_SYS_CLK_HZ / (2 * HANDOFF_CARRIER_HZ))

/* ---- the assertions --------------------------------------------------- */

#if defined(__cplusplus)
#define HANDOFF_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define HANDOFF_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

HANDOFF_STATIC_ASSERT(HANDOFF_ADC_FS_HZ % HANDOFF_GZ_N == 0,
    "Goertzel window must divide the sample rate exactly");

/* The carrier must sit on a bin centre: an integer number of carrier cycles
 * per window, so the window leaks into no other bin (design §10.3). */
HANDOFF_STATIC_ASSERT(HANDOFF_CARRIER_HZ % HANDOFF_WINDOW_RATE_HZ == 0,
    "carrier is not on a Goertzel bin centre");

HANDOFF_STATIC_ASSERT(2 * HANDOFF_GZ_BIN < HANDOFF_GZ_N,
    "carrier bin is above Nyquist for this window length");

HANDOFF_STATIC_ASSERT(HANDOFF_GZ_BIN > 0,
    "carrier bin 0 is DC, not a carrier");

HANDOFF_STATIC_ASSERT(HANDOFF_WINDOW_RATE_HZ % HANDOFF_WINDOWS_PER_CHIP == 0,
    "windows per chip must divide the window rate exactly");

HANDOFF_STATIC_ASSERT(HANDOFF_CHIP_RATE_HZ % 2 == 0,
    "Manchester needs an even chip rate");

HANDOFF_STATIC_ASSERT(1000000 % HANDOFF_CHIP_RATE_HZ == 0,
    "chip period must be a whole number of microseconds");

HANDOFF_STATIC_ASSERT(HANDOFF_CHIP_INTEGRATE >= 1,
    "chip guard leaves no windows to integrate");

/* Design §10.1: both 40 kHz and 200 kHz are exact integer divisions of the
 * 150 MHz system clock. Anything that is not, is not generatable by PIO. */
HANDOFF_STATIC_ASSERT(HANDOFF_SYS_CLK_HZ % (2 * HANDOFF_CARRIER_HZ) == 0,
    "carrier is not an exact PIO divider of the system clock");

HANDOFF_STATIC_ASSERT(HANDOFF_FRAG_PAYLOAD >= 8 && HANDOFF_FRAG_PAYLOAD <= 255,
    "fragment payload out of range");

/* A backoff range narrower than the detection latency is not a backoff. */
HANDOFF_STATIC_ASSERT(HANDOFF_BACKOFF_MAX_US >= 8 * HANDOFF_DETECT_US,
    "backoff range too narrow for the carrier detector to break a tie");

#endif /* HANDOFF_CONFIG_H */
