#include "crc.h"
#include "hf_test.h"
#include "tests.h"

#include <string.h>

void test_crc(void)
{
    /* The published check value for CRC-16/CCITT-FALSE. If this fails, the
     * variant is wrong, and every capture ever taken is worthless. */
    hf_begin("crc: check value");
    HF_EQ_INT(crc16((const uint8_t *)"123456789", 9), 0x29B1);

    hf_begin("crc: empty");
    HF_EQ_INT(crc16((const uint8_t *)"", 0), CRC16_INIT);

    hf_begin("crc: incremental matches bulk");
    {
        const uint8_t buf[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF };
        uint16_t c = CRC16_INIT;
        size_t i;
        for (i = 0; i < sizeof buf; i++) c = crc16_update(c, buf[i]);
        HF_EQ_INT(c, crc16(buf, sizeof buf));
    }

    /*
     * The property that actually matters on this link: any single bit flipped
     * anywhere in a full frame body must change the checksum. CRC-16/CCITT
     * detects every 1- and 2-bit error in a message this short by
     * construction, so this is a guard against a broken implementation rather
     * than a discovery.
     */
    hf_begin("crc: every single-bit error in a frame body is detected");
    {
        uint8_t buf[34];
        uint16_t base;
        size_t i;
        int bit;

        for (i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 7u + 3u);
        base = crc16(buf, sizeof buf);

        for (i = 0; i < sizeof buf; i++) {
            for (bit = 0; bit < 8; bit++) {
                uint16_t c;
                buf[i] ^= (uint8_t)(1u << bit);
                c = crc16(buf, sizeof buf);
                buf[i] ^= (uint8_t)(1u << bit);
                HF_CHECK_MSG(c != base, "bit %d of byte %u undetected",
                             bit, (unsigned)i);
            }
        }
    }

    /*
     * And the reason CRC-16 was chosen over CRC-8 (development plan §6): with
     * an 8-bit check, 1 in 256 random corruptions passes. Fragmentation runs
     * the check 3-6 times per contact, so that is a visibly wrong name in
     * someone's address book at a rate a demo would find. Measure the 16-bit
     * false-accept rate over random bodies and confirm it is far below.
     */
    hf_begin("crc: random corruption is rejected");
    {
        uint8_t buf[34];
        uint32_t st = 12345, trials = 0, accepted = 0;
        uint16_t good;
        int t;

        for (t = 0; t < (int)sizeof buf; t++) buf[t] = (uint8_t)t;
        good = crc16(buf, sizeof buf);

        for (t = 0; t < 200000; t++) {
            uint8_t alt[34];
            st = st * 1103515245u + 12345u;
            memcpy(alt, buf, sizeof buf);
            alt[(st >> 16) % sizeof buf] ^= (uint8_t)(st >> 8);
            alt[(st >> 3) % sizeof buf]  ^= (uint8_t)(st >> 24);
            if (memcmp(alt, buf, sizeof buf) == 0) continue;
            trials++;
            if (crc16(alt, sizeof buf) == good) accepted++;
        }
        HF_CHECK_MSG(accepted * 256u < trials,
                     "false accepts %u in %u — worse than CRC-8 would be",
                     (unsigned)accepted, (unsigned)trials);
    }
}
