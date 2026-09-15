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
            b->crossed = false;
            return BUTTON_NONE;
        }
        /* release: the action is whether the threshold was crossed */
        return b->crossed ? BUTTON_HOLD : BUTTON_SHORT;
    }

    if (b->down && !b->crossed && now - b->down_at >= BUTTON_HOLD_MS) {
        b->crossed = true;
        return BUTTON_HOLD_REACHED;
    }
    return BUTTON_NONE;
}

const char *button_event_name(button_event_t ev)
{
    switch (ev) {
    case BUTTON_SHORT:        return "short";
    case BUTTON_HOLD_REACHED: return "hold-reached";
    case BUTTON_HOLD:         return "hold";
    default:                  return "none";
    }
}
