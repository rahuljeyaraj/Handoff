/* Handoff — core1 -> core0 SPSC ring. STUB until M4. See ipc.h. */
#include "ipc.h"

void     ipc_init(void)                       { }
bool     ipc_push_chip(uint16_t energy)       { (void)energy; return false; }
size_t   ipc_pop_chips(uint16_t *d, size_t m) { (void)d; (void)m; return 0; }
uint32_t ipc_dropped(void)                    { return 0; }
