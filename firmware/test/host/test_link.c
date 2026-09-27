#include <stdio.h>
#include <string.h>

#include "hf_test.h"
#include "sim_twonode.h"
#include "tests.h"

static const char k_ada[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "FN:Ada Lovelace\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TITLE:Programmer\r\n"
    "END:VCARD\r\n";

static const char k_bo[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "FN:Bo Tester\r\n"
    "TEL;TYPE=CELL:+1 555 0100\r\n"
    "EMAIL;TYPE=INTERNET:bo@outlook.com\r\n"
    "ORG:Bench Instruments\r\n"
    "END:VCARD\r\n";

void test_link(void)
{
    hf_begin("link: state names cover every state");
    {
        int i;
        for (i = 0; i < LINK_STATE_COUNT; i++)
            HF_CHECK(link_state_name((link_state_t)i)[0] != '?');
    }

    /*
     * The headline: two nodes, one simulated channel, injected time. The
     * carousel runs both ways and both ends end up holding the other's card.
     * No hardware, at M1. Who talks first is given here rather than
     * discovered — see sim_twonode.h, and test_beacon.c for the discovery.
     */
    hf_begin("link: a clean handshake exchanges both cards");
    {
        sim_t s;
        sim_result_t r;
        char seen[1024];

        sim_init(&s, k_ada, k_bo, NULL, 1234);
        r = sim_run(&s, FRAME_AIRTIME_US * 30u);

        HF_CHECK_MSG(r.a_complete && r.b_complete,
                     "A %s, B %s after %u us",
                     link_state_name(s.sm_a.state), link_state_name(s.sm_b.state),
                     (unsigned)r.duration_us);
        HF_CHECK(r.a_has_b);
        HF_CHECK(r.b_has_a);

        /* Exactly one sender and one receiver — never two of either. */
        HF_CHECK_MSG((r.role_a == LINK_ROLE_SENDER) != (r.role_b == LINK_ROLE_SENDER),
                     "roles %d / %d", (int)r.role_a, (int)r.role_b);

        sim_card_a_sees(&s, seen, sizeof seen);
        HF_CHECK(strstr(seen, "FN:Bo Tester\r\n") != NULL);
        HF_CHECK(strstr(seen, "TEL;TYPE=CELL:+15550100\r\n") != NULL);
        HF_CHECK(strstr(seen, "EMAIL;TYPE=INTERNET:bo@outlook.com\r\n") != NULL);

        sim_card_b_sees(&s, seen, sizeof seen);
        HF_CHECK(strstr(seen, "FN:Ada Lovelace\r\n") != NULL);
        HF_CHECK(strstr(seen, "TEL;TYPE=CELL:+447700900123\r\n") != NULL);
        HF_CHECK(strstr(seen, "ORG:Analytical Engines Ltd\r\n") != NULL);
    }

    /*
     * Development plan M14's exit criteria, run here rather than there:
     * 50 handshakes, both parties end up with each other's contact. Seeds
     * differ, so the channel noise and the direction of the first turn differ
     * each time.
     */
    hf_begin("link: 50 handshakes, both ends always get the card");
    {
        int i, both = 0;
        for (i = 0; i < 50; i++) {
            sim_t s;
            sim_result_t r;
            sim_init(&s, k_ada, k_bo, NULL, (uint64_t)i * 7717u + 3u);
            r = sim_run(&s, FRAME_AIRTIME_US * 30u);
            if (r.a_has_b && r.b_has_a) both++;
        }
        HF_CHECK_MSG(both == 50, "only %d of 50 handshakes completed both ways", both);
    }

    /*
     * architecture §8.4's degradation table, as a test. A brief contact must
     * yield a real if sparse contact — a name and a mobile — and never a
     * corrupted one. This is the property that makes the vCard requirement
     * survive contact with physics.
     */
    hf_begin("link: a brief contact still yields a name and a mobile");
    {
        sim_t s;
        char seen[1024];
        int i, usable = 0;

        /*
         * One turn of contact. Only the RECEIVER can have heard anything by
         * then — the sender talks first and does not listen until its turn
         * ends — so the assertion follows the role rather than the letter,
         * which is the difference between testing the protocol and testing
         * the seed.
         */
        for (i = 0; i < 20; i++) {
            const sim_result_t r =
                (sim_init(&s, k_ada, k_bo, NULL, (uint64_t)i * 131u + 5u),
                 sim_run(&s, (uint64_t)(FRAME_AIRTIME_US * 3u)));
            const bool a_is_target = (r.role_a == LINK_ROLE_RECEIVER);
            const size_t n = a_is_target ? sim_card_a_sees(&s, seen, sizeof seen)
                                         : sim_card_b_sees(&s, seen, sizeof seen);
            const char *want_fn  = a_is_target ? "FN:Bo Tester" : "FN:Ada Lovelace";
            const char *want_tel = a_is_target ? "TEL;TYPE=CELL:+15550100"
                                               : "TEL;TYPE=CELL:+447700900123";

            if (n > 0 && strstr(seen, want_fn) && strstr(seen, want_tel)) usable++;
        }
        HF_CHECK_MSG(usable >= 18, "only %d of 20 brief contacts were usable", usable);
    }

    hf_begin("link: too brief a contact yields nothing, never something wrong");
    {
        sim_t s;
        char seen[1024];
        int i;

        for (i = 0; i < 20; i++) {
            sim_init(&s, k_ada, k_bo, NULL, (uint64_t)i * 977u + 11u);
            sim_run(&s, FRAME_AIRTIME_US / 2u);

            if (sim_card_a_sees(&s, seen, sizeof seen) > 0) {
                /* Whatever came out must be a real card, never a mangled one. */
                HF_CHECK(strstr(seen, "BEGIN:VCARD") == seen);
                HF_CHECK(strstr(seen, "END:VCARD") != NULL);
                HF_CHECK_MSG(strstr(seen, "FN:Ada") == NULL,
                             "A somehow received its own card");
            }
        }
    }

    /*
     * §7.2's awkward case: contact breaks mid-exchange. Both ends must return
     * to a defined state within the budget rather than hanging forever.
     */
    hf_begin("link: contact lost mid-exchange terminates cleanly");
    {
        sim_t s;
        link_cfg_t cfg;
        uint64_t t;
        const uint64_t step = HANDOFF_CHIP_US;
        int broke = 0;

        link_cfg_default(&cfg);
        cfg.contact_budget_us = FRAME_AIRTIME_US * 3u;
        sim_init(&s, k_ada, k_bo, &cfg, 4242);

        link_sm_begin(&s.sm_a, s.clock_us, LINK_ROLE_SENDER);
        link_sm_begin(&s.sm_b, s.clock_us, LINK_ROLE_RECEIVER);

        for (t = 0; t < FRAME_AIRTIME_US * 10u; t += step) {
            link_sm_poll(&s.sm_a, s.clock_us);
            link_sm_poll(&s.sm_b, s.clock_us);

            /* Hands come apart part way through: the medium stops carrying. */
            if (!broke && t > FRAME_AIRTIME_US) {
                s.node_a.chan.energy_on = s.node_a.chan.energy_off;
                s.node_b.chan.energy_on = s.node_b.chan.energy_off;
                broke = 1;
            }
            halh_advance(&s.node_a, &s.node_b, step);

            if ((s.sm_a.state == LINK_COMPLETE || s.sm_a.state == LINK_ABORT) &&
                (s.sm_b.state == LINK_COMPLETE || s.sm_b.state == LINK_ABORT))
                break;
        }

        HF_CHECK_MSG(s.sm_a.state == LINK_COMPLETE || s.sm_a.state == LINK_ABORT,
                     "A stuck in %s", link_state_name(s.sm_a.state));
        HF_CHECK_MSG(s.sm_b.state == LINK_COMPLETE || s.sm_b.state == LINK_ABORT,
                     "B stuck in %s", link_state_name(s.sm_b.state));
        HF_CHECK_MSG(t < FRAME_AIRTIME_US * 9u, "took %u us to give up", (unsigned)t);
    }

    /*
     * Two silent receive turns send
     * the exchange back through the trigger rather than through an election
     * redraw, and the half-built record has to survive that trip — it is still
     * that person's card, and architecture §8.4's whole argument is that half a
     * card is worth showing. Getting this wrong is silent: the handshake still
     * terminates, it just hands the wearer nothing.
     */
    hf_begin("link: a record survives falling back into the trigger");
    {
        sim_t s;
        link_cfg_t cfg;
        uint64_t t;
        const uint64_t step = HANDOFF_CHIP_US;
        const uint8_t *blob = NULL;
        size_t held;
        int broke = 0;

        link_cfg_default(&cfg);
        cfg.contact_budget_us = FRAME_AIRTIME_US * 12u;
        sim_init(&s, k_ada, k_bo, &cfg, 4242);

        link_sm_begin(&s.sm_a, s.clock_us, LINK_ROLE_SENDER);
        link_sm_begin(&s.sm_b, s.clock_us, LINK_ROLE_RECEIVER);

        for (t = 0; t < FRAME_AIRTIME_US * 30u; t += step) {
            link_sm_poll(&s.sm_a, s.clock_us);
            link_sm_poll(&s.sm_b, s.clock_us);

            /* Long enough in that fragments have landed, then hands part. */
            if (!broke && t > FRAME_AIRTIME_US * 5u) {
                s.node_a.chan.energy_on = s.node_a.chan.energy_off;
                s.node_b.chan.energy_on = s.node_b.chan.energy_off;
                broke = 1;
            }
            halh_advance(&s.node_a, &s.node_b, step);

            if ((s.sm_a.state == LINK_COMPLETE || s.sm_a.state == LINK_ABORT) &&
                (s.sm_b.state == LINK_COMPLETE || s.sm_b.state == LINK_ABORT))
                break;
        }

        /* The path has to have been taken, or the rest of this proves nothing. */
        HF_CHECK_MSG(s.sm_b.retries > 0,
                     "B never fell back into the trigger, so nothing was tested");
        HF_CHECK_MSG(s.sm_b.trig.beacons > 0,
                     "B fell back but never actually re-armed the trigger");

        held = link_sm_received(&s.sm_b, &blob);
        HF_CHECK_MSG(held > 0,
                     "B threw away the card it had already received when it"
                     " went back through the trigger");

        HF_CHECK_MSG(s.sm_b.state == LINK_COMPLETE || s.sm_b.state == LINK_ABORT,
                     "B stuck in %s", link_state_name(s.sm_b.state));
    }

    /* §7.2: one end reset mid-exchange while the other keeps talking. */
    hf_begin("link: one end restarting mid-exchange does not wedge the other");
    {
        sim_t s;
        uint64_t t;
        const uint64_t step = HANDOFF_CHIP_US;
        int reset_done = 0;

        sim_init(&s, k_ada, k_bo, NULL, 31337);
        link_sm_begin(&s.sm_a, s.clock_us, LINK_ROLE_SENDER);
        link_sm_begin(&s.sm_b, s.clock_us, LINK_ROLE_RECEIVER);

        for (t = 0; t < FRAME_AIRTIME_US * 40u; t += step) {
            link_sm_poll(&s.sm_a, s.clock_us);
            link_sm_poll(&s.sm_b, s.clock_us);

            if (!reset_done && t > FRAME_AIRTIME_US + FRAME_AIRTIME_US / 4u) {
                /* B reboots, and comes back believing it is the sender —
                 * the worst version of this case, since it talks over A. */
                link_sm_begin(&s.sm_b, s.clock_us, LINK_ROLE_SENDER);
                reset_done = 1;
            }
            halh_advance(&s.node_a, &s.node_b, step);

            if (s.sm_a.state == LINK_COMPLETE || s.sm_a.state == LINK_ABORT) break;
        }

        HF_CHECK_MSG(s.sm_a.state != LINK_RX_FRAME && s.sm_a.state != LINK_TX_FRAME,
                     "A left running in %s", link_state_name(s.sm_a.state));
    }

    hf_begin("link: a noisy channel still gets the card across, or nothing");
    {
        int i, complete = 0, corrupt = 0;

        for (i = 0; i < 30; i++) {
            sim_t s;
            link_cfg_t cfg;
            char seen[1024];

            link_cfg_default(&cfg);
            cfg.contact_budget_us = FRAME_AIRTIME_US * 40u;

            /*
             * 5 chips in 10 000. A frame is 624 chips, so about a quarter of
             * frames are destroyed — which is what the carousel's repetition
             * exists for. A whole percent, tried first, kills 998 frames in
             * 1000 and tests nothing but arithmetic.
             */
            sim_init(&s, k_ada, k_bo, &cfg, (uint64_t)i * 5003u + 17u);
            s.node_a.chan.chip_error_prob = 0.0005;
            s.node_b.chan.chip_error_prob = 0.0005;
            sim_run(&s, FRAME_AIRTIME_US * 40u);

            if (frag_rx_complete(&s.sm_b.rx)) complete++;
            if (sim_card_b_sees(&s, seen, sizeof seen) > 0 &&
                strstr(seen, "FN:") != NULL &&
                strstr(seen, "FN:Ada Lovelace") == NULL)
                corrupt++;
        }
        HF_CHECK_MSG(corrupt == 0, "%d handshakes produced a wrong name", corrupt);
        HF_CHECK_MSG(complete >= 24, "only %d of 30 completed at 5e-4 chip error", complete);
    }

    /*
     * The turnaround guard. design §9.7 allows 1 ms for the amplifier to come
     * out of saturation, and a receiver that trusts data during that window
     * will see its own transmission. Count how often the framer accepts a
     * frame it should have been deaf to.
     */
    hf_begin("link: nothing is decoded during the turnaround window");
    {
        sim_t s;
        sim_result_t r;

        sim_init(&s, k_ada, k_bo, NULL, 606);
        r = sim_run(&s, FRAME_AIRTIME_US * 30u);

        HF_CHECK(s.sm_a.turnarounds > 0);
        HF_CHECK(s.sm_b.turnarounds > 0);
        /* Every good frame either end saw must have come from the other, so
         * neither can have received more frames than the other sent. */
        HF_CHECK_MSG(r.frames_a <= s.sm_b.frames_sent,
                     "A got %u frames, B only sent %u",
                     (unsigned)r.frames_a, (unsigned)s.sm_b.frames_sent);
        HF_CHECK_MSG(r.frames_b <= s.sm_a.frames_sent,
                     "B got %u frames, A only sent %u",
                     (unsigned)r.frames_b, (unsigned)s.sm_a.frames_sent);
    }
}
