package com.handoff.band.ble

/**
 * The body-link readings kept over time rather than only as they arrive.
 *
 * A single [BandBench] answers "is it hearing anything right now". It cannot
 * answer the question the carrier floor redesign of 25 Sep 2026 actually
 * raised, which is whether the floor MOVES — the old one could only ever walk
 * downward, so it slid onto the quietest thing it had heard and stayed there,
 * and a lone sample of `floor 6` looks exactly like a correct floor in a quiet
 * room. Two readings a minute apart tell those apart; one reading never can.
 *
 * Kept in the service rather than the screen so leaving Advanced and coming
 * back does not throw the trace away — on a worn run the phone is in a pocket
 * and the screen has certainly been off.
 */
data class BenchSample(
    /** `SystemClock.elapsedRealtime()`, so it survives a wall-clock change. */
    val atMs: Long,
    val level: Int,
    val floor: Int,
    val present: Boolean,
) {
    /**
     * What `level` has to clear for the band to call it a carrier, which is
     * both of carrier.c's gates and not just the ratio. Drawn on the chart
     * because level against floor alone does not say whether the band would
     * have heard it: at a floor of 77 the ratio wants 231 and min_delta only
     * 101, and at a floor of 2 it is the other way round.
     */
    val gate: Int get() = maxOf(floor * BandBench.RATIO_NUM / 8, floor + BandBench.MIN_DELTA)
}

/**
 * The trace, newest last, capped at [MAX] samples. At the band's two a second
 * that is twenty minutes, which is longer than the longest window the chart
 * offers so the window is always a view of this rather than a limit on it.
 */
class BenchTrace private constructor(val samples: List<BenchSample>) {

    fun plus(s: BenchSample): BenchTrace =
        BenchTrace(if (samples.size < MAX) samples + s
                   else samples.subList(samples.size - MAX + 1, samples.size) + s)

    /** The tail of the trace covering [windowMs], newest last. */
    fun window(windowMs: Long): List<BenchSample> {
        val last = samples.lastOrNull() ?: return emptyList()
        val from = last.atMs - windowMs
        val i = samples.indexOfFirst { it.atMs >= from }
        return if (i <= 0) samples else samples.subList(i, samples.size)
    }

    companion object {
        const val MAX = 2400
        val EMPTY = BenchTrace(emptyList())
    }
}
