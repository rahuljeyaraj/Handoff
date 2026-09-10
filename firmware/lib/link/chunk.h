/*
 * Handoff — chunked transport for the phone link. architecture §11.2.
 *
 * NOT the body link. frame.c frames what crosses skin; this frames what
 * crosses BLE, and the two share nothing but a directory. It lives here
 * because it is pure logic over byte buffers and therefore host-testable,
 * which hal_pico/ble.c can never be — and "chunked reassembly works at the
 * 23-byte ATT MTU floor" is a development plan M2 exit criterion precisely
 * because it is the one that quietly works on the developer's handset and
 * fails on someone else's.
 *
 * The wire format, from architecture §11.2:
 *
 *     byte 0   seq     0-based index of this chunk
 *     byte 1   total   number of chunks in the message, 1..255
 *     byte 2+  payload
 *
 * Every chunk but the last carries a full payload. That is what lets the
 * receiver derive the chunk capacity from chunk 0 and be MTU-agnostic: the
 * sender may pick any capacity it likes — 18 bytes at the ATT floor, 244 at
 * the negotiated maximum — and the same reassembler handles both without
 * being told which.
 *
 * There is no sequence gap recovery. ATT writes and notifications are ordered
 * and acknowledged by the controller, so a gap means the transfer was
 * interrupted, not reordered; the assembler resets and says so rather than
 * splicing two messages together. A seq of 0 always starts a fresh message,
 * so the recovery from any error is simply for the sender to start again.
 */
#ifndef HANDOFF_CHUNK_H
#define HANDOFF_CHUNK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHUNK_HDR_BYTES   2     /* seq | total */
#define CHUNK_MAX_CHUNKS  255

/*
 * Reassembly buffer. Sized for the largest thing the contract carries, which
 * is a provisioning vCard: a realistic card is ~176 bytes of text and the
 * awkward ones in scripts/test.py reach ~300. 1 KB leaves room for a card with
 * an address and a note without letting a phone allocate unbounded RAM on the
 * wristband by lying about `total`.
 */
#define CHUNK_RX_MAX      1024

typedef enum {
    CHUNK_MORE      =  0,   /* accepted; more chunks expected               */
    CHUNK_COMPLETE  =  1,   /* accepted; the message is whole               */
    CHUNK_ERR_HDR   = -1,   /* runt chunk, or total == 0                    */
    CHUNK_ERR_SEQ   = -2,   /* out of order, or total changed mid-message   */
    CHUNK_ERR_SIZE  = -3,   /* the message cannot fit CHUNK_RX_MAX          */
    CHUNK_ERR_RAGGED= -4    /* a non-final chunk that is not payload-full   */
} chunk_res_t;

/* ---- sender ----------------------------------------------------------- */

typedef struct {
    const uint8_t *src;
    uint16_t len;
    uint16_t sent;      /* payload bytes already emitted */
    uint8_t  cap;       /* payload bytes per chunk */
    uint8_t  total;
    uint8_t  next;      /* seq of the chunk chunk_tx_next will emit */
} chunk_tx_t;

/*
 * chunk_bytes is the whole chunk including the 2-byte header — for BLE that
 * is ATT_MTU - 3. src must outlive the iterator; nothing is copied.
 *
 * A zero-length message is one chunk with an empty payload, not zero chunks,
 * so "the phone cleared my card" is transmissible and distinguishable from
 * "the phone sent nothing".
 */
chunk_res_t chunk_tx_init(chunk_tx_t *t, const void *src, size_t len,
                          size_t chunk_bytes);

bool    chunk_tx_done(const chunk_tx_t *t);
uint8_t chunk_tx_total(const chunk_tx_t *t);

/* Writes the next chunk and returns its length, or 0 when there are none
 * left or `max` is too small to hold one. */
size_t  chunk_tx_next(chunk_tx_t *t, uint8_t *out, size_t max);

/* ---- receiver --------------------------------------------------------- */

typedef struct {
    uint8_t  buf[CHUNK_RX_MAX];
    uint16_t len;       /* payload bytes assembled so far */
    uint16_t cap;       /* payload per chunk, learned from chunk 0 */
    uint8_t  total;
    uint8_t  next;      /* seq expected next */
    bool     started;
    bool     complete;
} chunk_rx_t;

void        chunk_rx_init(chunk_rx_t *r);

/*
 * Feed one received chunk. Any negative result has already reset the
 * assembler, so the caller's recovery is to report the error and wait for the
 * sender to restart at seq 0 — never to retry the chunk it just rejected.
 */
chunk_res_t chunk_rx_push(chunk_rx_t *r, const void *chunk, size_t n);

/* NULL until a push returns CHUNK_COMPLETE. */
const uint8_t *chunk_rx_data(const chunk_rx_t *r, size_t *len);

/* Progress, for the status characteristic: 0..total chunks received. */
uint8_t     chunk_rx_received(const chunk_rx_t *r);
uint8_t     chunk_rx_total(const chunk_rx_t *r);

#endif /* HANDOFF_CHUNK_H */
