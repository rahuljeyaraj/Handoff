#include "hal_host.h"

#include <string.h>

/* ---- hal_iface_t bindings --------------------------------------------- */

static void h_tx_drive(void *ctx, bool on)
{
    halh_node_t *n = (halh_node_t *)ctx;
    n->driving = on;
    if (!on) { n->tx_len = 0; n->tx_sent = 0; }
}

static size_t h_tx_chips(void *ctx, const uint8_t *chips, size_t count)
{
    halh_node_t *n = (halh_node_t *)ctx;
    const size_t take = (count > HALH_TX_FIFO) ? HALH_TX_FIFO : count;

    memcpy(n->tx, chips, take);
    n->tx_len = take;
    n->tx_sent = 0;
    n->tx_start_us = *n->clock_us;
    return take;
}

static bool h_tx_busy(void *ctx)
{
    halh_node_t *n = (halh_node_t *)ctx;
    return n->tx_sent < n->tx_len;
}

static size_t h_rx_chips(void *ctx, int32_t *dst, size_t max)
{
    halh_node_t *n = (halh_node_t *)ctx;
    size_t out = 0;

    while (n->rx_tail != n->rx_head && out < max) {
        dst[out++] = n->rx[n->rx_tail];
        n->rx_tail = (n->rx_tail + 1u) % HALH_RX_FIFO;
    }
    n->chips_rx += (uint32_t)out;
    return out;
}

static uint32_t h_rx_carrier_level(void *ctx)
{
    /* The level, which under FSK is whichever tone is being sent — so the
     * magnitude of the last difference, not its sign. Telemetry only. */
    halh_node_t *n = (halh_node_t *)ctx;
    return n->rx_level;
}

/* hal.h's contract: the latch since the last call, or the most recent window
 * if none has closed since. Reading clears the latch and leaves the level. */
static bool h_rx_busy(void *ctx)
{
    halh_node_t *n = (halh_node_t *)ctx;
    const bool was = n->busy_latch || n->pres.busy;
    n->busy_latch = false;
    return was;
}

static void h_rx_presence(void *ctx, uint32_t *signal, uint32_t *noise)
{
    halh_node_t *n = (halh_node_t *)ctx;
    if (signal) *signal = presence_signal_score(&n->pres);
    if (noise)  *noise  = presence_noise_score(&n->pres);
}

static uint64_t h_now_us(void *ctx)
{
    halh_node_t *n = (halh_node_t *)ctx;
    return *n->clock_us;
}

static void h_telemetry(void *ctx, hal_tlm_kind_t kind, const void *p, size_t len)
{
    halh_node_t *n = (halh_node_t *)ctx;
    (void)kind; (void)p; (void)len;
    n->tlm_events++;
}

static void h_random(void *ctx, void *dst, size_t len)
{
    halh_node_t *n = (halh_node_t *)ctx;
    uint8_t *b = (uint8_t *)dst;
    size_t i;

    if (n->forced_len) {
        uint32_t v = n->forced[n->forced_pos];
        n->forced_pos = (uint8_t)((n->forced_pos + 1u) % n->forced_len);
        for (i = 0; i < len; i++) b[i] = (uint8_t)(v >> ((i % 4u) * 8u));
        return;
    }
    for (i = 0; i < len; i++) b[i] = (uint8_t)rng_u32(&n->rng);
}

/* ---- construction ------------------------------------------------------ */

void halh_chan_default(halh_chan_t *c)
{
    c->energy_on        = 200;   /* design §5 link budget at the ADC */
    c->energy_off       = 6;     /* M4 will replace this with a measured floor */
    c->noise_lsb        = 8;
    c->chip_error_prob  = 0.0;
    c->dropout_prob     = 0.0;
    c->dropout_chips    = 0;
}

void halh_init(halh_node_t *n, const char *name, uint64_t *clock_us, uint64_t seed)
{
    memset(n, 0, sizeof *n);
    n->name = name;
    n->clock_us = clock_us;
    n->peer = NULL;
    halh_chan_default(&n->chan);
    rng_seed(&n->rng, seed);
    presence_init(&n->pres);

    n->iface.ctx = n;
    n->iface.tx_drive = h_tx_drive;
    n->iface.tx_chips = h_tx_chips;
    n->iface.tx_busy = h_tx_busy;
    n->iface.rx_chips = h_rx_chips;
    n->iface.rx_carrier_level = h_rx_carrier_level;
    n->iface.rx_busy = h_rx_busy;
    n->iface.rx_presence = h_rx_presence;
    n->iface.now_us = h_now_us;
    n->iface.telemetry = h_telemetry;
    n->iface.random = h_random;
}

void halh_pair(halh_node_t *a, halh_node_t *b, uint64_t *clock_us, uint64_t seed)
{
    *clock_us = 0;
    halh_init(a, "A", clock_us, seed);
    halh_init(b, "B", clock_us, seed ^ 0xA5A5A5A5A5A5A5A5ull);
    a->peer = b;
    b->peer = a;
    /* Paired means touching. Every test that predates beacon.c is testing what
     * happens during a contact, not how one starts, so contact is the default
     * and a rendezvous test switches it off and back on. */
    a->coupled = true;
    b->coupled = true;
}

void halh_set_coupled(halh_node_t *a, halh_node_t *b, bool on)
{
    if (a) a->coupled = on;
    if (b) b->coupled = on;
}

void halh_force_random(halh_node_t *n, const uint32_t *values, size_t count)
{
    size_t i;
    if (count > sizeof n->forced / sizeof n->forced[0])
        count = sizeof n->forced / sizeof n->forced[0];
    for (i = 0; i < count; i++) n->forced[i] = values[i];
    n->forced_len = (uint8_t)count;
    n->forced_pos = 0;
}

/* ---- the medium -------------------------------------------------------- */

/*
 * mag^2 of a Goertzel window that would have produced this score. gz_score_of
 * is score = 2*sqrt(mag2)/N, so this is its inverse, and it is exact enough
 * for a ratio — which is all presence ever takes.
 */
static uint64_t mag2_of_score(uint32_t score)
{
    const uint64_t a = (uint64_t)score * (uint64_t)HANDOFF_GZ_N / 2u;
    return a * a;
}

/*
 * One chip slot, as HANDOFF_WINDOWS_PER_CHIP bank windows. See hal_host.h for
 * what this models and what it deliberately does not.
 */
static void presence_feed(halh_node_t *n, uint16_t energy)
{
    const halh_chan_t *c = &n->chan;
    int w;

    for (w = 0; w < HANDOFF_WINDOWS_PER_CHIP; w++) {
        /* The bank decides guard windows at the window boundary, by count.
         * Same rule here, so the decimation being coprime with the windows
         * per chip is exercised rather than assumed. */
        const bool fresh = (n->pres_windows % HANDOFF_GUARD_DECIM) == 0u;
        uint64_t guard = 0;

        if (fresh) {
            /* An independent draw from the room, never from the signal. */
            int32_t g = (int32_t)c->energy_off;
            if (c->noise_lsb) {
                const int32_t span = (int32_t)c->noise_lsb * 2 + 1;
                g += (int32_t)(rng_u32(&n->rng) % (uint32_t)span)
                   - (int32_t)c->noise_lsb;
            }
            if (g < 0) g = 0;
            guard = mag2_of_score((uint32_t)g);
        }
        n->pres_windows++;
        if (presence_push(&n->pres, mag2_of_score(energy), guard, fresh))
            n->busy_latch = true;
    }
}

static void rx_put(halh_node_t *n, int32_t d, uint16_t level)
{
    const size_t next = (n->rx_head + 1u) % HALH_RX_FIFO;

    presence_feed(n, level);
    n->rx_level = level;

    if (next == n->rx_tail) return;   /* overrun: the real ring drops too */
    n->rx[n->rx_head] = d;
    n->rx_head = next;
}

static uint16_t one_bin(halh_node_t *n, uint16_t base)
{
    const halh_chan_t *c = &n->chan;
    int32_t e = base;

    if (c->noise_lsb) {
        const int32_t span = (int32_t)c->noise_lsb * 2 + 1;
        e += (int32_t)(rng_u32(&n->rng) % (uint32_t)span) - (int32_t)c->noise_lsb;
    }
    if (e < 0) e = 0;
    if (e > 0xFFFF) e = 0xFFFF;
    return (uint16_t)e;
}

/*
 * One chip slot as this node hears it: both bins, then their difference.
 *
 * LINK V2 STEP 6. There is no "off chip" any more. on_air says whether
 * anything at all is arriving; chip says which TONE it is. When nothing is
 * arriving both bins read the off-tone level and their difference is noise
 * with a random sign, which is what a quiet channel looks like to a detector
 * that compares two bins instead of remembering a floor.
 *
 * The two bins are drawn independently, and that is the point: every
 * impairment in this model has to be able to move one bin without the other,
 * or it would be testing a decision the hardware never makes.
 */
static int32_t chip_diff(halh_node_t *n, int on_air, int chip, uint16_t *level)
{
    const halh_chan_t *c = &n->chan;
    uint16_t ea, eb;

    if (n->dropout_left > 0) {
        n->dropout_left--;
        on_air = 0;
    } else if (c->dropout_prob > 0.0 && rng_uniform(&n->rng) < c->dropout_prob) {
        n->dropout_left = c->dropout_chips;
        on_air = 0;
    } else if (c->chip_error_prob > 0.0 && rng_uniform(&n->rng) < c->chip_error_prob) {
        chip = !chip;
    }

    ea = one_bin(n, (on_air && !chip) ? c->energy_on : c->energy_off);
    eb = one_bin(n, (on_air &&  chip) ? c->energy_on : c->energy_off);

    /* presence asks max(E_A, E_B), on hardware and here. */
    *level = (ea > eb) ? ea : eb;
    return (int32_t)eb - (int32_t)ea;
}

/*
 * Push whatever `src` has on air during the next `chips` chip slots into
 * `dst`'s receive queue. Half duplex: a node that is transmitting is deaf,
 * which is the whole reason design §9.7 has a turnaround budget at all.
 */
static void carry(halh_node_t *src, halh_node_t *dst, size_t chips)
{
    size_t i;

    for (i = 0; i < chips; i++) {
        int on_air = 0, chip = 0;

        if (src->driving && src->tx_sent < src->tx_len) {
            chip = src->tx[src->tx_sent] ? 1 : 0;
            on_air = 1;
            src->tx_sent++;
            src->chips_tx++;
        }
        if (!dst) continue;

        /* No contact, no channel. The chip was still transmitted — it just had
         * nowhere to go. */
        if (!src->coupled || !dst->coupled) on_air = 0;

        /*
         * A node mid-transmission hears its own amplifier, not the far end —
         * and under FSK what it hears is its own TONE at full strength, not a
         * saturated level with no sign. link_sm.c's drain_discard() throws it
         * away; a simulator that handed back something with no tone in it
         * would let a missing discard pass.
         */
        if (dst->driving && dst->tx_sent < dst->tx_len) {
            const int mine = dst->tx[dst->tx_sent] ? 1 : 0;
            rx_put(dst, mine ? 0x7FFF : -0x7FFF, 0xFFFFu);
        } else {
            uint16_t level = 0;
            const int32_t d = chip_diff(dst, on_air, chip, &level);
            rx_put(dst, d, level);
        }
    }
}

void halh_advance(halh_node_t *a, halh_node_t *b, uint64_t us)
{
    uint64_t *clock = a ? a->clock_us : (b ? b->clock_us : NULL);
    uint64_t elapsed;
    size_t chips;

    if (!clock) return;

    elapsed = *clock % HANDOFF_CHIP_US;   /* remainder carried from last call */
    *clock += us;
    chips = (size_t)((elapsed + us) / HANDOFF_CHIP_US);

    if (a) carry(a, b, chips);
    if (b) carry(b, a, chips);
}
