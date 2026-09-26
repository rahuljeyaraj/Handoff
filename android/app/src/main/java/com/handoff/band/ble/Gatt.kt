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
 * The bench block, on the `telemetry` characteristic - see `ble_bench_t` in
 * `firmware/lib/hal_pico/ble.h`. Little-endian, 20 bytes, tagged so it cannot
 * be mistaken for the 16-byte score block that shares the characteristic, or
 * for [BandTrig] which also rides it.
 *
 * This exists because design 13 forbids tethering a band to a mains-powered
 * PC while anyone touches an electrode, and because 24 Sep 2026 showed a
 * tethered reading is not merely unsafe but WRONG: both bands then share the
 * PC ground, and that wire is the return path under test.
 *
 * READ [signal] AGAINST [noiseRef], AND KNOW WHAT THAT PAIR IS. Link v2 step 5
 * deleted the carrier detector and with it the remembered floor: this design
 * has no floor, no gate, no ratio and no min_delta. The pair is CFAR - signal
 * is the louder of the two tone bins, noiseRef is the mean of the guard bins,
 * and the band called the channel busy when signal cleared [threshold]. Both
 * sides come from the same windows through the same body, so both high is a
 * loud room and not, as it was under v1, a detector that has gone deaf.
 *
 * A high signal with [syncs] stuck at zero still means what it always did: the
 * receiver is swamped, not starved.
 */
data class BandBench(
    val version: Int,
    /** CFAR signal: the louder tone bin, as a score. */
    val signal: Int,
    /** CFAR reference: the mean of the guard-bin boxcar, the same scale. */
    val noiseRef: Int,
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
    /**
     * What [signal] had to clear for the band to call the channel busy:
     * `k * noiseRef`, and nothing else. ONE test, not v1s two - there is no
     * additive floor to be the other one - so this is the whole rule the
     * detector applied, and signal above it means the band heard it.
     */
    val threshold get() = amplitudeThreshold(noiseRef)

    /** The band's own test, without the rounding a drawn [threshold] costs. */
    val heard get() = noiseRef > 0 && powerRatioClears(signal, noiseRef)

    /** One line, for `adb logcat` - what scripts/blelog.py parses. */
    fun line(): String =
        "signal $signal noise $noiseRef thr $threshold present ${if (present) 1 else 0} " +
            "good $good bad $bad sent $sent syncs $syncs " +
            "complete $complete aborts $aborts state $linkState " +
            "load $core1Load usb ${if (onUsb) 1 else 0}"

    companion object {
        const val SIZE = 20
        const val TAG = 0xB1

        /**
         * 2 since link v2. A version 1 band reports a remembered floor from
         * the deleted carrier detector, and labelling that as a CFAR
         * reference would be a lie about which comparison the band made - so
         * an older band is refused rather than mislabelled.
         */
        const val VERSION = 2

        /**
         * `HANDOFF_CFAR_K_NUM` / `HANDOFF_CFAR_K_DEN` from
         * `firmware/lib/hal/config.h`. Not a tuned number: it is solved from a
         * stated false-busy rate of one per minute of continuous listening, and
         * the firmwares own test recomputes the formula and fails if it drifts.
         */
        const val K_NUM = 1676
        const val K_DEN = 100

        /**
         * k IS A POWER RATIO AND THESE SCORES ARE AMPLITUDES. Getting that
         * wrong is why the phone's plot never showed the signal crossing
         * anything.
         *
         * presence.c decides `signal_mag2 > k * noise_mag2`, on mag^2, with
         * no square root anywhere near the hot path. What leaves the band is
         * `gzb_score()` of each — an AMPLITUDE, the square root already
         * taken, because that is the number a human reads. Multiplying an
         * amplitude by k therefore draws a bar sqrt(k) = 4.09 times higher
         * than the one the detector used, and from the v2 merge until
         * 26 Sep 2026 that is exactly what this file did: the band called a
         * channel busy and the phone drew the signal far below a line it had
         * put four times too high. v1 had no such gap, which is why the old
         * level-and-floor plot visibly crossed and this one never did.
         *
         * So: compare in power, and when a LINE has to be drawn on an
         * amplitude axis, draw it at sqrt(k) * reference.
         */
        private val K_AMPLITUDE = Math.sqrt(K_NUM.toDouble() / K_DEN)

        /** Where the detector's bar sits on an amplitude axis. */
        fun amplitudeThreshold(reference: Int) =
            Math.round(reference * K_AMPLITUDE).toInt()

        /** The detector's test itself, squared up so no root is taken. */
        fun powerRatioClears(signal: Int, reference: Int): Boolean =
            signal.toLong() * signal * K_DEN > reference.toLong() * reference * K_NUM

        /** Null for anything that is not a bench block. */
        fun parse(raw: ByteArray): BandBench? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            if ((b.get().toInt() and 0xFF) != TAG) return null
            val version = b.get().toInt() and 0xFF
            if (version < VERSION) return null
            val signal = b.short.toInt() and 0xFFFF
            val noiseRef = b.short.toInt() and 0xFFFF
            val good = b.short.toInt() and 0xFFFF
            val bad = b.short.toInt() and 0xFFFF
            val sent = b.short.toInt() and 0xFFFF
            val syncs = b.short.toInt() and 0xFFFF
            return BandBench(
                version = version, signal = signal, noiseRef = noiseRef,
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

/**
 * The trigger block, on the same characteristic - `ble_trig_t` in
 * `firmware/lib/hal_pico/ble.h`, 20 bytes, tag 0xB2.
 *
 * WHY THERE ARE TWO BLOCKS. [BandBench] answers "what is the signal doing".
 * It cannot answer "did these two bands hear each other", and under link v2
 * that is the question every worn run turns on.
 *
 * READ [peers] AGAINST [beacons]. beacons is how many this band transmitted;
 * peers is how many it decoded from the other one. Two bands that both beacon
 * and neither hears is a channel fault; one hearing and not the other is the
 * asymmetry that WAS the step-7 fault - 379E heard 2 of 93D1s 107 while 93D1
 * heard 20 of 379Es 60.
 *
 * AND READ [selfEchoes], which v1 could not see at all: beacons that passed
 * CRC carrying this bands OWN nonce, i.e. its amplifier still ringing past the
 * settle. Under v1 that was a band electing itself sender with no counter for
 * it; here it is caught, discarded and counted.
 *
 * EVERY COUNTER IS CUMULATIVE SINCE BOOT, deliberately - difference two blocks
 * and you have a rate over a window you chose, which is what the USB bench did
 * by hand. Erasing the stats on the band does not reset them.
 *
 * [peakSignal] is the exception and is not cumulative: it is the loudest window
 * since the previous block, which is the only level reading that means anything
 * at this rate. A beacon is on air for eleven milliseconds and these arrive
 * twice a second, so [BandBench.signal] samples the empty room almost every
 * time - on 25 Sep 2026 it drew a flat line under the threshold while the band
 * was in fact tripping its detector about seven times a second.
 */
data class BandTrig(
    val version: Int,
    /** Who this band is, this arming. Redrawn after every self-echo. */
    val nonce: Int,
    val beacons: Int,
    val peers: Int,
    val selfEchoes: Int,
    val sends: Int,
    val receives: Int,
    /** The loudest window since the previous block, not an instant. */
    val peakSignal: Int,
    /** The CFAR reference as it stood in that same window. */
    val peakNoise: Int,
    val beaconsBadCrc: Int,
    val trigState: Int,
) {
    /** The threshold that peak was judged against, on its own window. */
    val peakThreshold get() = BandBench.amplitudeThreshold(peakNoise)

    /** Did the loudest window of the interval clear the detector at all. */
    val peakHeard get() =
        peakNoise > 0 && BandBench.powerRatioClears(peakSignal, peakNoise)

    /** `firmware/lib/proto/beacon.h`s `trig_state_t`. */
    val stateName get() = when (trigState) {
        0 -> "off"
        1 -> "beacon"
        2 -> "settle"
        3 -> "listen"
        4 -> "send"
        5 -> "receive"
        else -> "?$trigState"
    }

    fun line(): String =
        "nonce ${"%04x".format(nonce)} beacons $beacons peers $peers " +
            "echoes $selfEchoes sends $sends receives $receives " +
            "peak $peakSignal/$peakThreshold badcrc $beaconsBadCrc trig $stateName"

    companion object {
        const val SIZE = 20
        const val TAG = 0xB2

        /**
         * 2: the v1 layout described a gate, a quiet-wait cap and a floor,
         * none of which exist any more. Nothing ever emitted a v1 block.
         */
        const val VERSION = 2

        fun parse(raw: ByteArray): BandTrig? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            if ((b.get().toInt() and 0xFF) != TAG) return null
            val version = b.get().toInt() and 0xFF
            if (version < VERSION) return null
            return BandTrig(
                version = version,
                nonce = b.short.toInt() and 0xFFFF,
                beacons = b.short.toInt() and 0xFFFF,
                peers = b.short.toInt() and 0xFFFF,
                selfEchoes = b.short.toInt() and 0xFFFF,
                sends = b.short.toInt() and 0xFFFF,
                receives = b.short.toInt() and 0xFFFF,
                peakSignal = b.short.toInt() and 0xFFFF,
                peakNoise = b.short.toInt() and 0xFFFF,
                beaconsBadCrc = b.get().toInt() and 0xFF,
                trigState = b.get().toInt() and 0xFF,
            )
        }
    }
}

/**
 * The spectrum: all five Goertzel bins from ONE window - `ble_bank_t` in
 * `firmware/lib/hal_pico/ble.h`, 12 bytes, tag 0xB3.
 *
 * WHY A THIRD BLOCK. [BandBench] carries the two numbers the detector decided
 * on, and both are derived - signal is a max of two bins, noiseRef a median of
 * three through a boxcar. When the pair rises together it cannot say why, and
 * by design it often does: the reference is MEANT to track the room. So a band
 * arriving and a charger being plugged in look nearly alike in that pair. Five
 * bins tell them apart at a glance - two tones out of three flat guards is a
 * band being heard; all five up together is the room.
 *
 * ALL FIVE COME FROM THE SAME WINDOW, which is the only reason they can be
 * drawn side by side. Five independently-maximised bins would be five
 * different instants wearing the shape of a spectrum.
 *
 * IT IS A PEAK, NOT AN INSTANT, for the reason written all over this file: the
 * band scores 20 000 windows a second, these arrive twice a second, and a
 * beacon is on air for eleven milliseconds. An instantaneous spectrum would
 * draw the empty room 9 999 times in 10 000.
 *
 * NO THRESHOLD HERE ON PURPOSE. k belongs to [BandBench], which carries the
 * decision; this block carries the picture the decision was taken from.
 */
data class BandBank(
    val version: Int,
    /** Tone A, 180 kHz. */
    val toneA: Int,
    /** Tone B, 200 kHz. */
    val toneB: Int,
    /** Guard, 140 kHz. */
    val guardLo: Int,
    /** Guard, 160 kHz. */
    val guardMid: Int,
    /** Guard, 220 kHz. */
    val guardHi: Int,
) {
    val tones get() = listOf(toneA, toneB)
    val guards get() = listOf(guardLo, guardMid, guardHi)

    /** The louder tone - the same quantity [BandBench.signal] carries. */
    val signal get() = maxOf(toneA, toneB)

    /**
     * The room, as the detector measures it: the MEDIAN of the three, which is
     * what `gzb_noise()` computes. Not the mean - one interferer in one guard
     * must not be able to move it, and on this board's 1/f slope the median
     * selects the middle bin every time while the outer two do the outlier
     * protection CFAR wants them for.
     */
    val room get() = guards.sorted()[1]

    /**
     * Did any window in this interval close at all. Five zeros mean the bank
     * is off, NOT a silent room - an idle detector still reports the room.
     */
    val measured get() = tones.any { it > 0 } || guards.any { it > 0 }

    fun line(): String =
        "bank A $toneA B $toneB guards $guardLo/$guardMid/$guardHi"

    companion object {
        const val SIZE = 12
        const val TAG = 0xB3
        const val VERSION = 1

        /** Bin centres, from `config.h`. Structural, so they are not sent. */
        val TONE_HZ = listOf(180_000, 200_000)
        val GUARD_HZ = listOf(140_000, 160_000, 220_000)

        fun parse(raw: ByteArray): BandBank? {
            if (raw.size < SIZE) return null
            val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
            if ((b.get().toInt() and 0xFF) != TAG) return null
            val version = b.get().toInt() and 0xFF
            if (version < VERSION) return null
            return BandBank(
                version = version,
                toneA = b.short.toInt() and 0xFFFF,
                toneB = b.short.toInt() and 0xFFFF,
                guardLo = b.short.toInt() and 0xFFFF,
                guardMid = b.short.toInt() and 0xFFFF,
                guardHi = b.short.toInt() and 0xFFFF,
            )
        }
    }
}
