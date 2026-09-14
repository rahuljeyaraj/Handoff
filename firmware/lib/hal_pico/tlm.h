/*
 * Handoff — telemetry sinks. development plan M4, design §10.5 and §13.
 *
 * tlm_usb.c is M4. tlm_ble.c is M2 and is what the body tests actually use.
 * The BLE stream is off until the phone asks for it with BLE_CTRL_TLM_DECIMATE;
 * see tlm_ble.c for why decimation is mandatory rather than optional.
 *
 * THE FORMAT DECISION, settled at M4, because design §10.5 and §13 are in
 * tension and something had to give:
 *
 *   raw ADC is 1 MB/s, which full-speed USB CDC cannot sustain
 *   design §13 forbids a mains-tethered laptop while anyone touches an electrode
 *
 * So:
 *
 *   SCORE STREAM is the continuous telemetry — 10 000 x 16 bit/s = 20 kB/s.
 *   Cheap enough for USB, and decimated it fits over BLE for the body tests
 *   where USB is not allowed. Every §14.1 plot needs this and nothing more.
 *
 *   RAW ADC is a TRIGGERED BURST: 100 ms into RAM (100 kB), dumped at leisure.
 *   Never continuous.
 */
#ifndef HANDOFF_TLM_H
#define HANDOFF_TLM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/*
 * Long enough to hold a whole frame with room either side: a frame is 156 ms
 * of airtime and a replay of a burst shorter than that can never decode.
 * 200 ms is 200 kB of the 520 kB on RP2350; M4 said 100 ms, which was sized
 * before anyone tried to replay one. Overridable per app.
 */
#ifndef TLM_RAW_BURST_MS
#define TLM_RAW_BURST_MS 200
#endif

/* Continuous, cheap. Decimation of 1 sends every score. */
void tlm_usb_init(uint16_t decimate);
void tlm_usb_score(uint16_t score);
void tlm_usb_event(const char *text);

/* Triggered burst. Capture then dump; never both at once. */
void tlm_usb_raw_trigger(void);
bool tlm_usb_raw_busy(void);
void tlm_usb_raw_dump(void);

/* The captured burst, once raw_busy() has cleared. NULL while capturing.
 * For a bench that wants a number (an RMS, a mean) rather than a dump. */
const int16_t *tlm_usb_raw_samples(size_t *n);

/* The only legal path out of a wristband someone is holding (design §13).
 * decimate 0 disables the stream, and is the state a wristband boots in. */
void tlm_ble_init(uint16_t decimate);
void tlm_ble_score(uint16_t score);

/* Blocks the controller had no room for. A §14.1 plot with holes in it needs
 * to say so rather than look like a quiet channel. */
uint32_t tlm_ble_dropped(void);

/* Route either sink through the HAL, so lib/ never names a transport. */
void tlm_sink(void *ctx, hal_tlm_kind_t kind, const void *data, size_t len);

#endif /* HANDOFF_TLM_H */
