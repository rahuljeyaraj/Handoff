/*
 * Handoff — core1 -> core0 SPSC ring. M4. See ipc.h.
 *
 * Single producer on core 1, single consumer on core 0, and no lock between
 * them: a spinlock taken at the chip rate would be a spinlock inside the path
 * that must never miss a DMA block, which is the one thing architecture §3.3
 * exists to prevent.
 *
 * Correctness rests on two things and nothing else. IPC_RING_CHIPS is a power
 * of two, so the wrap is a mask rather than a modulo that could be interrupted
 * halfway. And each index is written by exactly one core, with a barrier
 * between filling a slot and publishing it — so the consumer can never see an
 * index that points at a slot the producer has not finished writing.
 */
#include "ipc.h"

#include "config.h"

#include "hardware/sync.h"   /* __dmb */
#include "pico/stdlib.h"

#define IPC_MASK (IPC_RING_CHIPS - 1u)

_Static_assert((IPC_RING_CHIPS & IPC_MASK) == 0u,
    "IPC_RING_CHIPS must be a power of two");

static int32_t           s_buf[IPC_RING_CHIPS];
static uint32_t          s_idx[IPC_RING_CHIPS];
static volatile uint32_t s_head;      /* written by core 1 only */
static volatile uint32_t s_tail;      /* written by core 0 only */
static volatile uint32_t s_dropped;

void ipc_init(void)
{
    s_head = s_tail = 0;
    s_dropped = 0;
}

HANDOFF_HOT_FUNC bool ipc_push_chip(int32_t d, uint32_t sample_idx)
{
    uint32_t h = s_head;
    uint32_t n = (h + 1u) & IPC_MASK;

    /* Full. Drop rather than block: core 1 missing a DMA block to wait for
     * core 0 would trade the error this counts for a worse one. */
    if (n == s_tail) {
        s_dropped++;
        return false;
    }

    s_buf[h] = d;
    s_idx[h] = sample_idx;
    __dmb();                 /* fill the slot before publishing the index */
    s_head = n;
    return true;
}

size_t ipc_pop_chips(int32_t *dst, uint32_t *idx, size_t max)
{
    uint32_t t = s_tail;
    uint32_t h = s_head;
    size_t n = 0;

    while (n < max && t != h) {
        if (idx) idx[n] = s_idx[t];
        dst[n++] = s_buf[t];
        t = (t + 1u) & IPC_MASK;
    }

    if (n) {
        __dmb();             /* consume the slots before freeing them */
        s_tail = t;
    }
    return n;
}

uint32_t ipc_dropped(void) { return s_dropped; }
