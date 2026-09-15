/*
 * lib/ui/button: press lengths, thresholds, debounce. The raw level is fed
 * every millisecond the way the main loop will feed it.
 */
#include "button.h"
#include "hf_test.h"
#include "tests.h"

/* Feed `pressed` from t to t+ms, every ms. Returns the last non-NONE event
 * and when it fired; counts how many fired. */
static button_event_t run(button_t *b, bool pressed, uint32_t *t, uint32_t ms,
                          uint32_t *fired_at, int *count)
{
    button_event_t last = BUTTON_NONE;
    uint32_t end = *t + ms;
    for (; *t != end; (*t)++) {
        button_event_t ev = button_feed(b, pressed, *t);
        if (ev != BUTTON_NONE) {
            last = ev;
            if (fired_at) *fired_at = *t;
            if (count) (*count)++;
        }
    }
    return last;
}

void test_button(void)
{
    button_t b;
    uint32_t t, at;
    int n;

    hf_begin("button: a short press is reported on release, once");
    {
        t = 0; n = 0;
        button_init(&b, t);
        HF_EQ_INT(run(&b, true, &t, 150, &at, &n), BUTTON_NONE);
        HF_EQ_INT(run(&b, false, &t, 500, &at, &n), BUTTON_SHORT);
        HF_EQ_INT(n, 1);
        HF_EQ_INT(at, 150 + BUTTON_DEBOUNCE_MS);
    }

    hf_begin("button: a blip shorter than the debounce is nothing");
    {
        t = 0; n = 0;
        button_init(&b, t);
        run(&b, true, &t, BUTTON_DEBOUNCE_MS - 5, NULL, &n);
        run(&b, false, &t, 500, NULL, &n);
        HF_EQ_INT(n, 0);
        HF_CHECK(!b.down);
    }

    hf_begin("button: bounce inside a press neither restarts nor releases it");
    {
        t = 0; n = 0;
        button_init(&b, t);
        run(&b, true, &t, 100, NULL, &n);
        run(&b, false, &t, 5, NULL, &n);            /* a 5 ms bounce */
        run(&b, true, &t, 5, NULL, &n);
        run(&b, false, &t, 8, NULL, &n);
        HF_EQ_INT(run(&b, true, &t, 1900, &at, &n), BUTTON_HOLD1_REACHED);
        HF_EQ_INT(at, BUTTON_HOLD1_MS);              /* measured from the first edge */
        HF_EQ_INT(n, 1);
    }

    hf_begin("button: hold 1 taps at 2 s and acts on release before 6 s");
    {
        t = 0; n = 0;
        button_init(&b, t);
        HF_EQ_INT(run(&b, true, &t, 3000, &at, &n), BUTTON_HOLD1_REACHED);
        HF_EQ_INT(at, BUTTON_HOLD1_MS);
        HF_EQ_INT(run(&b, false, &t, 500, &at, &n), BUTTON_HOLD1);
        HF_EQ_INT(n, 2);
        HF_EQ_INT(at, 3000 + BUTTON_DEBOUNCE_MS);
    }

    hf_begin("button: hold 2 taps at 2 s and at 6 s, and only hold 2 acts");
    {
        t = 0; n = 0;
        button_init(&b, t);
        run(&b, true, &t, 4000, &at, &n);
        HF_EQ_INT(n, 1);
        HF_EQ_INT(run(&b, true, &t, 4000, &at, &n), BUTTON_HOLD2_REACHED);
        HF_EQ_INT(at, BUTTON_HOLD2_MS);
        HF_EQ_INT(n, 2);
        HF_EQ_INT(run(&b, false, &t, 500, &at, &n), BUTTON_HOLD2);
        HF_EQ_INT(n, 3);
    }

    hf_begin("button: holding past 6 s for a long time fires nothing more");
    {
        t = 0; n = 0;
        button_init(&b, t);
        run(&b, true, &t, 60000, NULL, &n);
        HF_EQ_INT(n, 2);
        HF_EQ_INT(run(&b, false, &t, 100, NULL, &n), BUTTON_HOLD2);
    }

    hf_begin("button: the next press starts clean");
    {
        t = 0; n = 0;
        button_init(&b, t);
        run(&b, true, &t, 7000, NULL, &n);
        run(&b, false, &t, 1000, NULL, &n);
        HF_EQ_INT(n, 3);
        run(&b, true, &t, 100, NULL, &n);
        HF_EQ_INT(run(&b, false, &t, 100, NULL, &n), BUTTON_SHORT);
    }

    hf_begin("button: the clock may wrap during a hold");
    {
        t = 0xFFFFFC00u; n = 0;               /* 1024 ms before the wrap */
        button_init(&b, t);
        HF_EQ_INT(run(&b, true, &t, 3000, &at, &n), BUTTON_HOLD1_REACHED);
        HF_EQ_INT(at, 0xFFFFFC00u + BUTTON_HOLD1_MS);   /* wrapped: 976 */
        HF_EQ_INT(run(&b, false, &t, 100, NULL, &n), BUTTON_HOLD1);
    }

    hf_begin("button: every event has a name");
    {
        HF_EQ_STR(button_event_name(BUTTON_SHORT), "short");
        HF_EQ_STR(button_event_name(BUTTON_HOLD2), "hold2");
        HF_EQ_STR(button_event_name(BUTTON_NONE), "none");
    }
}
