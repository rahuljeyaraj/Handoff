/*
 * Handoff — carrier presence. Running energy against a tracked noise floor.
 *
 * Two consumers: listen-before-talk during role election (§7.3), and the
 * turnaround guard that must not mistake the tail of our own transmission
 * for the other end starting to talk (§9.7).
 */
#ifndef HANDOFF_CARRIER_H
#define HANDOFF_CARRIER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t level;        /* fast EMA of chip energy                       */
    uint32_t floor;        /* slow, asymmetric EMA — falls fast, rises slow */
    uint8_t  fast_shift;
    uint8_t  slow_shift;
    uint8_t  ratio_num;    /* present when level > floor * ratio_num/8      */
    uint16_t min_delta;    /* ...and at least this many LSB above it        */
    bool     present;
    uint16_t hold;         /* chips of hysteresis remaining                 */
    uint16_t hold_chips;
    bool     primed;
} carrier_t;

void     carrier_init(carrier_t *c);
void     carrier_reset(carrier_t *c);
void     carrier_push(carrier_t *c, uint16_t chip_energy);
bool     carrier_present(const carrier_t *c);
uint32_t carrier_level(const carrier_t *c);
uint32_t carrier_floor(const carrier_t *c);

#endif /* HANDOFF_CARRIER_H */
