/*
 * Handoff — BLE GATT service. architecture §11.2. Development plan M2.
 *
 * The phone client is settled: a NATIVE ANDROID APP, no web client, no iOS
 * (architecture §11). A backgrounded browser tab drops the GATT link, and the
 * phone is in a pocket during a handshake — which is the one thing this link
 * has to survive. The app lives in android/ and is not built by the Pico SDK.
 *
 * The firmware is insulated from that decision regardless: it exposes this
 * service and nothing above it.
 *
 * The contract:
 *
 *   my_vcard    write, chunked   the phone provisions the wristband (§9)
 *   rx_vcard    notify, chunked  received contact, as reconstructed vCard text
 *   status      notify           link state, last score, fragment bitmap, errors
 *   telemetry   notify           decimated score stream for §14.1 body tests
 *   control     write            carrier select, trigger raw capture, force role
 *
 * The UUIDs and the security properties are in handoff.gatt, which is the
 * actual contract with android/ and is compiled into the attribute database.
 *
 * Chunking: ATT MTU is not guaranteed above 23 bytes, so every chunked
 * characteristic carries a 2-byte seq|total header and reassembles at the far
 * end. The framing itself is lib/link/chunk.c — host-testable, and tested at
 * the 23-byte floor rather than at whatever a developer's handset negotiates.
 * This file only binds it to ATT.
 *
 * telemetry exists because design §13 forbids a mains-tethered USB laptop
 * while anyone is touching an electrode. During body tests BLE is the only
 * legal way data leaves the wristband.
 */
#ifndef HANDOFF_BLE_H
#define HANDOFF_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chunk.h"
#include "gz_bank.h"    /* GZB_BINS: the bank block is one entry per bin */

#define BLE_CHUNK_HDR_BYTES CHUNK_HDR_BYTES
#define BLE_MIN_ATT_MTU     23   /* the floor everything must work at */

/* An ATT notification spends 3 bytes on opcode and handle. Everything else in
 * the MTU is ours, and chunk.c takes 2 of what is left. */
#define BLE_ATT_NOTIFY_OVERHEAD 3
#define BLE_CHUNK_BYTES(mtu)    ((size_t)(mtu) - BLE_ATT_NOTIFY_OVERHEAD)

/* The largest vCard the phone may provision, and the largest we will notify.
 * Bounded by the reassembly buffer at the far end, not by BLE. */
#define BLE_VCARD_MAX CHUNK_RX_MAX

/* ---- control opcodes -------------------------------------------------- */

/*
 * One byte of opcode, then that opcode's arguments. Unknown opcodes are
 * counted and ignored rather than refused, so an app built against a later
 * firmware does not have to feature-detect before it can connect.
 *
 * Most of these are the hooks the later milestones bind to. They are listed
 * now because architecture §1 says the seams are created before they are
 * filled, and an app that can already send them is one less thing to change
 * at M4 and M14.
 */
typedef enum {
    BLE_CTRL_NOP          = 0x00,
    BLE_CTRL_CARRIER      = 0x01,  /* u8: 0 = 40 kHz, 1 = 200 kHz     (M3)  */
    BLE_CTRL_RAW_TRIGGER  = 0x02,  /* no args, triggered burst        (M4)  */
    BLE_CTRL_FORCE_ROLE   = 0x03,  /* u8: 0 = target, 1 = initiator   (M14) */
    BLE_CTRL_TLM_DECIMATE = 0x04,  /* u16 LE, 0 disables telemetry    (M4)  */
    BLE_CTRL_FAKE_RX      = 0x05,  /* u8 delay seconds                (M2)  */
    BLE_CTRL_FORGET       = 0x06,  /* erase the provisioned record    (M2)  */
    BLE_CTRL_HAPTIC       = 0x07,  /* u8: 0 = off, 1 = on, persisted (review item 11) */
    BLE_CTRL_IDENTIFY     = 0x08,  /* no args: flash and buzz this band (lib/ui) */
    BLE_CTRL_ZERO_STATS   = 0x0A,  /* no args: the console `z`        (bench) */
    BLE_CTRL_LINK         = 0x0B,  /* u8: 0 = link off, 1 = on        (bench) */
    BLE_CTRL_RESET        = 0x09   /* no args: reset for a new wearer — bond, card,
                                      held card, preferences; the band then drops
                                      the link. The app's "Forget this band"
                                      (docs/band-ownership-brief.md §3). */
} ble_ctrl_op_t;

/* ---- status ----------------------------------------------------------- */

#define BLE_STATUS_VERSION 2

#define BLE_ST_ENCRYPTED   0x01u  /* the link is bonded and encrypted        */
#define BLE_ST_PROVISIONED 0x02u  /* a record is stored and transmittable    */
#define BLE_ST_FLASH_OK    0x04u  /* the flash backend registered at boot    */
#define BLE_ST_TLM_ON      0x08u  /* telemetry notifications are subscribed  */
#define BLE_ST_USB_POWER   0x10u  /* VBUS present at the Pico (v2). NOT charging: the
                                     charger is off-board on J5 and invisible here */
#define BLE_ST_HAPTIC_ON   0x20u  /* the motor fires on a shared/received card (review item 11) */
/* 0x40 was the button's dev mode, removed 15 Sep 2026 (band-ownership §5).
 * Left unused rather than renumbering: the app parses these by value. */

/*
 * 20 bytes, which is exactly one notification at the 23-byte ATT floor
 * (3 bytes of opcode and handle). Little-endian, and versioned, because
 * android/ parses it by offset.
 *
 * Version 1 was the first 16 bytes. Version 2 appended the supply and the
 * firmware version; the app reads any version >= 1 by offset and ignores
 * what it does not know, so a field is only ever appended, never moved.
 * There is no room left at the floor: a version 3 needs a second notify.
 */
typedef struct {
    uint8_t  version;
    uint8_t  flags;         /* BLE_ST_*                                     */
    uint8_t  link_state;    /* proto/link_sm.h state; IDLE until M13         */
    uint8_t  record_id;     /* the wristband's own record, 0..63             */
    uint16_t last_score;    /* most recent Goertzel score      (M4 onward)   */
    uint16_t own_blob_len;  /* compact TLV bytes stored                      */
    uint32_t frag_bitmap;   /* reassembly of the incoming record  (§8.3)     */
    uint16_t chunk_errors;  /* phone-link chunks rejected since boot         */
    uint16_t frame_errors;  /* body-link CRC failures since boot             */
    /* ---- version 2 ---- */
    uint8_t  vsys_20mv;     /* VSYS after D1, in 20 mV steps (0..5.1 V); 0 = not read */
    uint8_t  fw_major;      /* HANDOFF_FW_VERSION_*                          */
    uint8_t  fw_minor;
    uint8_t  fw_patch;
} ble_status_t;

_Static_assert(sizeof(ble_status_t) == 20, "status must fit one notify at the 23-byte floor");

/* ---- bench block, on the telemetry characteristic --------------------- */

/*
 * The numbers a body-coupled bench run actually needs, pushed over telemetry
 * so they leave the wristband by radio. design §13 forbids a USB tether to a
 * mains-powered PC while anyone touches an electrode, and 24 Sep 2026 showed
 * why it is not merely a safety rule: tethered, both bands share the PC ground
 * and that wire IS the return path under test, so chip energy read 1530 and
 * carrier level 144 while ZERO frames decoded. The readings were not just
 * unsafe, they were wrong. See docs/link-debug-brief.md.
 *
 * WHY TELEMETRY AND NOT STATUS. ble_status_t is full: 20 bytes is one notify
 * at the 23-byte ATT floor and its own comment says a version 3 needs a
 * second notify. telemetry was reserved for "the §14.1 body tests" from M2
 * and the handoff app never filled it, so it is free.
 *
 * A block is tagged because the score stream in tlm_ble.c also rides this
 * characteristic, as does ble_trig_t below. A score block is 16 untagged
 * bytes; anything starting with BLE_BENCH_TAG is one of these.
 *
 * READ signal AGAINST noise_ref, AND KNOW WHAT THE PAIR IS NOW. Link v2
 * step 5 deleted dsp/carrier.c and with it the remembered floor: there is no
 * floor, no gate, no ratio and no min_delta anywhere in this design. The pair
 * is CFAR (dsp/presence.h) — signal is max(E_A, E_B) on the two tone bins,
 * noise_ref is the mean of the guard-bin boxcar, and the detector called it
 * busy when signal > k * noise_ref with k = HANDOFF_CFAR_K_NUM/_K_DEN. Both
 * sides are measured in the same windows through the same body, so "both
 * high" is a loud room and not, as it was under v1, a poisoned floor.
 *
 * VERSION 2 IS THAT RENAME. The bytes did not move — the fields are the same
 * two offsets carrying the same two measurements they carried on the day
 * report_bench() was switched to hal_rx_presence(). What changed is that they
 * were still NAMED for a mechanism that no longer exists, and a reader that
 * labels this pair "level / floor" and draws a min_delta gate against it is
 * reporting a comparison the band never made. A v1 band answers a v2 reader
 * with a genuine floor, so the bump is what lets the reader refuse it.
 *
 * Versioned by offset like ble_status_t: append fields, never move them.
 */
#define BLE_BENCH_TAG     0xB1u
#define BLE_BENCH_VERSION 2

typedef struct {
    uint8_t  tag;           /* BLE_BENCH_TAG, so a score block cannot alias  */
    uint8_t  version;       /* BLE_BENCH_VERSION                             */
    uint16_t signal;        /* CFAR signal: max(E_A, E_B), as a score        */
    uint16_t noise_ref;     /* CFAR reference: the guard boxcar's mean       */
    uint16_t good;          /* frames decoded since the last zero            */
    uint16_t bad;           /* CRC failures since the last zero              */
    uint16_t sent;          /* frames transmitted since the last zero        */
    uint16_t syncs;         /* framer syncs; 0 with a high signal is the tell */
    uint8_t  present;       /* carrier_present()                             */
    uint8_t  link_state;    /* proto/link_sm.h state                         */
    uint8_t  complete;      /* handshakes completed, saturating at 255        */
    uint8_t  aborts;        /* handshakes aborted, saturating at 255          */
    uint8_t  core1_load;    /* per cent                                      */
    uint8_t  on_usb;        /* 1 = VBUS present, so THIS RUN IS NOT VALID    */
} ble_bench_t;

_Static_assert(sizeof(ble_bench_t) == 20, "bench must fit one notify at the 23-byte floor");

/* ---- trigger block, on the same characteristic ------------------------ */

/*
 * WHY THERE IS A SECOND BLOCK. ble_bench_t answers "what is the signal doing".
 * It cannot answer "did the band hear anything", and on 25 Sep 2026 that was
 * the question the whole bench turned on: a phone plot of signal and reference read
 * as a flat line under the gate while 93D1 was in fact tripping its detector
 * about seven times a second and completing exchanges. The counters below were
 * the only thing that told the two apart, and they were reachable only over
 * USB — which design §13 forbids while anyone is wearing a band, and which the
 * same day proved gives WRONG readings because the tether is the return path.
 *
 * So the trigger's own counters leave by radio too, and with them the peak
 * level, which is what makes a plot of a 11 ms event sampled twice a second
 * mean anything at all. See hal_pico_take_peak(), which is a TAKE: the peak
 * is reset by the read, so report_trig() in apps/handoff is its one caller.
 *
 * EVERY COUNTER HERE IS CUMULATIVE SINCE BOOT, deliberately: the reader
 * differences two blocks and gets a rate over a window it chose, which is what
 * the USB bench did by hand. BLE_CTRL_ZERO_STATS does not touch them.
 *
 * READ peers AGAINST beacons. beacons is how many this band transmitted;
 * peers is how many it decoded from the other one. The pair is the one thing
 * that says whether two bands can hear each other, and it is a far better
 * pair than the one it replaces: v1 could only report how many carriers
 * tripped a gate and how many of those were too brief to have been a shout,
 * and the difference between those two was an inference. 379E heard 2 of
 * 93D1's 107; 93D1 heard 20 of 379E's 60. That asymmetry was the fault.
 *
 * AND READ self_echoes, WHICH v1 COULD NOT SEE AT ALL. It is beacons that
 * passed a CRC carrying this band's OWN nonce — its amplifier still ringing
 * past HANDOFF_TRIG_SETTLE_US. Under v1 that was a band electing itself
 * sender and there was no counter for it, only a symptom; under v2 it is
 * caught, discarded, and counted.
 *
 * REDEFINED AT STEP 7, NOT EXTENDED. The v1 layout described a mechanism that
 * no longer exists — there is no gate, no quiet-wait cap and no floor — and
 * the block is full at 20 bytes, so there is nowhere to append. Nothing has
 * ever emitted one of these (ble_trig_t is a finished shape waiting for a
 * caller, brief §1), so no reader can be holding the old layout. The version
 * byte goes up regardless: that is what it is for.
 */
#define BLE_TRIG_TAG     0xB2u
#define BLE_TRIG_VERSION 2

typedef struct {
    uint8_t  tag;            /* BLE_TRIG_TAG                                 */
    uint8_t  version;        /* BLE_TRIG_VERSION                             */
    uint16_t nonce;          /* who this band is, this arming                */
    uint16_t beacons;        /* beacon frames transmitted                    */
    uint16_t peers;          /* beacons decoded carrying somebody else       */
    uint16_t self_echoes;    /* ...carrying OUR nonce: the settle is short   */
    uint16_t sends;          /* elected sender                               */
    uint16_t receives;       /* elected receiver                             */
    uint16_t peak_level;     /* highest signal since the last block          */
    uint16_t peak_noise;     /* the CFAR reference as it stood at that peak  */
    uint8_t  beacons_bad_crc;/* framed as a beacon, failed the CRC, at 255   */
    uint8_t  trig_state;     /* proto/beacon.h trig_state_t                  */
} ble_trig_t;

/*
 * WHAT DID NOT FIT, AND WHY IT IS NOT MISSED. redraws — nonces abandoned
 * after an echo — is one per self_echo by construction (beacon.c redraws at
 * the next beacon after any echo), so it is the same number twice and the
 * block has room for one of them. The USB console prints both.
 */

_Static_assert(sizeof(ble_trig_t) == 20, "trig must fit one notify at the 23-byte floor");

/* ---- bank block, on the same characteristic --------------------------- */

/*
 * THE SPECTRUM: all five Goertzel bins, so a reader can see WHERE the energy
 * is and not merely how much of it there is.
 *
 * WHY A THIRD BLOCK. ble_bench_t carries signal and noise_ref, which are the
 * two numbers the detector decided on — but they are both DERIVED, one a max
 * of two bins and the other a median of three put through a boxcar. When the
 * pair moves together there is no way back from it to the cause: a band
 * arriving raises the tones, and a hand on the bench or a charger plugged in
 * raises everything, and signal-against-reference reads nearly the same in
 * both cases because the reference is designed to move with the room. The
 * five bins separate them at a glance. Nothing in the link reads this; it is
 * an instrument, and it is the one the demo is built on.
 *
 * EVERY BIN IS FROM THE SAME WINDOW. hal_pico_take_bin_peak() snapshots the
 * whole bank at the window where the tones were loudest since the last take,
 * which is the only way five numbers drawn side by side mean anything — five
 * independent maxima would be five different instants wearing the shape of a
 * spectrum. It is also why this is a peak and not an instantaneous read: the
 * bank scores 20 000 windows a second, these blocks go out twice a second,
 * and a beacon is on air for eleven milliseconds. An instantaneous spectrum
 * would draw the empty room 9 999 times out of 10 000 — the 25 Sep 2026
 * failure, in five bins instead of one.
 *
 * READ THE TONES AGAINST THE GUARDS, NOT AGAINST A THRESHOLD. There is no
 * threshold in this block on purpose: k belongs to ble_bench_t, which carries
 * the decision. This block carries the picture the decision was taken from.
 *
 * The frequencies are not sent. They are structural — config.h fixes the bin
 * set and static-asserts the harmonic clearances — so a reader that did not
 * already know them could not interpret the magnitudes either.
 *
 * Twelve bytes, well inside one notify at the 23-byte ATT floor. Versioned by
 * offset like the others: append, never move.
 */
#define BLE_BANK_TAG     0xB3u
#define BLE_BANK_VERSION 1

typedef struct {
    uint8_t  tag;            /* BLE_BANK_TAG                                 */
    uint8_t  version;        /* BLE_BANK_VERSION                             */
    uint16_t bin[GZB_BINS];  /* gz_bank.h order: A, B, g_lo, g_mid, g_hi     */
} ble_bank_t;

_Static_assert(sizeof(ble_bank_t) == 12, "bank must fit one notify at the 23-byte floor");

/* ---- handlers --------------------------------------------------------- */

/* A complete provisioning card. The text is NOT null-terminated and the
 * buffer is reused as soon as this returns. */
typedef void (*ble_vcard_written_fn)(const char *text, size_t len, void *ctx);

/* One control write. arg points at the bytes after the opcode. */
typedef void (*ble_control_fn)(uint8_t op, const uint8_t *arg, size_t len, void *ctx);

/* ---- lifecycle -------------------------------------------------------- */

/*
 * cyw43_arch_init() must already have succeeded: BTstack runs inside the
 * cyw43 async context, and on this board the Bluetooth controller and the
 * wireless chip are the same die on the same bus.
 *
 * Starts advertising and returns; nothing here blocks.
 */
void ble_init(void);

void ble_set_vcard_handler(ble_vcard_written_fn fn, void *ctx);
void ble_set_control_handler(ble_control_fn fn, void *ctx);

/* ---- notifications ---------------------------------------------------- */

/*
 * Push a received contact to the phone, chunked. The text is copied, so the
 * caller's buffer is free on return; the chunks are then paced out as the
 * controller allows.
 *
 * Refuses — returning false — when there is no subscriber, when the link is
 * not encrypted, when a previous card is still going out, or when the text is
 * longer than BLE_VCARD_MAX.
 */
bool ble_notify_rx_vcard(const char *text, size_t len);
bool ble_rx_vcard_busy(void);

bool ble_notify_status(const ble_status_t *st);

/* Decimated scores, design §13. Dropped rather than queued when the
 * controller is busy: a gap in a score stream is a gap in a plot, and a
 * backlog is a stall in the receive path. */
bool ble_notify_telemetry(const void *scores, size_t len);

/* ---- state ------------------------------------------------------------ */

bool     ble_connected(void);

/* The advertised name, "Handoff band 7A3C" (review item 3). Valid after
 * ble_init(). */
const char *ble_local_name(void);
bool     ble_encrypted(void);
bool     ble_telemetry_subscribed(void);
bool     ble_rx_vcard_subscribed(void);

/* ---- the bond: one band, one phone ------------------------------------ */

/*
 * A band has no owner or exactly one (docs/band-ownership-brief.md §1).
 * Both states persist in flash; advertising is open in both, so the owner
 * can find the band and a bystander can use the bench-only characteristics.
 * What is gated is PAIRING: with no bond stored any phone may pair; with
 * one stored only the phone whose identity resolves to it may — that is
 * the owner re-pairing after a Forget in Bluetooth settings — and anyone
 * else is declined (SM_EVENT_JUST_WORKS_REQUEST in ble.c). The way out is
 * ble_forget_bonds(), from the button or from the owner's app.
 */

/* Whether the band has an owner: a phone's keys are stored. Decides "no
 * owner" (advertising, blue) from "owned, phone away", which look the same
 * on the air and must not on the LED. Never more than one bond exists under
 * the rule above; the function counts anyway rather than trusting it. */
bool     ble_has_bond(void);

/*
 * Erase every stored bond and drop the connection if there is one. The
 * band has no owner afterwards and advertises as a fresh device; a phone
 * that still holds its half of the keys finds them refused on reconnect,
 * which is that phone's cue to forget the band. BTstack context only, like
 * everything else here. The rest of a reset — the card, a held card, the
 * preferences — is the app's (handoff.c reset_for_new_wearer).
 */
void     ble_forget_bonds(void);

/* What the phone actually negotiated. BLE_MIN_ATT_MTU until it asks for more,
 * and the number every chunked transfer is sized from. */
uint16_t ble_att_mtu(void);

/* Chunks rejected by the reassembler since boot, for ble_status_t. */
uint16_t ble_chunk_errors(void);

#endif /* HANDOFF_BLE_H */
