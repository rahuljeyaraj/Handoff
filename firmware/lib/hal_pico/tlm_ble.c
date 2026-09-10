/*
 * Handoff — decimated score stream over BLE. architecture §11.2, design §13.
 * Development plan M2.
 *
 * design §13 forbids a USB tether to a mains-powered laptop while anyone is
 * touching an electrode, so from M10 onward this is the ONLY way measurements
 * leave the wristband. It is not a convenience.
 *
 * The undecimated score rate is HANDOFF_WINDOW_RATE_HZ — 20 000/s at the M1
 * settings, or 40 kB/s, which is well past what a 30 ms connection interval
 * carries. Decimation is therefore mandatory rather than optional, and the
 * phone sets it with BLE_CTRL_TLM_DECIMATE. A decimation of 0 turns the
 * stream off, which is the state a wristband boots in: nothing is streamed
 * until somebody asks for it.
 *
 * Scores are batched because one notification per score would spend three
 * bytes of ATT header on two bytes of data and still not fit the rate. A
 * block of eight is 16 bytes, which fits one notification at the 23-byte MTU
 * floor with nothing to spare — deliberately, so that the body tests are not
 * relying on an MTU negotiation that design §13's conditions might not get.
 */
#include "tlm.h"

#include <string.h>

#include "ble.h"

/* 8 x uint16 = 16 bytes = one notification at ATT_MTU 23. */
#define TLM_BLE_BLOCK 8

static uint16_t s_decimate;              /* 0 = off, and that is the default */
static uint16_t s_phase;
static uint16_t s_block[TLM_BLE_BLOCK];
static uint8_t  s_filled;
static uint32_t s_dropped;

void tlm_ble_init(uint16_t decimate)
{
    s_decimate = decimate;
    s_phase = 0;
    s_filled = 0;
    s_dropped = 0;
}

void tlm_ble_score(uint16_t score)
{
    if (s_decimate == 0u) return;

    if (++s_phase < s_decimate) return;
    s_phase = 0;

    s_block[s_filled++] = score;
    if (s_filled < TLM_BLE_BLOCK) return;

    /*
     * Dropped rather than queued when the controller is busy. A gap in a
     * score stream is a gap in a §14.1 plot; a backlog is a stall in the
     * receive path, and the receive path is what the plot is measuring.
     */
    if (!ble_notify_telemetry(s_block, sizeof s_block)) s_dropped++;

    s_filled = 0;
}

uint32_t tlm_ble_dropped(void) { return s_dropped; }
