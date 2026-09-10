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

static size_t h_rx_chips(void *ctx, uint16_t *dst, size_t max)
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
    /* The link state machine runs its own carrier_t over the chip stream; this
     * exists for apps that want a level without one. Report the most recent
     * chip energy in the queue. */
    halh_node_t *n = (halh_node_t *)ctx;
    if (n->rx_tail == n->rx_head) return 0;
    return n->rx[(n->rx_head + HALH_RX_FIFO - 1u) % HALH_RX_FIFO];
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

    n->iface.ctx = n;
    n->iface.tx_drive = h_tx_drive;
    n->iface.tx_chips = h_tx_chips;
    n->iface.tx_busy = h_tx_busy;
    n->iface.rx_chips = h_rx_chips;
    n->iface.rx_carrier_level = h_rx_carrier_level;
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

static void rx_put(halh_node_t *n, uint16_t energy)
{
    const size_t next = (n->rx_head + 1u) % HALH_RX_FIFO;
    if (next == n->rx_tail) return;   /* overrun: the real ring drops too */
    n->rx[n->rx_head] = energy;
    n->rx_head = next;
}

static uint16_t chip_energy(halh_node_t *rx_node, int chip_on)
{
    const halh_chan_t *c = &rx_node->chan;
    int32_t e;

    if (rx_node->dropout_left > 0) {
        rx_node->dropout_left--;
        chip_on = 0;
    } else if (c->dropout_prob > 0.0 && rng_uniform(&rx_node->rng) < c->dropout_prob) {
        rx_node->dropout_left = c->dropout_chips;
        chip_on = 0;
    } else if (c->chip_error_prob > 0.0 && rng_uniform(&rx_node->rng) < c->chip_error_prob) {
        chip_on = !chip_on;
    }

    e = chip_on ? c->energy_on : c->energy_off;
    if (c->noise_lsb) {
        const int32_t span = (int32_t)c->noise_lsb * 2 + 1;
        e += (int32_t)(rng_u32(&rx_node->rng) % (uint32_t)span) - (int32_t)c->noise_lsb;
    }
    if (e < 0) e = 0;
    if (e > 0xFFFF) e = 0xFFFF;
    return (uint16_t)e;
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
        int on = 0;

        if (src->driving && src->tx_sent < src->tx_len) {
            on = src->tx[src->tx_sent] ? 1 : 0;
            src->tx_sent++;
            src->chips_tx++;
        }
        if (!dst) continue;

        /* A node mid-transmission hears its own amplifier, not the far end. */
        if (dst->driving && dst->tx_sent < dst->tx_len)
            rx_put(dst, 0xFFFFu);
        else
            rx_put(dst, chip_energy(dst, on));
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
