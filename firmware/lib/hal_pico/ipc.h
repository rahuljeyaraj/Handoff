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
 * What crosses is CHIP ENERGIES, one per chip — the same thing hal_iface_t's
 * rx_chips hands out, which is exactly why test/host's simulator can stand in
 * for this entire side of the chip and the protocol layer cannot tell.
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

/* core 1 */
bool ipc_push_chip(uint16_t energy);

/* core 0 */
size_t ipc_pop_chips(uint16_t *dst, size_t max);

/* Chips dropped because core 0 fell behind. Should be zero; if it is not, the
 * core split of §3.3 is wrong somewhere and the fix is there, not here. */
uint32_t ipc_dropped(void);

#endif /* HANDOFF_IPC_H */
