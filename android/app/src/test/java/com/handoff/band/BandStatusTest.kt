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
    fun acceptsANewerVersionAndIgnoresTheTail() {
        val s = BandStatus.parse(v1(version = 2, tail = byteArrayOf(0x34, 0x0F, 1, 2, 3)))
        assertNotNull(s)
        assertEquals(2, s!!.version)
        assertEquals(128, s.ownBlobLen)
    }

    @Test
    fun rejectsVersionZeroAndShortPayloads() {
        assertNull(BandStatus.parse(v1(version = 0)))
        assertNull(BandStatus.parse(ByteArray(15)))
    }
}
