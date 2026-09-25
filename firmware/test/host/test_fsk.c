/*
 * Handoff — link v2 step 2: the two-tone generator's chip words.
 *
 * The brief owes this file: "a host test that builds the chip words and checks
 * both come to 36 000 cycles and that each period is even."
 *
 * config.h derives the words and static-asserts that the arithmetic closes, so
 * why test it as well? For the same reason test_clock.c exists. A static assert
 * can say "this is true"; it cannot say what it was supposed to be, and it
 * cannot be read as a record. More importantly, config.h asserts the ARITHMETIC
 * — that periods times period-cycles is a chip — while the thing that actually
 * has to be true is a property of the PIO PROGRAM: that walking fsk_out
 * instruction by instruction spends exactly that many cycles, with the pad high
 * for exactly half of them.
 *
 * So the model below is a cycle-by-cycle walk of pio_carrier.pio, written out
 * longhand. If someone edits the program — adds an instruction, moves the [1],
 * changes where the two OUTs live — this file disagrees with config.h and says
 * which half went wrong.
 *
 * Why that matters, in one line: an unbalanced half is a duty cycle off 50 %,
 * which puts energy in the EVEN harmonics, and the 2nd harmonic of tone A
 * aliases at 500 ksps straight onto guard bin 7. A contaminated guard is v1's
 * carrier floor wearing a new hat, and removing that floor is the whole point
 * of the redesign.
 */
#include "config.h"
#include "hf_test.h"
#include "tests.h"

/*
 * A cycle-accurate walk of fsk_out, one chip.
 *
 * The program, and what each line costs:
 *
 *      out y, 16                 1     pad low, from the previous chip
 *      set pins, 1               1   ─┐
 *      out isr, 16               1    │ first period, high half
 *      mov x, isr                1    │
 *  first_hi: jmp x--, first_hi   isr+1 ┘
 *      set pins, 0               1   ─┐
 *      mov x, isr                1    │ first period, low half — no jmp y--,
 *  first_lo: jmp x--, first_lo   isr+1 ┘ so one cycle shorter than the loop's
 *  period:
 *      set pins, 1        [1]    2   ─┐
 *      mov x, isr                1    │ high half
 *  hi: jmp x--, hi               isr+1 ┘
 *      set pins, 0               1   ─┐
 *      mov x, isr                1    │ low half
 *  lo: jmp x--, lo               isr+1 │
 *      jmp y--, period           1   ─┘
 *
 * `jmp x--` runs isr+1 times for x preloaded with isr: it jumps while x is
 * non-zero and decrements either way, so the pass that sees zero falls
 * through. `jmp y--, period` likewise runs the loop body y+1 times — once by
 * falling through into it, then y more times.
 *
 * The one cycle spent on `out y, 16` belongs to the PREVIOUS chip's last low
 * half, which is why it is counted as low here: over a run of chips each chip
 * gives one such cycle away and receives one back.
 */
typedef struct {
    long total;      /* cycles in one chip                              */
    long high;       /* of which the pad was driven high                */
    long low;        /* of which the pad was driven low                 */
    long periods;    /* pad periods emitted                             */
    long first_hi;   /* length of the first period's high half          */
    long first_lo;   /* and its low half                                */
    long loop_hi;    /* length of a looped period's high half           */
    long loop_lo;    /* and its low half                                */
} walk_t;

static void walk(long isr, long y, walk_t *w)
{
    long loops = y + 1;          /* fall-through, then y jumps back */

    w->first_hi = 1 + 1 + 1 + (isr + 1);        /* set, out isr, mov, loop */
    w->first_lo = 1 + 1 + (isr + 1);            /* set, mov, loop          */
    w->loop_hi  = 2 + 1 + (isr + 1);            /* set [1], mov, loop      */
    w->loop_lo  = 1 + 1 + (isr + 1) + 1;        /* set, mov, loop, jmp y-- */

    w->high = w->first_hi + loops * w->loop_hi;
    w->low  = 1 + w->first_lo + loops * w->loop_lo;   /* + the out y cycle */
    w->total   = w->high + w->low;
    w->periods = 1 + loops;
}

void test_fsk(void)
{
    walk_t a, b;
    long period_a = HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_A_HZ;
    long period_b = HANDOFF_SYS_CLK_HZ / HANDOFF_TONE_B_HZ;
    long chip     = HANDOFF_SYS_CLK_HZ / HANDOFF_CHIP_RATE_HZ;

    /* ---- the numbers design §4 and brief §3 work out by hand --------- */
    hf_begin("a chip is 36000 system cycles, 250 us at 144 MHz");
    HF_EQ_INT((int)chip, 36000);
    HF_EQ_INT((int)HANDOFF_FSK_CHIP_CYCLES, 36000);
    HF_EQ_INT(1000000 / HANDOFF_CHIP_RATE_HZ, 250);

    hf_begin("each tone period is a whole EVEN number of cycles");
    HF_EQ_INT((int)period_a, 800);
    HF_EQ_INT((int)period_b, 720);
    HF_EQ_INT((int)(period_a % 2), 0);
    HF_EQ_INT((int)(period_b % 2), 0);
    HF_EQ_INT((int)HANDOFF_FSK_PERIOD_A, (int)period_a);
    HF_EQ_INT((int)HANDOFF_FSK_PERIOD_B, (int)period_b);

    hf_begin("a chip is a whole number of periods of either tone");
    HF_EQ_INT((int)(chip % period_a), 0);
    HF_EQ_INT((int)(chip % period_b), 0);
    HF_EQ_INT((int)(chip / period_a), 45);
    HF_EQ_INT((int)(chip / period_b), 50);
    HF_EQ_INT((int)HANDOFF_FSK_PERIODS_A, 45);
    HF_EQ_INT((int)HANDOFF_FSK_PERIODS_B, 50);

    hf_begin("the half-period loop counts are 396 and 356");
    HF_EQ_INT((int)HANDOFF_FSK_ISR_A, 396);
    HF_EQ_INT((int)HANDOFF_FSK_ISR_B, 356);
    HF_EQ_INT((int)(2 * HANDOFF_FSK_ISR_A + HANDOFF_FSK_PERIOD_OVERHEAD),
              (int)period_a);
    HF_EQ_INT((int)(2 * HANDOFF_FSK_ISR_B + HANDOFF_FSK_PERIOD_OVERHEAD),
              (int)period_b);

    /*
     * y is periods - 2, and this is the one number the brief got wrong. Two
     * periods leave the loop: the one carrying the two OUT instructions inside
     * its own halves, and the fall-through into the loop head. With
     * periods - 1 the generator emits one period too many.
     */
    hf_begin("y is periods - 2, because two periods are emitted outside the loop");
    HF_EQ_INT((int)HANDOFF_FSK_Y_A, 43);
    HF_EQ_INT((int)HANDOFF_FSK_Y_B, 48);

    /* ---- the word layout -------------------------------------------- */
    hf_begin("the chip word is y in the top half, the loop count in the bottom");
    HF_EQ_INT((int)(HANDOFF_FSK_WORD_A >> 16), (int)HANDOFF_FSK_Y_A);
    HF_EQ_INT((int)(HANDOFF_FSK_WORD_A & 0xFFFFuL), (int)HANDOFF_FSK_ISR_A);
    HF_EQ_INT((int)(HANDOFF_FSK_WORD_B >> 16), (int)HANDOFF_FSK_Y_B);
    HF_EQ_INT((int)(HANDOFF_FSK_WORD_B & 0xFFFFuL), (int)HANDOFF_FSK_ISR_B);

    hf_begin("the two chip words are 0x002B018C and 0x00300164");
    HF_CHECK(HANDOFF_FSK_WORD_A == 0x002B018CuL);
    HF_CHECK(HANDOFF_FSK_WORD_B == 0x00300164uL);

    hf_begin("both fields fit sixteen bits");
    HF_CHECK(HANDOFF_FSK_ISR_A <= 0xFFFF && HANDOFF_FSK_ISR_B <= 0xFFFF);
    HF_CHECK(HANDOFF_FSK_Y_A <= 0xFFFF && HANDOFF_FSK_Y_B <= 0xFFFF);

    /* ---- walking the program, which is the part that can actually go
     * wrong when someone edits the .pio ------------------------------- */
    walk(HANDOFF_FSK_ISR_A, HANDOFF_FSK_Y_A, &a);
    walk(HANDOFF_FSK_ISR_B, HANDOFF_FSK_Y_B, &b);

    hf_begin("walking fsk_out emits the right number of periods");
    HF_EQ_INT((int)a.periods, 45);
    HF_EQ_INT((int)b.periods, 50);

    hf_begin("walking fsk_out spends exactly one chip of cycles");
    HF_EQ_INT((int)a.total, (int)chip);
    HF_EQ_INT((int)b.total, (int)chip);

    /*
     * The duty cycle, which is the claim the guard bins monitor. High and low
     * must be EXACTLY equal over a chip — not close. An imbalance of one cycle
     * in 36 000 is 56 ppm of chip rate with the same sign every chip, so it
     * walks the chip clock down the frame rather than scattering, and it is
     * also the even-harmonic leak onto guard bin 7.
     */
    hf_begin("the pad is high for exactly half of every chip");
    HF_EQ_INT((int)a.high, (int)a.low);
    HF_EQ_INT((int)b.high, (int)b.low);
    HF_EQ_INT((int)a.high, (int)(chip / 2));
    HF_EQ_INT((int)b.high, (int)(chip / 2));

    /*
     * Where the slack lives, so a bench reading of the pad is not a surprise:
     * every HIGH half is exactly the tone half-period, and the looped period
     * is exact. The first period's low half is one cycle short, and the last
     * one is one cycle long (it carries the next chip's `out y`). They cancel.
     */
    hf_begin("every high half is exactly the tone half-period");
    HF_EQ_INT((int)a.first_hi, (int)(period_a / 2));
    HF_EQ_INT((int)a.loop_hi,  (int)(period_a / 2));
    HF_EQ_INT((int)b.first_hi, (int)(period_b / 2));
    HF_EQ_INT((int)b.loop_hi,  (int)(period_b / 2));

    hf_begin("a looped period is exactly the tone period, both halves equal");
    HF_EQ_INT((int)a.loop_hi, (int)a.loop_lo);
    HF_EQ_INT((int)(a.loop_hi + a.loop_lo), (int)period_a);
    HF_EQ_INT((int)b.loop_hi, (int)b.loop_lo);
    HF_EQ_INT((int)(b.loop_hi + b.loop_lo), (int)period_b);

    hf_begin("the first period's low half is the one cycle the OUTs cost");
    HF_EQ_INT((int)(a.loop_lo - a.first_lo), 1);
    HF_EQ_INT((int)(b.loop_lo - b.first_lo), 1);

    /*
     * The [1] on the looped high `set`, as a negative test. Without it the
     * high half is one cycle shorter than the low, every period, and the duty
     * cycle over a chip is no longer 50 %. This is the single edit that would
     * put tone A's second harmonic into guard bin 7, so it is worth a check
     * that fails loudly rather than a comment.
     */
    hf_begin("dropping the [1] would break the duty cycle, every period");
    {
        long hi_no_delay = 1 + 1 + (HANDOFF_FSK_ISR_A + 1);
        HF_EQ_INT((int)(a.loop_lo - hi_no_delay), 1);
        HF_CHECK(hi_no_delay != a.loop_lo);
    }

    /*
     * And the nine-instruction form the brief proposed, as a record of why it
     * was not used: the two OUTs sit outside the period loop, so a chip is two
     * cycles long. Kept as a test because "it is only two cycles" is exactly
     * the argument that would put it back.
     */
    hf_begin("the nine-instruction form is two cycles long per chip");
    {
        long periods = chip / period_a;
        long naive   = 2 + periods * period_a;      /* out y, out isr, then P periods */
        HF_EQ_INT((int)(naive - chip), 2);
        /* 624 chips of a frame, in whole microseconds of drift. */
        HF_EQ_INT((int)(624 * 2 * 1000000L / HANDOFF_SYS_CLK_HZ), 8);
    }

    /* ---- both symbols must be the same length on the wire ----------- */
    hf_begin("both chips are the same length, so Manchester keeps its edge");
    HF_EQ_INT((int)a.total, (int)b.total);
}
