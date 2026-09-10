#include "elect.h"
#include "hal_host.h"
#include "hf_test.h"
#include "tests.h"

void test_elect(void)
{
    hf_begin("elect: silence makes an initiator");
    {
        uint64_t clock = 0;
        halh_node_t n;
        elect_t e;
        uint64_t t;

        halh_init(&n, "solo", &clock, 1);
        elect_init(&e, &n.iface);
        elect_start(&e, 0);

        for (t = 0; t < 20000; t += 100)
            if (elect_poll(&e, t, false) == ELECT_SETTLED) break;

        HF_EQ_INT(elect_role(&e), ELECT_ROLE_INITIATOR);
        HF_CHECK(elect_draw_us(&e) < HANDOFF_BACKOFF_MAX_US);
    }

    hf_begin("elect: a carrier heard during the backoff makes a target");
    {
        uint64_t clock = 0;
        halh_node_t n;
        elect_t e;

        halh_init(&n, "solo", &clock, 1);
        elect_init(&e, &n.iface);
        elect_start(&e, 0);

        HF_EQ_INT(elect_poll(&e, 10, true), ELECT_SETTLED);
        HF_EQ_INT(elect_role(&e), ELECT_ROLE_TARGET);
    }

    hf_begin("elect: a carrier heard during the listen window makes a target");
    {
        uint64_t clock = 0;
        halh_node_t n;
        elect_t e;
        const uint32_t forced = 100;

        halh_init(&n, "solo", &clock, 1);
        halh_force_random(&n, &forced, 1);
        elect_init(&e, &n.iface);
        elect_start(&e, 0);

        HF_EQ_INT(elect_draw_us(&e), 100);
        HF_EQ_INT(elect_poll(&e, 0, false), ELECT_BACKOFF);
        HF_EQ_INT(elect_poll(&e, 150, false), ELECT_LISTEN);
        HF_EQ_INT(elect_poll(&e, 200, true), ELECT_SETTLED);
        HF_EQ_INT(elect_role(&e), ELECT_ROLE_TARGET);
    }

    /*
     * The whole reason hal->random is injectable. Force both ends to draw
     * exactly the same backoff — the tie architecture §7.3 calls nearly
     * untestable — then require a redraw to resolve it.
     */
    hf_begin("elect: a forced tie is resolved by redraw");
    {
        uint64_t clock = 0;
        halh_node_t a, b;
        elect_t ea, eb;
        const uint32_t same = 1000;
        const uint32_t apart_a = 500, apart_b = 4000;

        halh_pair(&a, &b, &clock, 99);
        halh_force_random(&a, &same, 1);
        halh_force_random(&b, &same, 1);

        elect_init(&ea, &a.iface);
        elect_init(&eb, &b.iface);
        elect_start(&ea, 0);
        elect_start(&eb, 0);
        HF_EQ_INT(elect_draw_us(&ea), elect_draw_us(&eb));

        /* Neither hears the other, so both time out as initiator. That is the
         * collision listen-before-talk structurally cannot prevent. */
        elect_poll(&ea, 1000, false);
        elect_poll(&eb, 1000, false);
        elect_poll(&ea, 1000 + ELECT_LISTEN_US, false);
        elect_poll(&eb, 1000 + ELECT_LISTEN_US, false);
        HF_EQ_INT(elect_role(&ea), ELECT_ROLE_INITIATOR);
        HF_EQ_INT(elect_role(&eb), ELECT_ROLE_INITIATOR);

        /* Discovered on air. Redraw, this time landing apart. */
        halh_force_random(&a, &apart_a, 1);
        halh_force_random(&b, &apart_b, 1);
        elect_collision(&ea, 10000);
        elect_collision(&eb, 10000);
        HF_EQ_INT(elect_draw_us(&ea), 500);
        HF_EQ_INT(elect_draw_us(&eb), 4000);

        /* A finishes its backoff first and starts talking; B hears it while
         * still in backoff. */
        elect_poll(&ea, 10500, false);
        elect_poll(&ea, 10500 + ELECT_LISTEN_US, false);
        elect_poll(&eb, 10500 + ELECT_DETECT_US, true);

        HF_EQ_INT(elect_role(&ea), ELECT_ROLE_INITIATOR);
        HF_EQ_INT(elect_role(&eb), ELECT_ROLE_TARGET);
        HF_CHECK(!elect_gave_up(&ea));
    }

    hf_begin("elect: repeated collisions give up rather than spin forever");
    {
        uint64_t clock = 0;
        halh_node_t n;
        elect_t e;
        int i;

        halh_init(&n, "solo", &clock, 1);
        elect_init(&e, &n.iface);
        elect_start(&e, 0);
        for (i = 0; i <= ELECT_MAX_REDRAWS; i++) elect_collision(&e, (uint64_t)i * 10000u);

        HF_CHECK(elect_gave_up(&e));
        HF_EQ_INT(elect_role(&e), ELECT_ROLE_NONE);
    }

    /*
     * The collision rate is a property of design §9.6's numbers, not of this
     * code: two ends collide when their draws land within the carrier
     * detector's latency of each other. Measure it, and measure that redraws
     * converge — a handshake that needed nine attempts would blow the budget.
     */
    hf_begin("elect: collisions are as rare as the backoff range allows");
    {
        const int trials = 4000;
        int trial, collisions = 0, resolved = 0;
        long total_attempts = 0;

        for (trial = 0; trial < trials; trial++) {
            uint64_t clock = 0;
            halh_node_t a, b;
            elect_t ea, eb;
            int attempts = 1;

            halh_pair(&a, &b, &clock, (uint64_t)trial + 1u);
            elect_init(&ea, &a.iface);
            elect_init(&eb, &b.iface);
            elect_start(&ea, 0);
            elect_start(&eb, 0);

            for (;;) {
                const uint32_t da = elect_draw_us(&ea), db = elect_draw_us(&eb);
                const uint32_t gap = (da > db) ? da - db : db - da;

                if (gap >= ELECT_DETECT_US) break;    /* one end hears the other */
                if (attempts == 1) collisions++;
                if (attempts > ELECT_MAX_REDRAWS) break;
                elect_collision(&ea, 0);
                elect_collision(&eb, 0);
                attempts++;
            }
            total_attempts += attempts;
            if (attempts <= ELECT_MAX_REDRAWS) resolved++;
        }

        /*
         * Theory: 1 - (1 - detect/max)^2, which is about 12 % for the derived
         * backoff range in config.h. Asserted at a fifth rather than at the
         * exact figure, but tight enough that reverting the range to design
         * §9.6's flat 5 ms fails here — which is how it was found.
         */
        HF_CHECK_MSG(collisions * 5 < trials,
                     "%d of %d first attempts collided", collisions, trials);
        HF_CHECK_MSG(resolved == trials,
                     "%d of %d handshakes never elected a role",
                     trials - resolved, trials);
        HF_CHECK_MSG(total_attempts * 2 < (long)trials * 3,
                     "mean %.2f attempts to elect a role",
                     (double)total_attempts / (double)trials);
    }
}
