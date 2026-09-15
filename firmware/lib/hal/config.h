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

/*
 * The firmware version the band reports in its BLE status, and the app
 * shows on the Band screen. Updates are a USB job (BOOTSEL + .uf2); this is
 * so the wrist and the phone can be told apart. Bump it with the image.
 */
#define HANDOFF_FW_VERSION_MAJOR  0
#define HANDOFF_FW_VERSION_MINOR  2
#define HANDOFF_FW_VERSION_PATCH  0

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

/* Windows actually summed into a chip energy, after guarding. */
#define HANDOFF_CHIP_INTEGRATE    (HANDOFF_WINDOWS_PER_CHIP - 2 * HANDOFF_CHIP_GUARD)

/* PIO spends three cycles on each half-period slot (level, direction, and
 * one to make the half-period a whole number of system cycles). */
#define HANDOFF_PIO_SLOT_CYCLES   3
#define HANDOFF_PIO_DIVIDER       (HANDOFF_SYS_CLK_HZ / (2 * HANDOFF_PIO_SLOT_CYCLES * HANDOFF_CARRIER_HZ))

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
 * 150 MHz system clock, three PIO cycles per half-period (§9.8). Anything
 * that is not, is not generatable by PIO without fractional jitter. */
HANDOFF_STATIC_ASSERT(HANDOFF_SYS_CLK_HZ % (2 * HANDOFF_PIO_SLOT_CYCLES * HANDOFF_CARRIER_HZ) == 0,
    "carrier is not an exact PIO divider of the system clock");
HANDOFF_STATIC_ASSERT(HANDOFF_PIO_DIVIDER >= 1 && HANDOFF_PIO_DIVIDER <= 65535,
    "PIO divider out of range");

HANDOFF_STATIC_ASSERT(HANDOFF_FRAG_PAYLOAD >= 8 && HANDOFF_FRAG_PAYLOAD <= 255,
    "fragment payload out of range");

#endif /* HANDOFF_CONFIG_H */
