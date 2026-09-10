/* Handoff — record persistence. STUB until M2. See flash.h. */
#include "flash.h"

bool flash_record_load(uint8_t *b, size_t max, size_t *len, uint8_t *id)
{
    (void)b; (void)max; (void)len; (void)id;
    return false;
}

bool flash_record_save(const uint8_t *b, size_t len, uint8_t id)
{
    (void)b; (void)len; (void)id;
    return false;
}

bool flash_record_erase(void) { return false; }
