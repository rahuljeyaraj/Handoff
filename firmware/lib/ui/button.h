/*
 * Handoff — the push button, classified by how long it is held.
 *
 * SW2 sits inside the enclosure on GP15 with R15 pulling it up (hardware
 * README): a developer's input, not a wearer's. It carries the one thing the
 * app cannot do when the app cannot reach the band — reset it for a new
 * wearer — plus a battery check. What a press means is decided by its
 * length:
 *
 *   short          released before BUTTON_HOLD_MS          battery check
 *   hold           held past BUTTON_HOLD_MS (5 s)          reset for a new wearer
 *
 * The ACTION fires on release, so a finger that lets go at 4 s has done
 * nothing but a battery check. Crossing the threshold while still held fires
 * a REACHED event at that instant: that is the motor's tap telling the
 * finger "you can let go now" — without it there is no way to know when 5 s
 * has passed. There is no shorter hold: a 2 s "dev mode" toggle used to live
 * here and was removed with the ownership model (docs/band-ownership-brief).
 *
 * Debounce is in firmware (no cap on the board): a level has to hold for
 * BUTTON_DEBOUNCE_MS before it counts. Feed the raw level as often as you
 * like; at most one event comes back per call.
 */
#ifndef HANDOFF_BUTTON_H
#define HANDOFF_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_DEBOUNCE_MS 30u
#define BUTTON_HOLD_MS     5000u

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,           /* released before the hold                  */
    BUTTON_HOLD_REACHED,    /* still held, 5 s crossed                    */
    BUTTON_HOLD,            /* released after 5 s                         */
} button_event_t;

typedef struct {
    bool     raw;           /* last raw level fed                          */
    uint32_t raw_since;     /* when it last changed                        */
    bool     down;          /* the debounced level                         */
    uint32_t down_at;       /* when the debounced press began              */
    bool     crossed;       /* the hold threshold passed this press        */
} button_t;

void           button_init(button_t *b, uint32_t now_ms);
button_event_t button_feed(button_t *b, bool pressed, uint32_t now_ms);

const char *button_event_name(button_event_t ev);

#endif /* HANDOFF_BUTTON_H */
