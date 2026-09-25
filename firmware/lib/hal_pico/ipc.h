/*
 * Handoff — lock-free SPSC ring, core 1 to core 0. architecture §3.3.
 *
 * STUB UNTIL M4.
 *
 * The core split is the rule this enforces: core 1 owns everything at 2 kHz and
 * above (ADC servicing, Goertzel, symbol sync, carrier detection) and core 0
 * owns everything below (Manchester, framing, CRC, reassembly, the vCard codec,
 * the link state machine, BLE, USB).
 *
 * What crosses is one number per chip — the same thing hal_iface_t's rx_chips
 * hands out, which is exactly why test/host's simulator can stand in for this
 * entire side of the chip and the protocol layer cannot tell.
 *
 * LINK V2 STEP 6: that number is a SIGNED TONE DIFFERENCE, d = E_B - E_A, in
 * the bank's mag^2 units. It was a chip energy under v1, and the change is
 * the whole redesign in one field: a comparison of two bins scored in the
 * same window crosses the boundary instead of a level that core 0 would then
 * have to slice against something remembered.
 *
 * Single producer, single consumer, no locks. A spinlock held on core 1 at the
 * chip rate would be a spinlock held inside the path that must never miss a
 * DMA block.
 */
#ifndef HANDOFF_IPC_H
#define HANDOFF_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPC_RING_CHIPS 1024

void ipc_init(void);

/* core 1. sample_idx is the number of the last ADC sample in the chip, so
 * core 0 can place a chip on the sample clock (M13 needs to know which
 * chips were sampled inside the turnaround window, and the ring's own
 * latency is up to a DMA block). */
bool ipc_push_chip(int32_t d, uint32_t sample_idx);

/* core 0. idx may be NULL when the caller only wants the chips. */
size_t ipc_pop_chips(int32_t *dst, uint32_t *idx, size_t max);

/* Chips dropped because core 0 fell behind. Should be zero; if it is not, the
 * core split of §3.3 is wrong somewhere and the fix is there, not here. */
uint32_t ipc_dropped(void);

#endif /* HANDOFF_IPC_H */
