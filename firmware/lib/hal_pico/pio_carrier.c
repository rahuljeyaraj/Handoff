/* Handoff — carrier generation and gating. STUB until M3. See pio_carrier.h. */
#include "pio_carrier.h"

void     pio_carrier_init(uint32_t carrier_hz)            { (void)carrier_hz; }
void     pio_carrier_send(const uint8_t *chips, size_t n) { (void)chips; (void)n; }
bool     pio_carrier_busy(void)                           { return false; }
void     pio_carrier_drive(bool on)                       { (void)on; }
uint32_t pio_carrier_measure_hz(uint32_t gate_us)         { (void)gate_us; return 0; }
