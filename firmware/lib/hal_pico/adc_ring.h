/*
 * Handoff — free-running ADC into a DMA ring. design §10.2. STUB UNTIL M4.
 *
 * adc_set_clkdiv(0) gives 48 MHz / 96 = 500.000 ksps exactly, DMA into a
 * ping-pong ring, Goertzel on core 1.
 *
 * Development plan M4 is blunt about where the risk actually is: 500 k
 * samples/s against a 150 MHz core is 300 cycles per sample and a Goertzel
 * inner iteration is single digits, so the load is a few percent. THE RISK IS
 * DMA, INTERRUPT HANDLING AND RING OVERRUN, NOT THE ARITHMETIC. Hence
 * adc_ring_overruns(), which is the number that closes design §17's second
 * open item — or reinstates the analogue mixer.
 */
#ifndef HANDOFF_ADC_RING_H
#define HANDOFF_ADC_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADC_RING_BLOCK  2048   /* samples per DMA block */
#define ADC_RING_BLOCKS 2      /* ping-pong             */

void adc_ring_init(void);
void adc_ring_start(void);
void adc_ring_stop(void);

/* True while the ring owns the ADC: its channel, its FIFO, its DREQ and
 * its DMA. Anything else that wants a conversion has to wait for a stop,
 * or it takes the converter away from the receiver. See power.c. */
bool adc_ring_running(void);

/* Next full block, or NULL. Samples are DC-centred to int16 for the DSP —
 * goertzel.c rejects DC at any bin but a signed sample keeps the headroom
 * arithmetic in gz_mag2 honest. *seq, if wanted, is the block's ordinal
 * since start(): its first sample is number seq * ADC_RING_BLOCK. */
const int16_t *adc_ring_next_block(size_t *count);
const int16_t *adc_ring_next_block_seq(size_t *count, uint32_t *seq);

/*
 * When sample number idx was converted, on the time_us_64() clock. The ADC
 * clock and the timer both come off the crystal, so this is a count, not a
 * measurement: start() time plus idx / 500 kHz. There is a fixed offset of
 * a few microseconds between the ADC starting and its first result, which
 * M13 reads off the bare divider (its settling is nanoseconds, so whatever
 * it measures is this offset) rather than assuming.
 */
uint64_t adc_ring_sample_us(uint64_t idx);

/* M4 exit criteria: rate within 0.01 % measured over 60 s, and zero drops in
 * ten minutes with a counter proving it rather than an absence of complaints. */
uint32_t adc_ring_measured_sps(void);
uint32_t adc_ring_overruns(void);

/* Bare-ADC noise floor in tenths of an LSB RMS, taken on the on-die
 * temperature sensor (a rail clips the noise and reads 0; ~0.71 V does not).
 * *mean_code gets the raw 12-bit mean, so the log shows the input really was
 * away from the rails. Every
 * later amplitude measurement — M7's amplifier noise, M8's link-budget check
 * — is compared against this one. */
uint32_t adc_ring_noise_floor(int32_t *mean_code);

#endif /* HANDOFF_ADC_RING_H */
