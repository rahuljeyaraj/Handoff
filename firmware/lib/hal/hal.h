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

    /*
     * Chips, not samples: this is the core-1/core-0 boundary of architecture
     * §3.3, and the layer protocol tests inject at. DSP tests inject one
     * layer lower, at raw samples. Returns chips written.
     *
     * LINK V2 STEP 6: a chip is a SIGNED TONE DIFFERENCE, d = E_B - E_A, in
     * the bank's mag^2 units — not an energy. Both bins are scored in the
     * same window, through the same gain and the same body, so the number
     * that crosses here is already a comparison and the framer needs no
     * threshold to read it. int32_t rather than link/frame.h's frame_chip_t
     * because this header sits below link/ and must not reach up into it;
     * frame.h states the contract and the two are asserted equal there.
     */
    size_t   (*rx_chips)(void *ctx, int32_t *dst, size_t max);

    /* The level on the channel, as a Goertzel score. Under link v2 this is
     * max(E_A, E_B) — whichever tone is being sent — and it is telemetry, not
     * a decision: nothing compares it against a remembered number. */
    uint32_t (*rx_carrier_level)(void *ctx);

    /*
     * LISTEN BEFORE TALK, link v2 §6. "Is anyone on the channel?"
     *
     * THE ANSWER COVERS THE WHOLE INTERVAL SINCE YOU LAST ASKED, and that is
     * the contract, not an implementation detail. True if ANY Goertzel window
     * since the last call read busy, or — if no window has closed since — if
     * the most recent one did. Reading clears the first half and leaves the
     * second.
     *
     * Both halves are load-bearing and each one was a bug without the other:
     *
     *   - Without the latch, a caller polling a few hundred times a second
     *     against a detector deciding twenty thousand times a second samples
     *     one window in a hundred. Real traffic is every window, so it would
     *     survive; a short burst would not, and neither would the stated
     *     false-busy rate config.h derives k from.
     *
     *   - Without the level, a caller polling FASTER than windows close reads
     *     true, false, true, false down a continuous carrier, because it
     *     consumed the latch and no window has refilled it yet. That is not
     *     hypothetical: it is what the trigger's TRIG_WAIT saw in the host
     *     simulator, which delivers a chip every 250 us while the state
     *     machine polls every 100. 126 shouts in a row were read as 126
     *     carriers too short to be a shout.
     *
     * The corollary of the latch is that a caller which is deliberately deaf —
     * anything driving the pad, TURNAROUND, the trigger's SETTLE — must still
     * read and discard, exactly as it discards chips. Otherwise our own
     * transmission is waiting in the latch when the ears open. link_sm.c's
     * drain_discard() is where that happens.
     */
    bool     (*rx_busy)(void *ctx);

    /* The same decision's two sides, for a console or a telemetry block.
     * Reading them changes nothing and clears nothing. */
    void     (*rx_presence)(void *ctx, uint32_t *signal, uint32_t *noise);

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
static inline size_t hal_rx_chips(const hal_iface_t *h, int32_t *d, size_t max) {
    return (h && h->rx_chips) ? h->rx_chips(h->ctx, d, max) : 0;
}
static inline uint32_t hal_rx_carrier_level(const hal_iface_t *h) {
    return (h && h->rx_carrier_level) ? h->rx_carrier_level(h->ctx) : 0;
}
static inline bool hal_rx_busy(const hal_iface_t *h) {
    return (h && h->rx_busy) ? h->rx_busy(h->ctx) : false;
}
static inline void hal_rx_presence(const hal_iface_t *h,
                                   uint32_t *signal, uint32_t *noise) {
    if (signal) *signal = 0;
    if (noise)  *noise  = 0;
    if (h && h->rx_presence) h->rx_presence(h->ctx, signal, noise);
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
