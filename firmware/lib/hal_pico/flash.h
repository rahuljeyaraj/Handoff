/*
 * Handoff — record persistence in flash. architecture §9. Development plan M2.
 *
 * One compact TLV blob plus a record id in one flash sector. The wristband
 * never stores vCard TEXT, only the compact form it will transmit, so the
 * encoding cost is paid once at provisioning rather than at every handshake.
 *
 * Backs lib/record/store.c, which it registers itself with — see
 * flash_record_bind(). store.c is in the sandboxed half of architecture §3.2
 * and may not name a transport, so the dependency points this way and only
 * this way.
 *
 * NOT THE LAST SECTOR, despite what an earlier draft of this header said.
 * Two things are already living at the end of a Pico 2 W's flash:
 *
 *   PICO_FLASH_SIZE - 1 sector    reserved by the SDK on RP2350 for the
 *                                 erratum RP2350-E10 workaround
 *   the two sectors below that    BTstack's TLV bank, which is where the
 *                                 phone bond lives (pico_btstack_flash_bank)
 *
 * So the record goes in the fourth sector from the end, and flash.c static-
 * asserts that against PICO_FLASH_BANK_STORAGE_OFFSET rather than trusting
 * this comment to stay true. Getting this wrong would not fail at build time
 * or at first boot: it would erase the phone bond every time the wearer
 * re-provisioned their card, which is a bug that only shows up on somebody
 * else's bench a week later.
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

/*
 * Register this as lib/record/store.c's backend. Call once at start-up,
 * before store_load(). Returns false if the sector holds something this build
 * cannot read, which is not a failure — it is an unprovisioned wristband —
 * but is worth printing.
 */
bool flash_record_bind(void);

/* The backend itself. Public so that a bring-up app can poke at it directly;
 * everything above lib/hal_pico should go through store.h instead. */
bool flash_record_load(uint8_t *blob, size_t max, size_t *len, uint8_t *record_id);
bool flash_record_save(const uint8_t *blob, size_t len, uint8_t record_id);
bool flash_record_erase(void);

/* Where the record sector actually is, for the console banner. Both are byte
 * offsets from the start of flash, not XIP addresses. */
uint32_t flash_record_offset(void);
uint32_t flash_record_sector_size(void);

#endif /* HANDOFF_FLASH_H */
