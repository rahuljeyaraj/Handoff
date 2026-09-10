#include "manchester.h"

size_t manchester_encode(const uint8_t *bytes, size_t nbytes,
                         uint8_t *chips, size_t max_chips)
{
    size_t i, out = 0;

    if (nbytes * 8u * MANCHESTER_CHIPS_PER_BIT > max_chips) return 0;

    for (i = 0; i < nbytes; i++) {
        int b;
        for (b = 7; b >= 0; b--) {
            const uint8_t bit = (uint8_t)((bytes[i] >> b) & 1u);
            chips[out++] = bit;              /* mark: on then off */
            chips[out++] = (uint8_t)(bit ^ 1u);
        }
    }
    return out;
}

size_t manchester_decode_chips(const uint8_t *chips, size_t nchips,
                               uint8_t *bytes, size_t max_bytes)
{
    const size_t nbytes = nchips / 16u;
    size_t i;

    if (nchips % 16u) return 0;
    if (nbytes > max_bytes) return 0;

    for (i = 0; i < nbytes; i++) {
        uint8_t v = 0;
        int b;
        for (b = 0; b < 8; b++) {
            const uint8_t f = chips[i * 16u + (size_t)b * 2u];
            const uint8_t s = chips[i * 16u + (size_t)b * 2u + 1u];
            /* 10 -> 1, 01 -> 0, and a violation takes the first chip. */
            v = (uint8_t)((v << 1) | (f && !s ? 1u : (!f && s ? 0u : f)));
        }
        bytes[i] = v;
    }
    return nbytes;
}
