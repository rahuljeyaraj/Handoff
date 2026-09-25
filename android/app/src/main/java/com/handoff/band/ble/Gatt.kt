package com.handoff.band.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/**
 * The BLE contract, architecture §11.2.
 *
 * THE AUTHORITATIVE COPY IS `firmware/lib/hal_pico/ble_service.gatt`. Changing
 * a UUID on one side and not the other does not fail loudly: the app connects,
 * discovers nothing it recognises, and reports no error at all. Change both in
 * one commit or neither.
 */
object Gatt {

    private const val BASE = "48414e44-%04x-4f46-9b2c-1e0a7d3f5c81"

    /** 48414e44 is "HAND" in ASCII, so an nRF Connect dump is readable. */
    val SERVICE: UUID = UUID.fromString(BASE.format(0x0001))

    /** write, chunked — provisions the wristband (§9). Needs a bonded link. */
    val MY_VCARD: UUID = UUID.fromString(BASE.format(0x0002))

    /** notify, chunked — a received contact as vCard text. Needs a bonded link. */
    val RX_VCARD: UUID = UUID.fromString(BASE.format(0x0003))

    /** read + notify — link state, fragment bitmap, error counters. */
    val STATUS: UUID = UUID.fromString(BASE.format(0x0004))

    /** notify — decimated score stream for the §14.1 body tests. */
    val TELEMETRY: UUID = UUID.fromString(BASE.format(0x0005))

    /** write — carrier select, raw capture trigger, force role. */
    val CONTROL: UUID = UUID.fromString(BASE.format(0x0006))

    /** The SIG's Client Characteristic Configuration descriptor. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** ATT's guaranteed floor. Everything must work here — see Chunk.kt. */
    const val MIN_ATT_MTU = 23

    /** An ATT notification spends 3 bytes on opcode and handle. */
    const val ATT_NOTIFY_OVERHEAD = 3

    // ---- control opcodes, mirroring ble_service.gatt's ble.h -------------

    const val CTRL_CARRIER = 0x01
    const val CTRL_RAW_TRIGGER = 0x02
    const val CTRL_FORCE_ROLE = 0x03
    const val CTRL_TLM_DECIMATE = 0x04
    const val CTRL_FAKE_RX = 0x05
    const val CTRL_FORGET = 0x06
    const val CTRL_HAPTIC = 0x07
    const val CTRL_IDENTIFY = 0x08
    const val CTRL_RESET = 0x09

    /**
     * Ask the band to notify its hardcoded card in [seconds]. M2 only, and the
     * delay is the point: it is how you get a notification to arrive while the
     * screen is off and the app is backgrounded, which is the exit criterion a
     * web client could never have met.
     */
    fun fakeRx(seconds: Int): ByteArray =
        byteArrayOf(CTRL_FAKE_RX.toByte(), seconds.coerceIn(0, 255).toByte())

    /** Erase the card on the band, and only the card (Advanced). */
    fun forget(): ByteArray = byteArrayOf(CTRL_FORGET.toByte())

    /**
     * Reset the band for a new wearer (band-ownership brief §3): the bond,
     * the card, a held card, the preferences. The band drops the link once
     * it has done it, so there is no status to wait for — the write's
     * acknowledgement, or the disconnect, is the answer. Only the owner's
     * encrypted link is obeyed; the band ignores it otherwise.
     */
    fun reset(): ByteArray = byteArrayOf(CTRL_RESET.toByte())

    /**
     * Find my band: the band flashes white three times and taps three times
     * (lib/ui's identify row). Not gated on the encrypted link — it does
     * nothing a bystander could not do by looking at the band — and the
     * motor half waits if a handshake is in progress.
     */
    fun identify(): ByteArray = byteArrayOf(CTRL_IDENTIFY.toByte())

    /** The Settings switch (review item 11). The band persists this itself. */
    fun haptic(on: Boolean): ByteArray =
        byteArrayOf(CTRL_HAPTIC.toByte(), if (on) 1 else 0)

    fun telemetryDecimate(n: Int): ByteArray {
        val b = ByteBuffer.allocate(3).order(ByteOrder.LITTLE_ENDIAN)
        b.put(CTRL_TLM_DECIMATE.toByte())
        b.putShort(n.coerceIn(0, 0xFFFF).toShort())
        return b.array()
    }
}

/**
 * The `status` payload. Little-endian, versioned — see `ble_status_t` in
 * `firmware/lib/hal_pico/ble.h`. Version 1 is 16 bytes; version 2 appends
 * the supply and the firmware version for 20, which is the whole of one
 * notification at the 23-byte floor.
 *
 * FORWARD-COMPATIBLE BY OFFSET (design decisions §9). Any version at or
 * above 1 is accepted: the fields this app knows are read from their fixed
 * offsets and trailing bytes are ignored. A newer band therefore degrades to
 * "battery unknown" rather than blanking the status line, which is what
 * rejecting the version did. Only a payload too short for the version-1
 * fields is refused.
 */
data class BandStatus(
    val version: Int,
    val flags: Int,
    val linkState: Int,
    val recordId: Int,
    val lastScore: Int,
    val ownBlobLen: Int,
    val fragBitmap: Long,
    val chunkErrors: Int,
    val frameErrors: Int,
    /** VSYS after D1, millivolts. Null from a version-1 band or before it is read. */
    val vsysMv: Int? = null,
    /** "0.2.0". Null from a version-1 band. */
    val firmware: String? = null,
) {
    val encrypted get() = flags and ENCRYPTED != 0
    val provisioned get() = flags and PROVISIONED != 0
    val flashOk get() = flags and FLASH_OK != 0
    val telemetryOn get() = flags and TLM_ON != 0
    /** The band's own vibrate preference (review item 11), persisted there. */
    val hapticOn get() = flags and HAPTIC_ON != 0

    /**
     * VBUS at the Pico. NOT charging: the charger is off-board on J5 and the
     * band cannot see it. Also inferred from VSYS, which sits above any
     * Li-ion voltage when the Pico's Schottky is ORing USB in.
     */
    val usbPower get() = flags and USB_POWER != 0 || (vsysMv ?: 0) > USB_VSYS_MV

    companion object {
        const val SIZE = 16
        const val SIZE_V2 = 20
        const val VERSION = 1

        const val ENCRYPTED = 0x01
        const val PROVISIONED = 0x02
        const val FLASH_OK = 0x04
        const val TLM_ON = 0x08
        const val USB_POWER = 0x10
        const val HAPTIC_ON = 0x20
        // 0x40 was the band's dev mode; retired 15 Sep 2026, never decoded here.

        /** Above this VSYS is not a cell (design decisions §8). */
        const val USB_VSYS_MV = 4300

        fun parse(raw: ByteArray): BandStatus? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            val version = b.get().toInt() and 0xFF
            if (version < VERSION) return null
            val flags = b.get().toInt() and 0xFF
            val linkState = b.get().toInt() and 0xFF
            val recordId = b.get().toInt() and 0xFF
            val lastScore = b.short.toInt() and 0xFFFF
            val ownBlobLen = b.short.toInt() and 0xFFFF
            val fragBitmap = b.int.toLong() and 0xFFFFFFFFL
            val chunkErrors = b.short.toInt() and 0xFFFF
            val frameErrors = b.short.toInt() and 0xFFFF

            var vsysMv: Int? = null
            var firmware: String? = null
            if (version >= 2 && raw.size >= SIZE_V2) {
                // 20 mV steps; 0 means the band has not read it yet.
                vsysMv = (b.get().toInt() and 0xFF).takeIf { it != 0 }?.let { it * 20 }
                val major = b.get().toInt() and 0xFF
                val minor = b.get().toInt() and 0xFF
                val patch = b.get().toInt() and 0xFF
                firmware = "$major.$minor.$patch"
            }

            return BandStatus(
                version = version, flags = flags, linkState = linkState, recordId = recordId,
                lastScore = lastScore, ownBlobLen = ownBlobLen, fragBitmap = fragBitmap,
                chunkErrors = chunkErrors, frameErrors = frameErrors,
                vsysMv = vsysMv, firmware = firmware,
            )
        }
    }
}

/**
 * The bench block, on the `telemetry` characteristic — see `ble_bench_t` in
 * `firmware/lib/hal_pico/ble.h`. Little-endian, 20 bytes, tagged so it cannot
 * be mistaken for the 16-byte score block that shares the characteristic.
 *
 * This exists because design §13 forbids tethering a band to a mains-powered
 * PC while anyone touches an electrode, and because 24 Sep 2026 showed a
 * tethered reading is not merely unsafe but WRONG: both bands then share the
 * PC ground, and that wire is the return path under test.
 *
 * READ [level] AGAINST [noiseFloor]. A high level with [syncs] stuck at zero
 * means the receiver is swamped, not starved.
 */
data class BandBench(
    val version: Int,
    val level: Int,
    val noiseFloor: Int,
    val good: Int,
    val bad: Int,
    val sent: Int,
    val syncs: Int,
    val present: Boolean,
    val linkState: Int,
    val complete: Int,
    val aborts: Int,
    val core1Load: Int,
    /** VBUS at the band. True means the run is not a valid body-coupled test. */
    val onUsb: Boolean,
) {
    /** How far the carrier sits above its own floor. carrier.c gates on 24. */
    val margin get() = level - noiseFloor

    /** One line, for `adb logcat` — what scripts/blelog.py parses. */
    fun line(): String =
        "level $level floor $noiseFloor margin $margin present ${if (present) 1 else 0} " +
            "good $good bad $bad sent $sent syncs $syncs " +
            "complete $complete aborts $aborts state $linkState " +
            "load $core1Load usb ${if (onUsb) 1 else 0}"

    companion object {
        const val SIZE = 20
        const val TAG = 0xB1
        const val VERSION = 1

        /** carrier.c's min_delta: below this the band will not even start. */
        const val MIN_DELTA = 24

        /** carrier.c's ratio_num, in eighths: present at 3x the floor. */
        const val RATIO_NUM = 24

        /** Null for anything that is not a bench block, the score stream included. */
        fun parse(raw: ByteArray): BandBench? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            if ((b.get().toInt() and 0xFF) != TAG) return null
            val version = b.get().toInt() and 0xFF
            if (version < VERSION) return null
            val level = b.short.toInt() and 0xFFFF
            val noiseFloor = b.short.toInt() and 0xFFFF
            val good = b.short.toInt() and 0xFFFF
            val bad = b.short.toInt() and 0xFFFF
            val sent = b.short.toInt() and 0xFFFF
            val syncs = b.short.toInt() and 0xFFFF
            return BandBench(
                version = version, level = level, noiseFloor = noiseFloor,
                good = good, bad = bad, sent = sent, syncs = syncs,
                present = (b.get().toInt() and 0xFF) != 0,
                linkState = b.get().toInt() and 0xFF,
                complete = b.get().toInt() and 0xFF,
                aborts = b.get().toInt() and 0xFF,
                core1Load = b.get().toInt() and 0xFF,
                onUsb = (b.get().toInt() and 0xFF) != 0,
            )
        }
    }
}
