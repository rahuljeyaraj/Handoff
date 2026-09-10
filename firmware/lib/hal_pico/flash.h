/*
 * Handoff — record persistence in flash. architecture §9. STUB UNTIL M2.
 *
 * One compact TLV blob plus a record id in the last flash sector. The
 * wristband never stores vCard TEXT, only the compact form it will transmit,
 * so the encoding cost is paid once at provisioning rather than at every
 * handshake.
 *
 * Backs lib/record/store.c, which returns STORE_ERR_BACKEND until this is real.
 */
#ifndef HANDOFF_FLASH_H
#define HANDOFF_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Magic and version, so a blob written by an older build is rejected rather
 * than decoded into somebody else half-formed. */
#define FLASH_RECORD_MAGIC   0x48414E44u   /* "HAND" */
#define FLASH_RECORD_VERSION 1u

bool   flash_record_load(uint8_t *blob, size_t max, size_t *len, uint8_t *record_id);
bool   flash_record_save(const uint8_t *blob, size_t len, uint8_t record_id);
bool   flash_record_erase(void);

#endif /* HANDOFF_FLASH_H */
