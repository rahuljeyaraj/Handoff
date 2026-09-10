#include "crc.h"

/* Bitwise. 16 iterations per byte against a 34-byte frame is ~4400 cycles per
 * frame on core 0, which has 250 ms to spare. A table would cost 512 bytes of
 * flash to save nothing that matters. */
uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    int i;
    crc ^= (uint16_t)byte << 8;
    for (i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;
    size_t i;
    for (i = 0; i < len; i++) crc = crc16_update(crc, data[i]);
    return crc;
}
