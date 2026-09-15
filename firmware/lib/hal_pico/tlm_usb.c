/*
 * Handoff — score stream and triggered raw bursts over USB CDC. M4.
 * See tlm.h for the format decision and why it went this way.
 *
 * The score stream is continuous and cheap: 20 kB/s, which full-speed CDC
 * carries without thinking about it. Raw ADC is 1 MB/s and is therefore never
 * continuous — it is captured into RAM on a trigger and dumped afterwards, at
 * whatever rate the host cares to read.
 *
 * Scores go out as text rather than binary. It costs about three times the
 * bytes and buys a stream that survives being looked at in a terminal, which
 * during a §14.1 body test is the difference between noticing that the link
 * died and not. 20 kB/s of text is still a fifth of what CDC will carry.
 */
#include "tlm.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "adc_ring.h"

#define RAW_SAMPLES ((HANDOFF_ADC_FS_HZ / 1000) * TLM_RAW_BURST_MS)

static uint16_t s_decimate;
static uint16_t s_phase;

static int16_t           s_raw[RAW_SAMPLES];
static size_t            s_raw_n;
static volatile bool     s_raw_arming;
static volatile size_t   s_raw_want;      /* samples to take                  */
static volatile uint64_t s_raw_t0;        /* first sample at or after this    */
static uint64_t          s_raw_start_us;  /* when the first taken one was     */

void tlm_usb_init(uint16_t decimate)
{
    s_decimate = decimate;
    s_phase    = 0;
}

void tlm_usb_score(uint16_t score)
{
    if (s_decimate == 0u) return;
    if (++s_phase < s_decimate) return;
    s_phase = 0;
    printf("s %u\n", (unsigned)score);
}

void tlm_usb_event(const char *text)
{
    printf("e %s\n", text ? text : "");
}

/* ---------------------------------------------------------------------- */

void tlm_usb_raw_trigger(void)
{
    tlm_usb_raw_trigger_at(0, RAW_SAMPLES);
}

void tlm_usb_raw_trigger_at(uint64_t t0_us, size_t n)
{
    s_raw_n      = 0;
    s_raw_want   = n > RAW_SAMPLES ? RAW_SAMPLES : n;
    s_raw_t0     = t0_us;
    s_raw_arming = true;
}

bool tlm_usb_raw_busy(void) { return s_raw_arming; }

uint64_t tlm_usb_raw_start_us(void) { return s_raw_start_us; }

/*
 * Called from the sample path with each block. Kept out of the header because
 * only the owner of the block loop can call it, and it must not be mistaken
 * for something the protocol layer may reach for.
 */
void tlm_usb_raw_feed(const int16_t *samples, size_t n, uint64_t first_idx)
{
    size_t room, skip = 0;

    if (!s_raw_arming) return;

    /* Nothing taken yet: skip to the first sample at or after t0. A t0
     * before this block is simply late (the caller armed too late, or asked
     * for time already consumed) and the burst starts here. */
    if (s_raw_n == 0 && s_raw_t0) {
        uint64_t t_first = adc_ring_sample_us(first_idx);
        if (t_first < s_raw_t0) {
            uint64_t d = s_raw_t0 - t_first;
            skip = (size_t)((d * (uint64_t)HANDOFF_ADC_FS_HZ + 999999u) / 1000000u);
            if (skip >= n) return;           /* not in this block yet */
        }
        s_raw_start_us = adc_ring_sample_us(first_idx + skip);
    } else if (s_raw_n == 0) {
        s_raw_start_us = adc_ring_sample_us(first_idx);
    }

    samples += skip;
    n       -= skip;
    room = s_raw_want - s_raw_n;
    if (n > room) n = room;

    memcpy(&s_raw[s_raw_n], samples, n * sizeof s_raw[0]);
    s_raw_n += n;

    if (s_raw_n >= s_raw_want) s_raw_arming = false;
}

void tlm_usb_raw_dump(void)
{
    size_t i;

    printf("r %u\n", (unsigned)s_raw_n);
    for (i = 0; i < s_raw_n; i++) printf("%d\n", (int)s_raw[i]);
    printf("r end\n");
}

const int16_t *tlm_usb_raw_samples(size_t *n)
{
    if (s_raw_arming) { if (n) *n = 0; return 0; }
    if (n) *n = s_raw_n;
    return s_raw;
}

/* ---------------------------------------------------------------------- */

void tlm_sink(void *ctx, hal_tlm_kind_t kind, const void *data, size_t len)
{
    (void)ctx;

    switch (kind) {
    case HAL_TLM_SCORE:
        if (data && len >= sizeof(uint16_t)) tlm_usb_score(*(const uint16_t *)data);
        break;
    case HAL_TLM_EVENT:
        if (data) tlm_usb_event((const char *)data);
        break;
    default:
        break;
    }
}
