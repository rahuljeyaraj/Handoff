/*
 * Handoff — link v2 step 1: the clock and the tone pair, as assertions.
 *
 * config.h already static-asserts most of this, and a static assert is the
 * stronger guard because it fails the build rather than a test run. So why
 * also test it here?
 *
 * Because a static assert can only say "this is true". It cannot say what it
 * was supposed to be, and it cannot be read as a record. The numbers below are
 * the ones docs/link-v2-design.md §4 works out by hand — 800 and 720 cycles,
 * the harmonic folds, the guard set — and this file is where a future reader
 * can see that the hand arithmetic and the build agree.
 *
 * It is also the negative half. The static asserts fire when a clock change
 * breaks a tone; nothing else checks that they fire for the RIGHT reason, or
 * that the guard bins were chosen rather than guessed.
 */
#include "config.h"
#include "hf_test.h"
#include "tests.h"

/*
 * Which bin a harmonic folds back into at this sample rate, worked out the
 * long way round in integers — deliberately NOT config.h's macro, so the two
 * are independent and a mistake in either shows up as a disagreement.
 */
static int fold_bin(int harmonic_bin)
{
    int n = HANDOFF_GZ_N;
    int b = harmonic_bin % n;

    if (b < 0) b += n;
    return b <= n / 2 ? b : n - b;
}

void test_clock(void)
{
    int a = HANDOFF_TONE_A_BIN;
    int b = HANDOFF_TONE_B_BIN;

    /* ---- the clock, and why it is 144 MHz ---------------------------- */
    hf_begin("sys clock is 144 MHz");
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ, 144000000);

    hf_begin("bin spacing is the window rate");
    HF_EQ_INT(HANDOFF_WINDOW_RATE_HZ, 20000);

    hf_begin("tone frequencies are bin index times bin spacing");
    HF_EQ_INT(HANDOFF_TONE_A_HZ, 180000);
    HF_EQ_INT(HANDOFF_TONE_B_HZ, 200000);

    /*
     * Design §4's table, which is the whole reason for the clock move: both
     * periods a whole number of cycles, and both even, because the generator
     * splits each period into two equal halves. 150 MHz gives 833.33 for
     * tone A and is why it had to go.
     */
    hf_begin("each tone period is a whole number of system cycles");
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ % HANDOFF_TONE_A_HZ, 0);
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ % HANDOFF_TONE_B_HZ, 0);

    hf_begin("each tone period is 800 / 720 cycles, and both are even");
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_A_HZ, 800);
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_B_HZ, 720);
    HF_EQ_INT((HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_A_HZ) % 2, 0);
    HF_EQ_INT((HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_B_HZ) % 2, 0);

    /* The v1 generator's divider moves 125 -> 120 and stays exact. */
    hf_begin("the v1 200 kHz PIO divider stays exact at the new clock");
    HF_EQ_INT(HANDOFF_PIO_DIVIDER, 120);
    HF_EQ_INT(HANDOFF_SYS_CLK_HZ %
              (2 * HANDOFF_PIO_SLOT_CYCLES * HANDOFF_CARRIER_HZ), 0);

    /* ---- the tone pair ----------------------------------------------- */
    hf_begin("the tones are adjacent bins");
    HF_EQ_INT(b, a + 1);

    hf_begin("both tones are below Nyquist for this window");
    HF_CHECK(2 * b < HANDOFF_GZ_N);

    /* ---- the harmonics, against design §4's table -------------------- */
    hf_begin("odd harmonics of tone A fold to bins 2, 5, 12");
    HF_EQ_INT(fold_bin(3 * a), 2);
    HF_EQ_INT(fold_bin(5 * a), 5);
    HF_EQ_INT(fold_bin(7 * a), 12);

    hf_begin("odd harmonics of tone B fold to bins 5, 0, 5");
    HF_EQ_INT(fold_bin(3 * b), 5);
    HF_EQ_INT(fold_bin(5 * b), 0);
    HF_EQ_INT(fold_bin(7 * b), 5);

    hf_begin("config.h's fold macro agrees with the long way round");
    HF_EQ_INT(HANDOFF_FOLD_BIN(3 * HANDOFF_TONE_A_BIN), fold_bin(3 * a));
    HF_EQ_INT(HANDOFF_FOLD_BIN(5 * HANDOFF_TONE_A_BIN), fold_bin(5 * a));
    HF_EQ_INT(HANDOFF_FOLD_BIN(7 * HANDOFF_TONE_A_BIN), fold_bin(7 * a));
    HF_EQ_INT(HANDOFF_FOLD_BIN(3 * HANDOFF_TONE_B_BIN), fold_bin(3 * b));
    HF_EQ_INT(HANDOFF_FOLD_BIN(5 * HANDOFF_TONE_B_BIN), fold_bin(5 * b));
    HF_EQ_INT(HANDOFF_FOLD_BIN(7 * HANDOFF_TONE_B_BIN), fold_bin(7 * b));

    /* ---- the guard set ------------------------------------------------
     *
     * The guards are the noise reference. A guard a harmonic can reach would
     * be measuring our own transmitter, which is v1's floor in a new hat, so
     * the contaminated bins are named here as well as the clear ones.
     */
    hf_begin("the chosen guards are 7, 8 and 11");
    HF_EQ_INT(HANDOFF_GUARD_LO_BIN, 7);
    HF_EQ_INT(HANDOFF_GUARD_MID_BIN, 8);
    HF_EQ_INT(HANDOFF_GUARD_HI_BIN, 11);

    hf_begin("every chosen guard is clear of both tones' odd harmonics");
    HF_CHECK(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_LO_BIN));
    HF_CHECK(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_MID_BIN));
    HF_CHECK(HANDOFF_GUARD_CLEAR(HANDOFF_GUARD_HI_BIN));

    hf_begin("bins a harmonic reaches are rejected as guards");
    HF_CHECK(!HANDOFF_GUARD_CLEAR(2));
    HF_CHECK(!HANDOFF_GUARD_CLEAR(5));
    HF_CHECK(!HANDOFF_GUARD_CLEAR(12));

    hf_begin("the tone bins themselves are rejected as guards");
    HF_CHECK(!HANDOFF_GUARD_CLEAR(HANDOFF_TONE_A_BIN));
    HF_CHECK(!HANDOFF_GUARD_CLEAR(HANDOFF_TONE_B_BIN));

    hf_begin("every guard is below Nyquist for this window");
    HF_CHECK(2 * HANDOFF_GUARD_LO_BIN < HANDOFF_GZ_N);
    HF_CHECK(2 * HANDOFF_GUARD_MID_BIN < HANDOFF_GZ_N);
    HF_CHECK(2 * HANDOFF_GUARD_HI_BIN < HANDOFF_GZ_N);

    /*
     * Design §4 says even harmonics land in bins 7 and 11 and are zero only
     * while the generator holds a 50 % duty cycle — which is what makes those
     * two a duty-cycle monitor. Record that here so the claim is checkable
     * rather than remembered.
     */
    hf_begin("guards 7 and 11 are the even-harmonic monitor");
    /* 2nd of tone A: 360 kHz, aliases to 140 kHz -> guard 7. */
    HF_EQ_INT(fold_bin(2 * a), HANDOFF_GUARD_LO_BIN);
    /* 4th of tone A: 720 kHz, aliases to 220 kHz -> guard 11. */
    HF_EQ_INT(fold_bin(4 * a), HANDOFF_GUARD_HI_BIN);

    /*
     * Both come from tone A. Tone B's even harmonics land on bin 5 (2nd) and
     * on tone B itself (4th), neither of which is a guard, so a duty-cycle
     * slip on the 200 kHz symbol shows up only indirectly. Worth knowing
     * before reading the guards as a monitor: they watch tone A closely and
     * tone B not at all.
     */
    hf_begin("tone B's even harmonics reach no guard");
    HF_EQ_INT(fold_bin(2 * b), 5);
    HF_EQ_INT(fold_bin(4 * b), HANDOFF_TONE_B_BIN);
    HF_CHECK(fold_bin(2 * b) != HANDOFF_GUARD_LO_BIN);
    HF_CHECK(fold_bin(2 * b) != HANDOFF_GUARD_MID_BIN);
    HF_CHECK(fold_bin(2 * b) != HANDOFF_GUARD_HI_BIN);

    /* Guard 8 is reached by no harmonic of either tone, even or odd: it is
     * the one pure noise cell, which is what a median of three needs. */
    hf_begin("guard 8 is reached by no harmonic of either tone");
    {
        int h;
        for (h = 2; h <= 8; h++) {
            HF_CHECK(fold_bin(h * a) != HANDOFF_GUARD_MID_BIN);
            HF_CHECK(fold_bin(h * b) != HANDOFF_GUARD_MID_BIN);
        }
    }
}
