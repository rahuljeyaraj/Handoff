/*
 * Handoff — CRC-16/CCITT-FALSE. poly 0x1021, init 0xFFFF, no reflection,
 * no final xor. Check value for "123456789" is 0x29B1.
 *
 * Settled at M1 (development-plan §6): CRC-8 lets roughly 1 in 256 corrupted
 * frames through, and fragmentation runs the check 3-6 times per contact, so
 * CRC-8 would put a visibly wrong name in someone's address book at a rate
 * you would notice in a demo. CRC-16 costs one byte, about 8 ms of airtime.
 */
#ifndef HANDOFF_CRC_H
#define HANDOFF_CRC_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT 0xFFFFu

uint16_t crc16_update(uint16_t crc, uint8_t byte);
uint16_t crc16(const uint8_t *data, size_t len);

#endif /* HANDOFF_CRC_H */
