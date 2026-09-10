package com.handoff.band.ble

/**
 * The phone half of the chunked transport, architecture §11.2.
 *
 * A line-for-line counterpart of `firmware/lib/link/chunk.c`, and it is a
 * separate file with no Android import for the same reason that one is a
 * separate file with no SDK include: the framing has to be testable without
 * the radio it frames. See `ChunkTest.kt`, which runs the same cases the C
 * suite does — including the ATT floor, which development plan M2 calls out as
 * the criterion that quietly passes on the developer's own handset.
 *
 *     byte 0   seq     0-based index of this chunk
 *     byte 1   total   number of chunks, 1..255
 *     byte 2+  payload
 *
 * Every chunk but the last carries a full payload, which is what lets the
 * receiver derive the capacity from chunk 0 and never be told the MTU.
 */
object Chunk {
    const val HDR_BYTES = 2
    const val MAX_CHUNKS = 255

    /** Matches CHUNK_RX_MAX. A phone claiming more is refused, not allocated. */
    const val RX_MAX = 1024

    /**
     * Split [data] into chunks of at most [chunkBytes] each, header included.
     * For a write that is `mtu - 3`, the same as for a notification.
     */
    fun split(data: ByteArray, chunkBytes: Int): List<ByteArray> {
        require(chunkBytes > HDR_BYTES) { "a chunk with no room for a payload" }
        val cap = minOf(chunkBytes - HDR_BYTES, 255)

        // An empty message is one empty chunk, not zero chunks: "the wearer
        // cleared their card" has to be distinguishable from "nothing was sent".
        val total = maxOf(1, (data.size + cap - 1) / cap)
        require(total <= MAX_CHUNKS) { "message needs $total chunks, max $MAX_CHUNKS" }

        return (0 until total).map { seq ->
            val from = seq * cap
            val to = minOf(from + cap, data.size)
            val payload = if (from < to) data.copyOfRange(from, to) else ByteArray(0)
            ByteArray(HDR_BYTES + payload.size).also {
                it[0] = seq.toByte()
                it[1] = total.toByte()
                payload.copyInto(it, HDR_BYTES)
            }
        }
    }
}

/** What [Assembler.push] made of a chunk. */
sealed interface ChunkResult {
    /** Accepted; more chunks expected. */
    data object More : ChunkResult

    /** Accepted; [data] is the whole message. */
    data class Complete(val data: ByteArray) : ChunkResult {
        override fun equals(other: Any?) =
            other is Complete && data.contentEquals(other.data)

        override fun hashCode() = data.contentHashCode()
    }

    /**
     * Rejected. The assembler has already reset itself, so the recovery is
     * always for the sender to start again at seq 0 — never to resend the
     * chunk that was refused.
     */
    data class Error(val why: String) : ChunkResult
}

/**
 * Reassembles one chunked characteristic. One instance per characteristic;
 * `rx_vcard` and any future chunked notify do not share state.
 */
class Assembler {
    private var buf = ByteArray(0)
    private var cap = 0
    private var total = 0
    private var next = 0
    private var started = false

    /** 0 until the first chunk of a message arrives. For a progress readout. */
    val received get() = if (started) next else 0
    val expected get() = total

    fun reset() {
        buf = ByteArray(0)
        cap = 0
        total = 0
        next = 0
        started = false
    }

    fun push(chunk: ByteArray): ChunkResult {
        if (chunk.size < Chunk.HDR_BYTES) return fail("runt chunk, ${chunk.size} bytes")

        val seq = chunk[0].toInt() and 0xFF
        val tot = chunk[1].toInt() and 0xFF
        val payload = chunk.copyOfRange(Chunk.HDR_BYTES, chunk.size)

        if (tot == 0) return fail("total of 0 is not a message")
        if (seq >= tot) return fail("seq $seq past its own total $tot")

        if (seq == 0) {
            // seq 0 is unconditionally a fresh message. A band whose previous
            // transfer was interrupted recovers by starting again and needs no
            // way of saying so beyond this.
            reset()
            cap = payload.size
            total = tot
            started = true
        } else if (!started || seq != next || tot != total) {
            return fail("out of order: got seq $seq/$tot, expected $next/$total")
        }

        val last = seq + 1 == tot

        // Only the last chunk may be short. A ragged middle chunk shifts every
        // byte after it and still yields a plausible-looking vCard — a wrong
        // phone number rather than a rejection.
        if (!last && payload.size != cap) return fail("ragged middle chunk")
        if (last && payload.size > cap) return fail("last chunk overruns capacity")

        // Refused on the first chunk rather than after filling a kilobyte.
        if (total.toLong() * cap > Chunk.RX_MAX) return fail("message too large")
        if (buf.size + payload.size > Chunk.RX_MAX) return fail("message too large")

        buf += payload
        next = seq + 1

        if (!last) return ChunkResult.More

        val whole = buf
        reset()
        return ChunkResult.Complete(whole)
    }

    private fun fail(why: String): ChunkResult {
        reset()
        return ChunkResult.Error(why)
    }
}
