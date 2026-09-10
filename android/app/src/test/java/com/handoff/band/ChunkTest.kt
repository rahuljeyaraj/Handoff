package com.handoff.band

import com.handoff.band.ble.Assembler
import com.handoff.band.ble.Chunk
import com.handoff.band.ble.ChunkResult
import com.handoff.band.ble.Gatt
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The same cases `firmware/test/host/test_chunk.c` runs, on the other
 * implementation. Development plan M2's fourth exit criterion is that chunked
 * reassembly works at the 23-byte ATT MTU floor "not just at whatever MTU your
 * phone happens to negotiate", and this is the half of that which does not
 * need a band on the desk.
 */
class ChunkTest {

    /** ATT_MTU 23 minus the 3-byte notification header: 18 payload bytes. */
    private val floor = Gatt.MIN_ATT_MTU - Gatt.ATT_NOTIFY_OVERHEAD

    private fun message(n: Int) = ByteArray(n) { (it * 7 + 3).toByte() }

    private fun roundTrip(data: ByteArray, chunkBytes: Int): ByteArray {
        val asm = Assembler()
        var out: ByteArray? = null
        for (c in Chunk.split(data, chunkBytes)) {
            assertTrue("chunk longer than the MTU allows", c.size <= chunkBytes)
            when (val r = asm.push(c)) {
                is ChunkResult.More -> Unit
                is ChunkResult.Complete -> out = r.data
                is ChunkResult.Error -> throw AssertionError("rejected: ${r.why}")
            }
        }
        return out ?: throw AssertionError("never completed")
    }

    @Test
    fun `round trips at every capacity from the floor up`() {
        val lengths = listOf(0, 1, 17, 18, 19, 36, 37, 176, 300)
        for (cap in floor..244) {
            for (len in lengths) {
                val msg = message(len)
                assertArrayEquals("cap $cap len $len", msg, roundTrip(msg, cap))
            }
        }
    }

    @Test
    fun `chunk count at the ATT floor`() {
        assertEquals(10, Chunk.split(message(176), floor).size)  // ceil(176/18)
        assertEquals(1, Chunk.split(message(18), floor).size)
        assertEquals(1, Chunk.split(ByteArray(0), floor).size)
    }

    @Test
    fun `a skipped chunk is refused, not spliced`() {
        val chunks = Chunk.split(message(54), floor)   // three full chunks
        val asm = Assembler()
        assertTrue(asm.push(chunks[0]) is ChunkResult.More)
        assertTrue(asm.push(chunks[2]) is ChunkResult.Error)

        // The reset is real: the assembler does not resume mid-message.
        assertTrue(asm.push(chunks[1]) is ChunkResult.Error)

        // Recovery is always "start again at seq 0" and nothing more.
        assertTrue(asm.push(chunks[0]) is ChunkResult.More)
        assertTrue(asm.push(chunks[1]) is ChunkResult.More)
        assertTrue(asm.push(chunks[2]) is ChunkResult.Complete)
    }

    @Test
    fun `a restart discards the abandoned message`() {
        val abandoned = Chunk.split(message(54), floor)
        val asm = Assembler()
        assertTrue(asm.push(abandoned[0]) is ChunkResult.More)

        val fresh = Chunk.split("hi".toByteArray(), floor)
        val r = asm.push(fresh[0])
        assertTrue(r is ChunkResult.Complete)
        assertArrayEquals("hi".toByteArray(), (r as ChunkResult.Complete).data)
    }

    @Test
    fun `a ragged middle chunk is refused`() {
        val chunks = Chunk.split(message(54), floor)
        val asm = Assembler()
        assertTrue(asm.push(chunks[0]) is ChunkResult.More)
        assertTrue(asm.push(chunks[1].copyOfRange(0, chunks[1].size - 3))
            is ChunkResult.Error)
    }

    @Test
    fun `malformed headers are refused`() {
        val asm = Assembler()
        assertTrue(asm.push(ByteArray(0)) is ChunkResult.Error)
        assertTrue(asm.push(ByteArray(1)) is ChunkResult.Error)
        assertTrue(asm.push(byteArrayOf(0, 0, 1, 2)) is ChunkResult.Error)   // total 0
        assertTrue(asm.push(byteArrayOf(3, 3, 1, 2)) is ChunkResult.Error)   // seq >= total
    }

    @Test
    fun `an oversized message is refused on the first chunk`() {
        // 255 x 18 = 4590 bytes, well past RX_MAX.
        val c = ByteArray(floor).also { it[0] = 0; it[1] = 255.toByte() }
        val asm = Assembler()
        assertTrue(asm.push(c) is ChunkResult.Error)
        assertEquals(0, asm.received)
    }
}
