/*
 * Handoff — telemetry sinks. development plan M4, design §10.5 and §13.
 *
 * tlm_usb.c is M4. tlm_ble.c is M2 and is what the body tests actually use.
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

#define TLM_RAW_BURST_MS 100

/* Continuous, cheap. Decimation of 1 sends every score. */
void tlm_usb_init(uint16_t decimate);
void tlm_usb_score(uint16_t score);
void tlm_usb_event(const char *text);

/* Triggered burst. Capture then dump; never both at once. */
void tlm_usb_raw_trigger(void);
bool tlm_usb_raw_busy(void);

/* The only legal path out of a wristband someone is holding (design §13). */
void tlm_ble_init(uint16_t decimate);
void tlm_ble_score(uint16_t score);

/* Route either sink through the HAL, so lib/ never names a transport. */
void tlm_sink(void *ctx, hal_tlm_kind_t kind, const void *data, size_t len);

#endif /* HANDOFF_TLM_H */
