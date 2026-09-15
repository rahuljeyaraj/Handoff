/* See button.h. */
#include "button.h"

#include <string.h>

void button_init(button_t *b, uint32_t now_ms)
{
    memset(b, 0, sizeof *b);
    b->raw_since = now_ms;
}

button_event_t button_feed(button_t *b, bool pressed, uint32_t now)
{
    if (pressed != b->raw) {
        b->raw = pressed;
        b->raw_since = now;
    }

    /* the debounced level follows the raw one once it has held long enough */
    if (b->raw != b->down && now - b->raw_since >= BUTTON_DEBOUNCE_MS) {
        b->down = b->raw;
        if (b->down) {
            /* the press began when the level changed, not when we believed it */
            b->down_at = b->raw_since;
            b->crossed = 0;
            return BUTTON_NONE;
        }
        /* release: the action is the highest threshold crossed */
        switch (b->crossed) {
        case 2:  return BUTTON_HOLD2;
        case 1:  return BUTTON_HOLD1;
        default: return BUTTON_SHORT;
        }
    }

    if (b->down) {
        uint32_t held = now - b->down_at;
        if (b->crossed < 1 && held >= BUTTON_HOLD1_MS) { b->crossed = 1; return BUTTON_HOLD1_REACHED; }
        if (b->crossed < 2 && held >= BUTTON_HOLD2_MS) { b->crossed = 2; return BUTTON_HOLD2_REACHED; }
    }
    return BUTTON_NONE;
}

const char *button_event_name(button_event_t ev)
{
    switch (ev) {
    case BUTTON_SHORT:         return "short";
    case BUTTON_HOLD1_REACHED: return "hold1-reached";
    case BUTTON_HOLD2_REACHED: return "hold2-reached";
    case BUTTON_HOLD1:         return "hold1";
    case BUTTON_HOLD2:         return "hold2";
    default:                   return "none";
    }
}
