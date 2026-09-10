/* Handoff — ADC + DMA ring. STUB until M4. See adc_ring.h. */
#include "adc_ring.h"

void adc_ring_init(void)  { }
void adc_ring_start(void) { }
void adc_ring_stop(void)  { }

const int16_t *adc_ring_next_block(size_t *count)
{
    if (count) *count = 0;
    return 0;
}

uint32_t adc_ring_measured_sps(void)    { return 0; }
uint32_t adc_ring_overruns(void)        { return 0; }
uint32_t adc_ring_noise_floor_lsb(void) { return 0; }
