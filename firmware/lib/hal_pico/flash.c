/*
 * Handoff — record persistence. architecture §9. See flash.h.
 *
 * One sector, one record, rewritten whole. There is no wear levelling and
 * none is wanted: provisioning is a deliberate user action that happens once
 * or twice in a wristband's life, and flash endurance is 100 000 cycles.
 */
#include "flash.h"

#include <assert.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "pico/multicore.h"

#include "crc.h"
#include "store.h"

/*
 * The fourth sector from the end. See the header for why not the last: the
 * SDK reserves the final sector on RP2350 for the E10 workaround, and
 * BTstack's bond storage takes the two below it.
 */
#define RECORD_SECTOR_OFFSET (PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE)

/*
 * Deriving the offset from PICO_FLASH_BANK_STORAGE_OFFSET rather than writing
 * a literal is what actually prevents the collision — if the SDK moves the
 * Bluetooth bank, this moves with it. The assertion below cannot fail while
 * that derivation stands, and that is the point: it fails the day somebody
 * replaces the expression with a number, which is the edit the header warns
 * about.
 */
_Static_assert(RECORD_SECTOR_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET,
               "the record sector overlaps BTstack's bond storage");
_Static_assert(RECORD_SECTOR_OFFSET % FLASH_SECTOR_SIZE == 0,
               "the record sector is not sector-aligned");

/* The other end of the sandwich, and the one that can only be checked at run
 * time: a firmware image that grew past here would be erased by the first
 * provisioning. Nothing in the build says how big the image is. */
extern char __flash_binary_end;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t len;        /* compact TLV bytes that follow */
    uint8_t  record_id;  /* 0..63, frame.h's field width */
    uint8_t  flags;      /* FLASH_RECORD_FLAG_* (v2) — reserved[0] before it */
    uint8_t  reserved[2];
    uint16_t crc;        /* CRC-16/CCITT over the blob */
    uint16_t hdr_crc;    /* ...and over everything above it */
} flash_record_hdr_t;

/* One page holds the header; the blob follows it. Anything larger than a
 * sector could not be written atomically anyway, and a compact record is a
 * few hundred bytes. */
#define RECORD_MAX_BLOB (FLASH_SECTOR_SIZE - sizeof(flash_record_hdr_t))

static const uint8_t *record_flash(void)
{
    return (const uint8_t *)(XIP_BASE + RECORD_SECTOR_OFFSET);
}

static uint16_t hdr_crc_of(const flash_record_hdr_t *h)
{
    return crc16((const uint8_t *)h, offsetof(flash_record_hdr_t, hdr_crc));
}

/* ---- the write, which has to run out of RAM --------------------------- */

typedef struct {
    const uint8_t *page;   /* FLASH_PAGE_SIZE-aligned staging buffer */
    size_t pages;
    bool erase_only;
} write_req_t;

/*
 * __no_inline_not_in_flash_func because the XIP window is unusable while the
 * flash is being erased or programmed, and code fetched through it is the
 * first thing to notice.
 */
static void __no_inline_not_in_flash_func(do_write)(void *param)
{
    const write_req_t *req = (const write_req_t *)param;
    uint32_t ints = save_and_disable_interrupts();

    flash_range_erase(RECORD_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
    if (!req->erase_only)
        flash_range_program(RECORD_SECTOR_OFFSET, req->page,
                            req->pages * FLASH_PAGE_SIZE);

    restore_interrupts(ints);
}

/*
 * Core 1 is idle at M2. From M4 it runs the Goertzel loop, and it runs it out
 * of XIP, so an erase while it is fetching an instruction hangs it — which is
 * what flash_safe_execute's multicore lockout exists to prevent, and it is
 * why M4 must call flash_safe_execute_core_init() on core 1 at start-up.
 *
 * The choice between the two paths is made at RUN time rather than by
 * PICO_FLASH_ASSUME_CORE1_SAFE, because that macro would keep claiming core 1
 * is safe long after M4 has started it.
 */
static bool run_write(write_req_t *req)
{
    if (multicore_lockout_ready())
        return flash_safe_execute(do_write, req, 1000) == PICO_OK;

    do_write(req);
    return true;
}

/* ---- the backend ------------------------------------------------------ */

bool flash_record_load(uint8_t *blob, size_t max, size_t *len, uint8_t *record_id,
                       bool *haptic_on)
{
    const uint8_t *p = record_flash();
    flash_record_hdr_t h;

    memcpy(&h, p, sizeof h);

    if (h.magic != FLASH_RECORD_MAGIC)     return false;   /* never written */
    if (h.version != FLASH_RECORD_VERSION) return false;   /* an older build */
    if (h.hdr_crc != hdr_crc_of(&h))       return false;   /* torn write */
    if (h.len == 0 || h.len > RECORD_MAX_BLOB) return false;
    if (h.len > max)                       return false;

    if (crc16(p + sizeof h, h.len) != h.crc) return false;

    memcpy(blob, p + sizeof h, h.len);
    *len = h.len;
    *record_id = h.record_id;
    if (haptic_on) *haptic_on = (h.flags & FLASH_RECORD_FLAG_HAPTIC) != 0;
    return true;
}

bool flash_record_save(const uint8_t *blob, size_t len, uint8_t record_id,
                       bool haptic_on)
{
    /*
     * The whole sector is staged in RAM first. flash_range_program writes
     * whole pages, and the header and the first bytes of the blob share one,
     * so there is no useful way to write this incrementally.
     */
    static uint8_t page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
    flash_record_hdr_t h;
    write_req_t req;
    size_t total;

    if (len == 0 || len > RECORD_MAX_BLOB) return false;

    memset(&h, 0, sizeof h);
    h.magic     = FLASH_RECORD_MAGIC;
    h.version   = FLASH_RECORD_VERSION;
    h.len       = (uint16_t)len;
    h.record_id = record_id;
    h.flags     = haptic_on ? FLASH_RECORD_FLAG_HAPTIC : 0u;
    h.crc       = crc16(blob, len);
    h.hdr_crc   = hdr_crc_of(&h);

    total = sizeof h + len;

    /* 0xFF is erased flash, so an interrupted program leaves the tail looking
     * unwritten rather than looking like zeroes. */
    memset(page, 0xFF, sizeof page);
    memcpy(page, &h, sizeof h);
    memcpy(page + sizeof h, blob, len);

    req.page = page;
    req.pages = (total + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    req.erase_only = false;
    return run_write(&req);
}

bool flash_record_erase(void)
{
    write_req_t req;

    req.page = NULL;
    req.pages = 0;
    req.erase_only = true;
    return run_write(&req);
}

uint32_t flash_record_offset(void)      { return RECORD_SECTOR_OFFSET; }
uint32_t flash_record_sector_size(void) { return FLASH_SECTOR_SIZE; }

/* ---- binding ---------------------------------------------------------- */

static const store_backend_t k_backend = {
    flash_record_load,
    flash_record_save,
    flash_record_erase,
};

bool flash_record_bind(void)
{
    flash_record_hdr_t h;

    /* See __flash_binary_end above. A wristband whose firmware has grown into
     * the record sector must not quietly erase itself at provisioning time. */
    assert((uintptr_t)&__flash_binary_end - XIP_BASE <= RECORD_SECTOR_OFFSET);

    store_set_backend(&k_backend);

    memcpy(&h, record_flash(), sizeof h);
    return h.magic == FLASH_RECORD_MAGIC
        && h.version == FLASH_RECORD_VERSION
        && h.hdr_crc == hdr_crc_of(&h);
}
