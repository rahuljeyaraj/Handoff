/*
 * Handoff — link v2 presence. CFAR against the guard bins, no floor.
 * link-v2-design.md §6; brief §6 (step 5).
 *
 * This file replaces dsp/carrier.c, which is deleted rather than fixed.
 *
 * v1 asked "is this louder than I remember?" and had to carry a remembered
 * number to answer it. That number was the floor, the floor was reachable by
 * the signal, and every fault this branch exists to remove came out of that
 * one fact. v2 asks "is this louder than the room is, RIGHT NOW":
 *
 *      signal = max(E_A, E_B)                  bins 9 and 10, mag^2
 *      noise  = median(E_140, E_160, E_220)    bins 7, 8, 11, averaged
 *      busy   = signal > k * noise
 *
 * Everything on the right is measured in the same windows, through the same
 * amplifier, the same body and the same gain. Nothing is remembered about the
 * room except the room's own noise, and nothing we transmit can enter the bins
 * that measure it.
 *
 * NO HYSTERESIS, NO HOLD, NO PRIME, NO FREEZE, NO RESET. Every one of those
 * exists in v1 to protect a floor the signal could poison. There is no floor
 * here to protect. busy is this window's verdict and nothing else.
 *
 * ---------------------------------------------------------------------------
 * AVERAGING THE GUARDS IS SAFE IN A WAY v1's FLOOR NEVER WAS. Read this before
 * "fixing" the boxcar out.
 *
 * The lesson of the floor bug was never that averaging is wrong. It was that
 * averaging a channel THE SIGNAL CAN REACH is wrong: v1's average ran on the
 * carrier's own bin, so a carrier that lasted a whole frame pulled the average
 * up to meet it and the detector went deaf to the thing it was listening for.
 *
 * Bins 7, 8 and 11 are clear of every odd harmonic of both tones that folds
 * back into the band (config.h static-asserts it), and the even harmonics are
 * null at the 50.0000 % duty the generator was measured to hold at the pad.
 * Our transmitter cannot enter them. So a long average there is a better
 * estimate of the room and carries none of v1's failure mode.
 *
 * ---------------------------------------------------------------------------
 * WHERE k COMES FROM. It is computed from a stated false-busy rate. Nothing
 * here was chosen because it worked on a bench (design §1).
 *
 * For cell-averaging CFAR over N reference cells of exponentially distributed
 * power — which is what mag^2 of Gaussian noise in one bin is — the null
 * distribution of signal/mean(reference) is known in closed form:
 *
 *      P_fa = (1 + k/N)^-N        so        k = N * (P_fa^(-1/N) - 1)
 *
 * The three numbers that go in are all in config.h with their own working:
 *
 *      the stated rate    one false busy per HANDOFF_CFAR_FALSE_BUSY_S of
 *                         continuous listening
 *      the decision rate  HANDOFF_WINDOW_RATE_HZ — one verdict per window
 *      N                  HANDOFF_CFAR_CELLS, the guard windows in one
 *                         preamble
 *
 * giving HANDOFF_CFAR_K_NUM / HANDOFF_CFAR_K_DEN. test_presence.c recomputes
 * the formula in double and fails if the constant has drifted from it, so
 * changing the stated rate and not the constant is a test failure rather than
 * a quietly detuned link — the same way config.h treats every derived rate.
 *
 * WHAT THE BENCH SAYS ABOUT THAT k, AND WHY IT IS NOT CORRECTED. Step 4
 * measured the three guards silent at 116 / 88 / 52 LSB on bins 7 / 8 / 11 —
 * a stable 2.3:1 slope falling with frequency, which is this project's 1/f pad
 * noise seen five bins at a time. Two consequences, both stated rather than
 * compensated:
 *
 *   - The median of three is not an average of three. On a monotone slope it
 *     selects bin 8 every time, so the reference IS bin 8, and bins 7 and 11
 *     are doing the job CFAR wants a median for: outlier protection. One
 *     interferer landing in one guard cannot move the estimate. That is not a
 *     nicety — step 3 measured median(2, 23, 391) = 23 where the mean would
 *     have been 138, and 138 is a deafened receiver.
 *
 *   - Bin 8 sits ABOVE the tone bins on that slope: 88 against 67 and 60 at
 *     the same moment. So the reference over-estimates the noise actually in
 *     bins 9 and 10 by roughly 1.4x in amplitude, near 2x in power. That
 *     biases the detector toward SAFE — the realised false-busy rate is better
 *     than stated, at a cost of a few dB of sensitivity. It is not corrected
 *     with a constant: a per-board or per-slope correction would be a tuned
 *     number wearing a different hat, and design §11 names that as a way this
 *     design fails its own rule. If the few dB ever has to be recovered, it is
 *     recovered by measuring the slope in the same run, never by typing it.
 *
 * ---------------------------------------------------------------------------
 * NO SQUARE ROOTS ON THE DECISION PATH. Everything above is a ratio, and a
 * ratio can be taken on mag^2 by cross-multiplication. gz_isqrt64 is reached
 * only by the two score() calls at the bottom, which are telemetry.
 */
#ifndef HANDOFF_PRESENCE_H
#define HANDOFF_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "gz_bank.h"

typedef struct {
    /*
     * The reference: the last HANDOFF_CFAR_CELLS guard medians, as a boxcar.
     *
     * A boxcar of a stated length and not an EMA, because the CFAR formula
     * above is stated for N cells and a boxcar of N is exactly that. An EMA
     * would need an effective-N to put back into the formula, and its shift
     * would be a time constant — which is the shape of every v1 constant this
     * branch is removing.
     */
    uint64_t ref[HANDOFF_CFAR_CELLS];
    uint64_t sum;              /* of ref[], carried rather than re-summed   */
    uint16_t idx;              /* next cell to overwrite                    */
    uint16_t filled;           /* cells written; busy is false below N      */

    uint64_t signal;           /* last window's max(E_A, E_B), mag^2        */
    uint64_t noise;            /* last guard median pushed, mag^2           */
    bool     busy;             /* that window's verdict                     */

    uint32_t windows;          /* windows scored since init                 */
    uint32_t cells;            /* guard windows taken into the reference    */
    uint32_t busy_windows;     /* of windows, the ones that read busy       */

    /*
     * THE PEAK, AND WHY A DETECTOR THAT NEEDS NO MEMORY STILL KEEPS ONE.
     *
     * Nothing in the decision reads these. They exist because a reader two
     * hops away — the phone's Body link page, twice a second — is sampling a
     * beacon that is on air for eleven milliseconds. On 25 Sep 2026 that
     * reader drew a flat line under the threshold while the band was in fact
     * tripping its detector about seven times a second: not a wrong number,
     * a number taken at the wrong moments. The peak is every moment.
     *
     * The pair is taken together, at the same window, because a peak signal
     * printed beside a reference from a different instant is not the
     * comparison the detector made. mag^2 on both, so the hot path stays a
     * compare and two stores and the square roots wait for a reader.
     */
    uint64_t peak_signal;      /* max signal since the last take            */
    uint64_t peak_sum;         /* the reference sum as it stood at THAT peak */
    uint16_t peak_filled;      /* ...and the cell count to divide it by     */
} presence_t;

void presence_init(presence_t *p);

/*
 * One completed bank window. signal_mag2 is max(E_A, E_B); guard_mag2 is the
 * median of the three guards, and is taken into the reference only when
 * guard_fresh — the guards are decimated, so between guard windows the bank
 * holds its last value and counting it again would weight one reading
 * HANDOFF_GUARD_DECIM times.
 *
 * Returns the verdict for this window, which is also left in p->busy.
 *
 * Before the reference has HANDOFF_CFAR_CELLS cells in it the answer is
 * always false. That is a startup transient of one preamble's airtime, and
 * the alternative — deciding against a part-filled reference — is a threshold
 * built from fewer cells than the k it is being compared with.
 */
bool presence_push(presence_t *p, uint64_t signal_mag2, uint64_t guard_mag2,
                   bool guard_fresh);

/* The same, straight off a bank whose window has just closed. */
bool presence_push_bank(presence_t *p, const gz_bank_t *b);

static inline bool presence_busy(const presence_t *p) { return p->busy; }

/* Is the reference full — i.e. is a false answer an answer at all? */
static inline bool presence_ready(const presence_t *p) {
    return p->filled >= HANDOFF_CFAR_CELLS;
}

/*
 * Telemetry, and only telemetry: both take a square root.
 *
 * The noise score is the MEAN of the boxcar, not its last cell, because the
 * mean is what the decision is taken against. A console that printed the last
 * median beside the signal would be printing a different comparison from the
 * one the detector made.
 */
uint32_t presence_signal_score(const presence_t *p);
uint32_t presence_noise_score(const presence_t *p);

/*
 * The highest signal seen since the last call, and the reference it stood
 * against in that same window, both as scores. Takes: the peak is reset, so
 * two readers would steal windows from each other and there is exactly one.
 *
 * Zero for both when no window has closed since the last take — which a
 * reader must not confuse with a quiet room, so it is worth saying that an
 * idle detector never reports a peak of zero, it reports the room.
 */
void presence_take_peak(presence_t *p, uint32_t *signal, uint32_t *noise);

#endif /* HANDOFF_PRESENCE_H */
