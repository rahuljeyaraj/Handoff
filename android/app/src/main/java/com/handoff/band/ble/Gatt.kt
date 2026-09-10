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

    /**
     * Ask the band to notify its hardcoded card in [seconds]. M2 only, and the
     * delay is the point: it is how you get a notification to arrive while the
     * screen is off and the app is backgrounded, which is the exit criterion a
     * web client could never have met.
     */
    fun fakeRx(seconds: Int): ByteArray =
        byteArrayOf(CTRL_FAKE_RX.toByte(), seconds.coerceIn(0, 255).toByte())

    fun forget(): ByteArray = byteArrayOf(CTRL_FORGET.toByte())

    fun telemetryDecimate(n: Int): ByteArray {
        val b = ByteBuffer.allocate(3).order(ByteOrder.LITTLE_ENDIAN)
        b.put(CTRL_TLM_DECIMATE.toByte())
        b.putShort(n.coerceIn(0, 0xFFFF).toShort())
        return b.array()
    }
}

/**
 * The `status` payload. 16 bytes, little-endian, versioned — see `ble_status_t`
 * in `firmware/lib/hal_pico/ble.h`.
 *
 * Parsed by offset rather than by any framework, so a firmware that grows the
 * struct is handled by the version check rather than by a crash.
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
) {
    val encrypted get() = flags and ENCRYPTED != 0
    val provisioned get() = flags and PROVISIONED != 0
    val flashOk get() = flags and FLASH_OK != 0
    val telemetryOn get() = flags and TLM_ON != 0

    companion object {
        const val SIZE = 16
        const val VERSION = 1

        const val ENCRYPTED = 0x01
        const val PROVISIONED = 0x02
        const val FLASH_OK = 0x04
        const val TLM_ON = 0x08

        fun parse(raw: ByteArray): BandStatus? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            val version = b.get().toInt() and 0xFF
            if (version != VERSION) return null
            return BandStatus(
                version = version,
                flags = b.get().toInt() and 0xFF,
                linkState = b.get().toInt() and 0xFF,
                recordId = b.get().toInt() and 0xFF,
                lastScore = b.short.toInt() and 0xFFFF,
                ownBlobLen = b.short.toInt() and 0xFFFF,
                fragBitmap = b.int.toLong() and 0xFFFFFFFFL,
                chunkErrors = b.short.toInt() and 0xFFFF,
                frameErrors = b.short.toInt() and 0xFFFF,
            )
        }
    }
}
