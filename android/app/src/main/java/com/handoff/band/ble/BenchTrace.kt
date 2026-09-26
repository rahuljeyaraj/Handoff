package com.handoff.band.ble

/**
 * The body-link readings kept over time rather than only as they arrive.
 *
 * A single [BandBench] answers "is it hearing anything right now", and under
 * link v2 it usually answers no even when the band is hearing plenty: a beacon
 * is on air for eleven milliseconds and the band sends these twice a second, so
 * an instantaneous sample lands in the empty room nearly every time. That is
 * exactly what happened on 25 Sep 2026, when the phone drew a flat line under
 * the threshold while the band was tripping its detector about seven times a
 * second.
 *
 * So the series that matters here is the PEAK from the band's trigger block —
 * the loudest window of the whole interval — with the instantaneous signal kept
 * beside it. Two lines that diverge mean brief events; two lines together mean
 * a steady room.
 *
 * Kept in the service rather than the screen so leaving Advanced and coming
 * back does not throw the trace away — on a worn run the phone is in a pocket
 * and the screen has certainly been off.
 */
data class BenchSample(
    /** `SystemClock.elapsedRealtime()`, so it survives a wall-clock change. */
    val atMs: Long,
    /** The CFAR signal at the moment the block was built. */
    val signal: Int,
    /** The CFAR reference it was judged against. */
    val noise: Int,
    /**
     * The loudest window since the previous block, from [BandTrig]. Null until
     * a trigger block has arrived — a band on firmware without one still draws
     * the two series above.
     */
    val peak: Int? = null,
    val present: Boolean = false,
) {
    /**
     * What the signal had to clear for the band to call the channel busy:
     * `k * noise`, and that is the whole rule. Drawn as a line because the
     * reference moves with the room, so the bar the signal is read against is
     * a series and not a constant.
     *
     * v1 drew the higher of two gates here. There are not two any more — the
     * ratio test IS the test, because there is no remembered floor for an
     * additive test to protect.
     */
    val threshold: Int get() = (noise.toLong() * BandBench.K_NUM / BandBench.K_DEN).toInt()
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

    /**
     * The peak on the newest sample, filled in when the trigger block lands
     * after the bench block it belongs with. The two arrive as separate
     * notifications a few milliseconds apart, and pairing them by arrival is
     * honest at this rate — the alternative is a second trace at the same
     * timestamps, drawn on the same axis, kept apart for no reason.
     */
    fun withPeak(peak: Int): BenchTrace {
        val last = samples.lastOrNull() ?: return this
        if (last.peak != null) return this
        return BenchTrace(samples.dropLast(1) + last.copy(peak = peak))
    }

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
