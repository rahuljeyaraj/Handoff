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

/*
 * How often the guard bins are scored, in windows. The tones are scored every
 * window; the guards are not.
 *
 * They can be skipped because of what they are: nothing we transmit can enter
 * bins 7, 8 and 11 — they are clear of every odd harmonic that folds back into
 * the band, and the even harmonics are null at 50 % duty — so a guard is a
 * noise estimate, time-averaged anyway, and not a signal path. Skipping is
 * what pays for three extra Goertzels:
 *
 *      two tones, every window          2.00 multiplies a sample
 *      three guards, every 4th window    0.75
 *                                        ----
 *                                        2.75   against v1's 1.00
 *
 * 4 rather than 5 or 10 because the guard windows must walk every phase of a
 * chip rather than landing on the same one forever. HANDOFF_WINDOWS_PER_CHIP
 * is 5, so a decimation sharing a factor with it would read one fixed position
 * in every chip — including, at the wrong value, always the chip-boundary
 * window, which is the one window that is a mixture of two chips. Coprime with
 * the windows per chip is the requirement; 4 is the cheapest number that meets
 * it and still costs a quarter.
 */
#ifndef HANDOFF_GUARD_DECIM
#define HANDOFF_GUARD_DECIM       4
#endif

/*
 * ---- link v2 step 5: presence, and where k comes from -------------------
 *
 * dsp/presence.h holds the reasoning; this is the arithmetic. Three inputs,
 * one output, nothing typed that was not computed from them.
 *
 * THE STATED REQUIREMENT. One false busy per minute of continuous listening.
 * That is a goal chosen here, which §1 allows and which every threshold below
 * is then computed from; it is not a number read off a bench.
 *
 * Why a minute. A false busy costs the trigger one cycle — it sends TRIG_LISTEN
 * to TRIG_WAIT, which gives up after HANDOFF_QUIET_WAIT_MAX_US and draws a
 * fresh listen window. That is tens of milliseconds, inside a contact budget of
 * a second holding about twenty such cycles. At one a minute, fewer than one
 * handshake attempt in twenty loses a single cycle, which is not measurable
 * against the budget. A tighter rate would cost sensitivity for nothing; a
 * looser one starts to deflect handshakes.
 *
 * THE DECISION RATE. One verdict per Goertzel window, so HANDOFF_WINDOW_RATE_HZ
 * of them. Per window and not per chip because presence must not depend on
 * symbol sync: a band in IDLE has no lock to integrate a chip against, and the
 * whole point of v2's trigger is that it works before anything is locked.
 *
 * Core 0 reads the verdict far more slowly than core 1 makes it, so hal_pico
 * publishes it sticky-until-read — "has anyone been on the channel since you
 * last asked". That leaves the rate below exactly as stated: a sticky flag
 * fires once per false verdict, the same as the verdicts themselves. Reading
 * an instantaneous flag at poll rate would instead sample one window in twenty
 * and miss most real traffic.
 *
 * THE REFERENCE LENGTH. The guards are decimated, so the cells are guard
 * windows, and the boxcar spans one PREAMBLE of chips:
 *
 *   - Upper bound. The reference must be refreshed at least as often as the
 *     shortest transmission it must not miss, so a room whose noise moved is
 *     re-measured before the next frame arrives. The preamble is that
 *     transmission — it is the shortest run of chips a receiver has to catch,
 *     and missing it loses the whole frame (frame.c cannot join a preamble in
 *     progress). test_presence.c asserts this equals FRAME_PREAMBLE_CHIPS,
 *     which config.h cannot see from here.
 *
 *   - Lower bound. More cells means a smaller k means a more sensitive
 *     detector, so shorter is strictly worse. At 40 cells k is 16.8; at 5 it
 *     is 77, which would be 6.6 dB of sensitivity thrown away.
 *
 *   - Phase. HANDOFF_GUARD_DECIM and HANDOFF_WINDOWS_PER_CHIP are coprime, so
 *     the guard windows walk all five positions in a chip with period five.
 *     The cell count is a multiple of five, so every chip phase enters the
 *     reference the same number of times — eight each. Asserted below.
 *
 * THE FORMULA. For cell-averaging CFAR over N reference cells of exponentially
 * distributed power, P_fa = (1 + k/N)^-N, so
 *
 *      k = N * (P_fa^(-1/N) - 1)
 *        = 40 * (1200000^(1/40) - 1)
 *        = 40 * (1.4189906 - 1)
 *        = 16.7596
 *
 * expressed below as a rational because the decision is a cross-multiplication
 * and never a division. The preprocessor cannot take a fortieth root, so
 * test_presence.c recomputes the line above in double and fails if the
 * constant has drifted from it — the same contract the derived rates at the
 * top of this file get from their static asserts.
 */
#define HANDOFF_CFAR_FALSE_BUSY_S   60u
#define HANDOFF_CFAR_PFA_INV        ((uint32_t)HANDOFF_CFAR_FALSE_BUSY_S \
                                     * (uint32_t)HANDOFF_WINDOW_RATE_HZ)

#define HANDOFF_CFAR_REF_CHIPS      32   /* = FRAME_PREAMBLE_CHIPS */
#define HANDOFF_CFAR_REF_WINDOWS    (HANDOFF_CFAR_REF_CHIPS * HANDOFF_WINDOWS_PER_CHIP)
#define HANDOFF_CFAR_CELLS          (HANDOFF_CFAR_REF_WINDOWS / HANDOFF_GUARD_DECIM)

#define HANDOFF_CFAR_K_NUM          1676
#define HANDOFF_CFAR_K_DEN          100

#define HANDOFF_CHIP_RATE_HZ      (HANDOFF_WINDOW_RATE_HZ / HANDOFF_WINDOWS_PER_CHIP)
#define HANDOFF_BIT_RATE_BPS      (HANDOFF_CHIP_RATE_HZ / 2)   /* Manchester   */
#define HANDOFF_CHIP_US           (1000000 / HANDOFF_CHIP_RATE_HZ)

/*
 * ---- link v2: the two-tone generator's chip words (brief S3) -------------
 *
 * One 32-bit word per chip, built here and nowhere else. Every field is the
 * clock divided by a structural number; none of it is typed.
 *
 *   period cycles  = sys_clk / tone            800 and 720 at 144 MHz
 *   half loop      = (period - overhead) / 2   396 and 356
 *   periods a chip = chip cycles / period      45 and 50
 *
 * The overhead is what fsk_out spends on each period outside its two delay
 * loops: set, mov, the loop-back jmp, and the one delay cycle that keeps the
 * two halves equal. See pio_carrier.pio, which balances it to the cycle.
 *
 * y is periods - 2, not periods - 1, because the program emits two periods
 * outside its loop: the one that carries the two OUT instructions inside its
 * own halves, and the fall-through into the loop head. That is what makes a
 * chip exactly HANDOFF_FSK_CHIP_CYCLES rather than two cycles more.
 */
#define HANDOFF_FSK_PERIOD_OVERHEAD  8

#define HANDOFF_FSK_PERIOD_A   (HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_A_HZ)
#define HANDOFF_FSK_PERIOD_B   (HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_B_HZ)

#define HANDOFF_FSK_ISR_A \
    ((HANDOFF_FSK_PERIOD_A - HANDOFF_FSK_PERIOD_OVERHEAD) / 2)
#define HANDOFF_FSK_ISR_B \
    ((HANDOFF_FSK_PERIOD_B - HANDOFF_FSK_PERIOD_OVERHEAD) / 2)

#define HANDOFF_FSK_CHIP_CYCLES (HANDOFF_SYS_CLK_HZ / HANDOFF_CHIP_RATE_HZ)

#define HANDOFF_FSK_PERIODS_A  (HANDOFF_FSK_CHIP_CYCLES / HANDOFF_FSK_PERIOD_A)
#define HANDOFF_FSK_PERIODS_B  (HANDOFF_FSK_CHIP_CYCLES / HANDOFF_FSK_PERIOD_B)

#define HANDOFF_FSK_Y_A        (HANDOFF_FSK_PERIODS_A - 2)
#define HANDOFF_FSK_Y_B        (HANDOFF_FSK_PERIODS_B - 2)

/* The OSR shifts LEFT and autopull is 32, so `out y, 16` takes the top half
 * of the word and `out isr, 16` the bottom. */
#define HANDOFF_FSK_WORD(y, isr)                                \
    ((((unsigned long)(y) & 0xFFFFuL) << 16) |                  \
      ((unsigned long)(isr) & 0xFFFFuL))

#define HANDOFF_FSK_WORD_A  HANDOFF_FSK_WORD(HANDOFF_FSK_Y_A, HANDOFF_FSK_ISR_A)
#define HANDOFF_FSK_WORD_B  HANDOFF_FSK_WORD(HANDOFF_FSK_Y_B, HANDOFF_FSK_ISR_B)

/*
 * How long carrier detection takes to raise its flag.
 *
 * LINK V2, STEP 5: this no longer describes the detector. dsp/presence.h
 * decides in ONE window with no hysteresis, so the real figure is a window
 * plus core 0's poll interval, far under what is written here. It is left at
 * its v1 value on purpose: the only things that read it are beacon.h's shout
 * and listen constants, and those go out whole at step 7 (brief §8) when the
 * flat-tone shout becomes a nonce beacon. Re-deriving it now would move the
 * rendezvous timing inside a step that is not about rendezvous.
 */
#define HANDOFF_DETECT_US         (4 * HANDOFF_CHIP_US)

/* Windows actually summed into a chip energy, after guarding. */
#define HANDOFF_CHIP_INTEGRATE    (HANDOFF_WINDOWS_PER_CHIP - 2 * HANDOFF_CHIP_GUARD)

/* PIO spends three cycles on each half-period slot (level, direction, and
 * one to make the half-period a whole number of system cycles). */
#define HANDOFF_PIO_SLOT_CYCLES   3
#define HANDOFF_PIO_DIVIDER       (HANDOFF_SYS_CLK_HZ / (2 * HANDOFF_PIO_SLOT_CYCLES * HANDOFF_CARRIER_HZ))

/*
 * ---- where the hot loop lives ------------------------------------------
 *
 * Core 1 runs out of XIP, so every instruction it executes is a flash fetch
 * through a cache that core 0 is also using. That is survivable when core 0 is
 * a console; it is not when core 0 is BLTstack. MEASURED, 25 Sep 2026: the
 * five-bin bank plus presence cost core 1 eighteen points in `linktest`, where
 * core 0 is idle, and took it to NINETY-NINE PER CENT in `apps/handoff`, where
 * core 0 is running a Bluetooth stack — with 4433 DMA overruns in thirty
 * seconds and not one frame decoded. Same code, same clock, same arithmetic.
 * The difference is contention for flash.
 *
 * So the bank's inner loop and the presence decision are placed in RAM. This
 * changes WHERE the code is, not what it does: not one instruction of the
 * arithmetic differs, and the host build — which is the thing that keeps
 * lib/dsp free of SDK headers — sees an empty macro.
 *
 * It is spelled out here rather than taken from the SDK's __not_in_flash_func
 * for exactly that reason: `.time_critical.*` is a plain GCC section
 * attribute, and the SDK's linker script already collects it into RAM, so
 * lib/dsp needs no pico header to ask for it.
 */
#if defined(__GNUC__) && defined(__arm__)
#define HANDOFF_HOT_FUNC __attribute__((section(".time_critical.handoff")))
#else
#define HANDOFF_HOT_FUNC
#endif

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

/*
 * The guards must not lock to one phase of the chip. The full requirement is
 * that the decimation is coprime with the windows per chip — test_gz_bank.c
 * computes the gcd and checks it, because the preprocessor cannot. What is
 * checkable here is the necessary part: neither divides the other, which is
 * what a lazy 5 or 10 would trip over.
 */
HANDOFF_STATIC_ASSERT(HANDOFF_GUARD_DECIM >= 1,
    "guard decimation must be at least every window");
HANDOFF_STATIC_ASSERT(HANDOFF_GUARD_DECIM == 1 ||
    (HANDOFF_GUARD_DECIM % HANDOFF_WINDOWS_PER_CHIP != 0 &&
     HANDOFF_WINDOWS_PER_CHIP % HANDOFF_GUARD_DECIM != 0),
    "guard decimation shares a factor with the windows per chip: the guards "
    "would read the same position in every chip forever");

/*
 * ---- link v2: the chip words have to close, exactly ----------------------
 *
 * A chip that is not a whole number of tone periods, or a period whose two
 * halves are not equal, is a timing error with the same sign every chip --
 * so it walks the chip clock down the frame rather than scattering. That is
 * the one failure pio_carrier.pio's original comment warned about, and the
 * only defence is that the arithmetic closes here.
 */
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_PERIOD_A * HANDOFF_FSK_PERIODS_A
                          == HANDOFF_FSK_CHIP_CYCLES,
    "tone A: a chip is not a whole number of periods");
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_PERIOD_B * HANDOFF_FSK_PERIODS_B
                          == HANDOFF_FSK_CHIP_CYCLES,
    "tone B: a chip is not a whole number of periods");

/* Both tones must take the SAME number of cycles per chip, or the two
 * symbols are different lengths on the wire and Manchester loses its edge. */
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_PERIOD_A * HANDOFF_FSK_PERIODS_A
                          == HANDOFF_FSK_PERIOD_B * HANDOFF_FSK_PERIODS_B,
    "the two tones do not take the same number of cycles per chip");

/* The generator rebuilds each period from two equal halves of isr + 4. */
HANDOFF_STATIC_ASSERT(2 * HANDOFF_FSK_ISR_A + HANDOFF_FSK_PERIOD_OVERHEAD
                          == HANDOFF_FSK_PERIOD_A,
    "tone A half-period loop does not reconstruct its period");
HANDOFF_STATIC_ASSERT(2 * HANDOFF_FSK_ISR_B + HANDOFF_FSK_PERIOD_OVERHEAD
                          == HANDOFF_FSK_PERIOD_B,
    "tone B half-period loop does not reconstruct its period");

/* Two periods are emitted outside the loop, so y cannot go below zero. */
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_PERIODS_A >= 2 && HANDOFF_FSK_PERIODS_B >= 2,
    "a chip must hold at least two tone periods");

/* Both fields are 16 bits of one word. */
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_ISR_A <= 0xFFFF && HANDOFF_FSK_ISR_B <= 0xFFFF,
    "half-period loop count does not fit 16 bits");
HANDOFF_STATIC_ASSERT(HANDOFF_FSK_Y_A <= 0xFFFF && HANDOFF_FSK_Y_B <= 0xFFFF,
    "period count does not fit 16 bits");

/* A chip is a whole number of microseconds of system clock, which is what
 * lets the bench state the chip period without a rounding note. */
HANDOFF_STATIC_ASSERT(HANDOFF_SYS_CLK_HZ % HANDOFF_CHIP_RATE_HZ == 0,
    "a chip is not a whole number of system cycles");

/*
 * ---- link v2 step 5: the CFAR reference has to close, exactly ------------
 *
 * The value of k above is stated for exactly HANDOFF_CFAR_CELLS cells. Any
 * change to the preamble, the decimation or the windows per chip moves the
 * cell count, which moves k — and the preprocessor cannot recompute a
 * fortieth root, so what it can check is that the cell count is still a whole,
 * phase-balanced number. test_presence.c checks k itself against the formula.
 */
HANDOFF_STATIC_ASSERT(HANDOFF_CFAR_REF_WINDOWS % HANDOFF_GUARD_DECIM == 0,
    "the CFAR reference is not a whole number of guard windows");

HANDOFF_STATIC_ASSERT(HANDOFF_CFAR_CELLS % HANDOFF_WINDOWS_PER_CHIP == 0,
    "the CFAR reference weights some chip phases more than others: the guard "
    "windows walk WINDOWS_PER_CHIP positions, so the cell count must be a "
    "multiple of it");

/* Fewer cells than this and k stops being the number written above. */
HANDOFF_STATIC_ASSERT(HANDOFF_CFAR_CELLS >= HANDOFF_WINDOWS_PER_CHIP,
    "the CFAR reference is shorter than one pass through the chip phases");

/* presence.c cross-multiplies signal by K_DEN * CELLS. mag^2 of a 12-bit
 * converter at HANDOFF_GZ_N stays under 2^31, so this must leave room. */
HANDOFF_STATIC_ASSERT((unsigned long long)HANDOFF_CFAR_K_DEN * HANDOFF_CFAR_CELLS
                          < (1ull << 30),
    "the CFAR cross-multiplication could overflow a 64-bit product");

HANDOFF_STATIC_ASSERT(HANDOFF_CFAR_K_NUM > HANDOFF_CFAR_K_DEN,
    "k below 1 would call an empty room busy");

HANDOFF_STATIC_ASSERT(HANDOFF_FRAG_PAYLOAD >= 8 && HANDOFF_FRAG_PAYLOAD <= 255,
    "fragment payload out of range");

#endif /* HANDOFF_CONFIG_H */
