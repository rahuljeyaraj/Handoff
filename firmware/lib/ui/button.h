/*
 * Handoff — the push button, classified by how long it is held.
 *
 * SW2 sits inside the enclosure on GP15 with R15 pulling it up (hardware
 * README): a developer's input, not a wearer's. It carries the things the
 * app cannot do when the app cannot reach the band — clear the bond — plus
 * a battery check and a dev-mode toggle. What each press means is decided
 * by its length:
 *
 *   short          released before BUTTON_HOLD1_MS         battery check
 *   hold 1         held past BUTTON_HOLD1_MS (2 s)         dev mode toggle
 *   hold 2         held past BUTTON_HOLD2_MS (6 s)         clear the bond
 *
 * The ACTION fires on release, at whichever threshold was last crossed, so
 * holding through 2 s to reach 6 s does not also toggle dev mode. Crossing a
 * threshold while still held fires a REACHED event at that instant: that is
 * the motor's tap telling the finger "you can let go now" — without it there
 * is no way to know when 6 s has passed.
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
#define BUTTON_HOLD1_MS    2000u
#define BUTTON_HOLD2_MS    6000u

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,           /* released before hold 1                    */
    BUTTON_HOLD1_REACHED,   /* still held, 2 s crossed                    */
    BUTTON_HOLD2_REACHED,   /* still held, 6 s crossed                    */
    BUTTON_HOLD1,           /* released after 2 s and before 6 s          */
    BUTTON_HOLD2,           /* released after 6 s                         */
} button_event_t;

typedef struct {
    bool     raw;           /* last raw level fed                          */
    uint32_t raw_since;     /* when it last changed                        */
    bool     down;          /* the debounced level                         */
    uint32_t down_at;       /* when the debounced press began              */
    uint8_t  crossed;       /* 0, 1 or 2: thresholds passed this press     */
} button_t;

void           button_init(button_t *b, uint32_t now_ms);
button_event_t button_feed(button_t *b, bool pressed, uint32_t now_ms);

const char *button_event_name(button_event_t ev);

#endif /* HANDOFF_BUTTON_H */
