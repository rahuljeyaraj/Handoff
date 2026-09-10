/*
 * Handoff — priority-weighted carousel. architecture §8.4.
 *
 * Replaces design §9.5's "send the packet repeatedly". Transmission order is
 * 0, 1, 0, 2, 0, 3, 0, 1, ... — fragment 0 gets half the airtime and the rest
 * share what is left, because fragment 0 alone is a usable contact.
 *
 * The weighting is a sweep at M1 (architecture §13), so it is a parameter:
 * weight is how many slots out of (weight + 1) belong to fragment 0.
 *
 * Keeps design §9.5's best property intact: no acknowledgement, no retry
 * negotiation, no timers. The receiver takes whatever passes CRC.
 */
#ifndef HANDOFF_CAROUSEL_H
#define HANDOFF_CAROUSEL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  count;     /* fragments in the record            */
    uint8_t  weight;    /* fragment-0 slots per other slot    */
    uint8_t  phase;     /* position in the weight+1 cycle     */
    uint8_t  next_tail; /* next fragment from 1..count-1      */
    uint32_t sent;
    uint32_t passes;    /* completed sweeps of 1..count-1     */
} carousel_t;

/*
 * SETTLED AT M1 BY SWEEP, AND NOT THE VALUE ARCHITECTURE §8.4 ASSUMED.
 *
 * The measurement (handoff_sweep carousel) says plain round robin beats every
 * weighting at every contact duration, and by a lot at the one that matters:
 * a full card in 54% of one-second contacts against 0% for 0,1,0,2.
 *
 * The reason is that §8.4 assumed frames were cheap enough to spend half of
 * them on repetition. They are not: at HANDOFF_GZ_N 25 a frame is 156 ms, so
 * a one-second contact carries about six of them in total, and spending three
 * re-sending a fragment the far end already has is three fragments it never
 * gets. Repetition would start paying on a much longer contact, or a much
 * shorter frame.
 *
 * What actually delivers §8.4's promise — a usable contact from a brief touch —
 * is the PRIORITY ORDERING, not the repetition. compact_sort_priority puts FN
 * and the mobile in fragment 0, and round robin still sends fragment 0 first.
 *
 * Left as a parameter because M5 and M6 will re-measure it against a channel
 * that loses frames for real, and repetition may earn its place back there.
 */
#define CAROUSEL_DEFAULT_WEIGHT 0

void    carousel_init(carousel_t *c, uint8_t frag_count, uint8_t weight);
uint8_t carousel_next(carousel_t *c);

/* True once every fragment has been transmitted at least once. The link state
 * machine will not call an exchange finished before this. */
bool    carousel_full_pass(const carousel_t *c);

#endif /* HANDOFF_CAROUSEL_H */
