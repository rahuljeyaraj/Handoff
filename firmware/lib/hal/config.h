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

/*
 * Link v2 §4: both tones must come out as a whole, EVEN number of system
 * cycles per period, because the PIO generator splits each period into two
 * equal halves and an odd count cannot be halved. 144 MHz is the only clock
 * near the RP2350 default that does this for both 180 and 200 kHz:
 *
 *      150 MHz   180 kHz -> 833.33 cycles   no
 *      144 MHz   180 kHz -> 800 cycles      yes    200 kHz -> 720   yes
 *
 * Physical, not tuned: it is the clock the tone arithmetic demands.
 *
 * The SDK is told the same number in the root CMakeLists, with the matching
 * PLL_SYS_* setup, so the board boots at 144 MHz rather than switching to it.
 * hal_pico.c static-asserts the two agree.
 */
#ifndef HANDOFF_SYS_CLK_HZ
#define HANDOFF_SYS_CLK_HZ        144000000  /* link v2 §4, was 150 MHz        */
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

/*
 * ---- link v2 tone pair (design link-v2-design.md §4) --------------------
 *
 * Structural: the two tones are ADJACENT bins, the closest the transform
 * allows. Coupling rises with frequency, so a wider pair arrives at two
 * different strengths and needs a correction; one bin apart keeps the
 * imbalance near 1 dB and puts both tones at the top of the band where
 * coupling is best. The guards are bins 7, 8 and 11 — the bins no odd
 * harmonic of either tone can reach (§4). Nothing here is tuned; changing
 * these changes the protocol.
 *
 * Frequencies are derived, not typed: bin index times the bin spacing.
 */
#define HANDOFF_TONE_A_BIN        9
#define HANDOFF_TONE_B_BIN        10

#define HANDOFF_TONE_A_HZ         (HANDOFF_TONE_A_BIN * HANDOFF_WINDOW_RATE_HZ)
#define HANDOFF_TONE_B_HZ         (HANDOFF_TONE_B_BIN * HANDOFF_WINDOW_RATE_HZ)

#define HANDOFF_GUARD_LO_BIN      7
#define HANDOFF_GUARD_MID_BIN     8
#define HANDOFF_GUARD_HI_BIN      11

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

/*
 * ---- link v2: the clock owes the tones an exact, even period -------------
 *
 * The fsk_out generator builds each period out of two equal halves. A period
 * that is not a whole number of system cycles cannot be generated at all; a
 * period that is whole but odd cannot be split evenly, and an uneven split is
 * a duty cycle off 50 %, which puts energy in the EVEN harmonics. The second
 * harmonic of 180 kHz is 360 kHz, which aliases at 500 ksps onto 140 kHz —
 * guard bin 7. That would poison the noise reference with our own
 * transmitter, which is exactly the v1 fault this redesign exists to remove.
 *
 * So: a future clock change fails the build here rather than on the bench.
 */
HANDOFF_STATIC_ASSERT(HANDOFF_SYS_CLK_HZ % HANDOFF_TONE_A_HZ == 0,
    "tone A is not a whole number of system cycles");
HANDOFF_STATIC_ASSERT(HANDOFF_SYS_CLK_HZ % HANDOFF_TONE_B_HZ == 0,
    "tone B is not a whole number of system cycles");
HANDOFF_STATIC_ASSERT((HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_A_HZ) % 2 == 0,
    "tone A period is odd: its two halves cannot be equal");
HANDOFF_STATIC_ASSERT((HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_B_HZ) % 2 == 0,
    "tone B period is odd: its two halves cannot be equal");

/* Adjacent bins is the design, not an accident of the numbers above. */
HANDOFF_STATIC_ASSERT(HANDOFF_TONE_B_BIN == HANDOFF_TONE_A_BIN + 1,
    "the tone pair must be adjacent bins");

/* Both tones, and every guard, must be below Nyquist for this window. */
HANDOFF_STATIC_ASSERT(2 * HANDOFF_GUARD_HI_BIN < HANDOFF_GZ_N,
    "guard bin is above Nyquist for this window length");

/*
 * Guards must miss every odd harmonic that folds back into the band. For a
 * tone on bin b, harmonic h lands on |((h*b) mod GZ_N) folded about GZ_N/2|.
 * 180 kHz: 3rd -> 2, 5th -> 5, 7th -> 12.  200 kHz: 3rd -> 5, 5th -> 0,
 * 7th -> 5.  So bins 2, 5 and 12 are unusable, and 7, 8, 11 are clear.
 */
#define HANDOFF_FOLD_BIN(b)                                     \
    (((b) % HANDOFF_GZ_N) <= (HANDOFF_GZ_N / 2)                 \
         ? ((b) % HANDOFF_GZ_N)                                 \
         : (HANDOFF_GZ_N - ((b) % HANDOFF_GZ_N)))

#define HANDOFF_GUARD_CLEAR_OF(g, t)                            \
    ((g) != HANDOFF_FOLD_BIN(3 * (t)) &&                        \
     (g) != HANDOFF_FOLD_BIN(5 * (t)) &&                        \
     (g) != HANDOFF_FOLD_BIN(7 * (t)))

#define HANDOFF_GUARD_CLEAR(g)                                  \
    (HANDOFF_GUARD_CLEAR_OF(g, HANDOFF_TONE_A_BIN) &&           \
     HANDOFF_GUARD_CLEAR_OF(g, HANDOFF_TONE_B_BIN) &&           \
     (g) != HANDOFF_TONE_A_BIN && (g) != HANDOFF_TONE_B_BIN)

HANDOFF_STATIC_ASSERT(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_LO_BIN),
    "low guard bin is contaminated by a tone harmonic");
HANDOFF_STATIC_ASSERT(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_MID_BIN),
    "mid guard bin is contaminated by a tone harmonic");
HANDOFF_STATIC_ASSERT(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_HI_BIN),
    "high guard bin is contaminated by a tone harmonic");

HANDOFF_STATIC_ASSERT(HANDOFF_FRAG_PAYLOAD >= 8 && HANDOFF_FRAG_PAYLOAD <= 255,
    "fragment payload out of range");

#endif /* HANDOFF_CONFIG_H */
