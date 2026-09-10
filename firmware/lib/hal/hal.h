/*
 * Handoff — the HAL seam. firmware-architecture.md §5.
 *
 * Interface only. No code lives here and nothing above this line may include
 * a Pico SDK header. It is implemented twice:
 *
 *   firmware/lib/hal_pico/   binds to RP2350
 *   firmware/test/host/      binds to the channel simulator
 *
 * This is what lets the link state machine — role election included — be
 * tested at M1 against a simulated channel and then run unmodified at M14.
 */
#ifndef HANDOFF_HAL_H
#define HANDOFF_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

typedef enum {
    HAL_TLM_SCORE,   /* continuous, 16-bit chip/window energies (plan M4) */
    HAL_TLM_RAW,     /* triggered burst of raw ADC                        */
    HAL_TLM_EVENT    /* text or struct, low rate                          */
} hal_tlm_kind_t;

typedef struct hal_iface {
    /* --- transmit --- */

    /* false puts GP2 in high-Z, design §6.3. Not the same as sending zeros:
     * a driven low still loads the shared pad the receiver is listening on. */
    void     (*tx_drive)(void *ctx, bool on);

    /* Queue chips (one byte per chip, 0 or 1). Returns chips accepted. */
    size_t   (*tx_chips)(void *ctx, const uint8_t *chips, size_t n);

    bool     (*tx_busy)(void *ctx);

    /* --- receive --- */

    /* Chip energies, not samples: this is the core-1/core-0 boundary of
     * architecture §3.3, and the layer protocol tests inject at. DSP tests
     * inject one layer lower, at raw samples. Returns chips written. */
    size_t   (*rx_chips)(void *ctx, uint16_t *dst, size_t max);

    /* Smoothed carrier energy, for listen-before-talk (§7.3). */
    uint32_t (*rx_carrier_level)(void *ctx);

    /* --- time --- */

    /* Virtual under test: sim_twonode advances it by hand, so a 5 ms backoff
     * and a 350 ms frame cost no wall-clock time. */
    uint64_t (*now_us)(void *ctx);

    /* --- out of band --- */

    void     (*telemetry)(void *ctx, hal_tlm_kind_t kind, const void *p, size_t n);

    /* Injectable, so election ties can be forced rather than waited for. */
    void     (*random)(void *ctx, void *dst, size_t n);

    void *ctx;
} hal_iface_t;

/* Convenience wrappers. Every caller goes through these, so a NULL entry in a
 * partially implemented HAL is a visible no-op rather than a jump to zero. */

static inline void hal_tx_drive(const hal_iface_t *h, bool on) {
    if (h && h->tx_drive) h->tx_drive(h->ctx, on);
}
static inline size_t hal_tx_chips(const hal_iface_t *h, const uint8_t *c, size_t n) {
    return (h && h->tx_chips) ? h->tx_chips(h->ctx, c, n) : 0;
}
static inline bool hal_tx_busy(const hal_iface_t *h) {
    return (h && h->tx_busy) ? h->tx_busy(h->ctx) : false;
}
static inline size_t hal_rx_chips(const hal_iface_t *h, uint16_t *d, size_t max) {
    return (h && h->rx_chips) ? h->rx_chips(h->ctx, d, max) : 0;
}
static inline uint32_t hal_rx_carrier_level(const hal_iface_t *h) {
    return (h && h->rx_carrier_level) ? h->rx_carrier_level(h->ctx) : 0;
}
static inline uint64_t hal_now_us(const hal_iface_t *h) {
    return (h && h->now_us) ? h->now_us(h->ctx) : 0;
}
static inline void hal_telemetry(const hal_iface_t *h, hal_tlm_kind_t k,
                                 const void *p, size_t n) {
    if (h && h->telemetry) h->telemetry(h->ctx, k, p, n);
}
static inline uint32_t hal_random_u32(const hal_iface_t *h) {
    uint32_t v = 0;
    if (h && h->random) h->random(h->ctx, &v, sizeof v);
    return v;
}

#endif /* HANDOFF_HAL_H */
