package com.handoff.band

import com.handoff.band.ble.BandStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The status parser has to survive a band that is newer than the app
 * (design decisions §9): a higher version with extra bytes is read by
 * offset, not rejected.
 */
class BandStatusTest {

    private fun v1(version: Int = 1, flags: Int = 0x03, tail: ByteArray = ByteArray(0)): ByteArray {
        val b = ByteBuffer.allocate(16 + tail.size).order(ByteOrder.LITTLE_ENDIAN)
        b.put(version.toByte()).put(flags.toByte()).put(2).put(7)   // version flags link record
        b.putShort(0).putShort(128)                                   // last_score own_blob_len
        b.putInt(0)                                                   // frag_bitmap
        b.putShort(3).putShort(0)                                     // chunk_errors frame_errors
        b.put(tail)
        return b.array()
    }

    @Test
    fun parsesVersionOne() {
        val s = BandStatus.parse(v1())
        assertNotNull(s)
        assertTrue(s!!.encrypted)
        assertTrue(s.provisioned)
        assertEquals(7, s.recordId)
        assertEquals(128, s.ownBlobLen)
        assertEquals(3, s.chunkErrors)
    }

    @Test
    fun parsesVersionTwoSupplyAndFirmware() {
        // 0xBE = 190 * 20 mV = 3.80 V VSYS; firmware 0.2.0
        val s = BandStatus.parse(v1(version = 2, tail = byteArrayOf(0xBE.toByte(), 0, 2, 0)))
        assertNotNull(s)
        assertEquals(3800, s!!.vsysMv)
        assertEquals("0.2.0", s.firmware)
        assertEquals(false, s.usbPower)
    }

    @Test
    fun versionTwoWithoutTheBytesReadsAsVersionOne() {
        val s = BandStatus.parse(v1(version = 2))
        assertNotNull(s)
        assertNull(s!!.vsysMv)
        assertNull(s.firmware)
    }

    @Test
    fun acceptsANewerVersionAndIgnoresTheTail() {
        val s = BandStatus.parse(v1(version = 3, tail = byteArrayOf(0xBE.toByte(), 0, 2, 0, 9, 9, 9)))
        assertNotNull(s)
        assertEquals(3, s!!.version)
        assertEquals(128, s.ownBlobLen)
        assertEquals(3800, s.vsysMv)
    }

    @Test
    fun usbPowerFromTheFlagOrFromVsys() {
        val flagged = BandStatus.parse(v1(version = 2, flags = 0x13, tail = byteArrayOf(0xBE.toByte(), 0, 2, 0)))
        assertEquals(true, flagged!!.usbPower)
        // 0xE6 = 230 * 20 = 4.60 V: above any cell, so USB even without the flag
        val high = BandStatus.parse(v1(version = 2, tail = byteArrayOf(0xE6.toByte(), 0, 2, 0)))
        assertEquals(true, high!!.usbPower)
    }

    @Test
    fun hapticOnFromTheFlag() {
        // 0x23 = ENCRYPTED | PROVISIONED | HAPTIC_ON
        val on = BandStatus.parse(v1(flags = 0x23))
        assertEquals(true, on!!.hapticOn)
        val off = BandStatus.parse(v1(flags = 0x03))
        assertEquals(false, off!!.hapticOn)
    }

    @Test
    fun rejectsVersionZeroAndShortPayloads() {
        assertNull(BandStatus.parse(v1(version = 0)))
        assertNull(BandStatus.parse(ByteArray(15)))
    }
}
