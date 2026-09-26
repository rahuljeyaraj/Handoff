package com.handoff.band.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.handoff.band.ble.BandBench
import com.handoff.band.ble.BenchSample
import com.handoff.band.ble.BenchTrace
import com.handoff.band.ui.theme.MonoStyle

/**
 * Three lines against time: the SIGNAL, the ROOM it is measured against, and
 * the THRESHOLD between them. Signal above threshold is the band hearing
 * something that is not itself.
 *
 * ALL THREE COME FROM THE BANK BLOCK, which is why that block still exists
 * even though its five bins are no longer drawn. It is the only source on the
 * band that is BLANKED DURING OUR OWN TRANSMISSION (hal_pico.c,
 * core1_tx_deaf, placed on the sample clock). The bench block's pair is not:
 * measured 26 Sep 2026 with the peer's link switched off at the console, the
 * ungated signal sat at 960 against a room of 20 and never moved, because
 * what it was hearing was this band's own beacon. A chart drawn from that
 * cannot show anything arriving, because it is already saturated by us.
 *
 *   signal      max(E_A, E_B) at the loudest window of the interval, skipping
 *               every window our own pad was driven. A peak, because a beacon
 *               is on air for eleven milliseconds and these blocks arrive
 *               twice a second — an instantaneous reading lands in the empty
 *               room 9 999 times out of 10 000.
 *   room        the guard bins, averaged over every guard window of the same
 *               interval. A mean, because the room is steady and a peak of it
 *               would just select the noisiest window of ten thousand.
 *   threshold   sqrt(k) x room, and it is a LINE the signal crosses rather
 *               than a number to be inferred.
 *
 * THE THRESHOLD IS sqrt(k), NOT k, AND THAT IS THE WHOLE REASON THIS CHART
 * WENT BACK TO THREE LINES. presence.c decides on mag^2 — `signal^2 > k *
 * noise^2` — and what leaves the band is the amplitude, the root already
 * taken. From the v2 merge until 26 Sep 2026 the app multiplied the amplitude
 * by k anyway, drawing the bar 4.09x too high, so the signal could never
 * reach it: the band would call a channel busy while the phone drew the
 * reading far below its own line. Under v1 no such gap existed, which is
 * exactly why the old level-and-floor plot visibly crossed and this one never
 * did. BandBench.amplitudeThreshold is where that is put right.
 *
 * WHAT THIS IS NOT. It is not v1's level and floor. The floor was a
 * REMEMBERED average of the same bin the signal was in, so the signal could
 * poison it and it lagged behind every change — half the drama of the old
 * plot was the floor crawling after a level that had already jumped. This
 * room is measured live, in the same windows, in bins our transmitter cannot
 * reach. It does not lag and it cannot be poisoned.
 */

/* Categorical slots 1 and 5 of the validated palette, stepped per mode.
 *
 * Slot 5 rather than 2 on purpose: this app reserves green, amber and red for
 * connected / low battery / critical (theme §12), so the orange, aqua and
 * yellow slots are not free to mean "a series". Magenta is the next one that
 * is. Both pairs validate — worst CVD dE 13.0 light / 15.9 dark against a
 * target of 8, normal-vision 27.5 / 26.5 against a floor of 15.
 *
 * Light-mode magenta sits at 2.57:1 on the surface, under the 3:1 bar, so the
 * relief rule applies and every series carries a visible value label. */
private val SignalLight = Color(0xFF2A78D6)
private val SignalDark = Color(0xFF3987E5)
private val ThresholdLight = Color(0xFFD55181)
private val ThresholdDark = Color(0xFFE87BA4)

private enum class Window(val label: String, val ms: Long) {
    SHORT("30 s", 30_000),
    MEDIUM("2 min", 120_000),
    LONG("10 min", 600_000),
}

@Composable
fun BenchChart(trace: BenchTrace, modifier: Modifier = Modifier) {
    var window by remember { mutableStateOf(Window.SHORT) }
    // Off the surface rather than isSystemInDarkTheme(), because the app has
    // its own light/dark override (Prefs.Theme) and the chart has to follow
    // the surface it is actually drawn on.
    val dark = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    val signalColor = if (dark) SignalDark else SignalLight
    val thresholdColor = if (dark) ThresholdDark else ThresholdLight
    val ink = MaterialTheme.colorScheme.onSurfaceVariant
    val grid = MaterialTheme.colorScheme.outlineVariant
    // The room is the baseline the other two are read against, so it is the
    // quietest thing on the canvas — present, never competing.
    val roomColor = ink.copy(alpha = 0.55f)

    val samples = trace.window(window.ms).filter { it.bank != null }
    val last = samples.lastOrNull()

    fun signalOf(s: BenchSample) = s.bank!!.signal
    fun roomOf(s: BenchSample) = s.bank!!.room
    fun thresholdOf(s: BenchSample) = BandBench.amplitudeThreshold(s.bank!!.room)

    Column(modifier.padding(horizontal = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            for (w in Window.entries) {
                FilterChip(
                    selected = w == window,
                    onClick = { window = w },
                    label = { Text(w.label) },
                    modifier = Modifier.padding(end = 8.dp),
                )
            }
        }

        Spacer(Modifier.height(8.dp))

        if (samples.size < 2) {
            Text(
                "Waiting for readings. The band sends two a second while the " +
                    "app is connected; the chart needs a few seconds of them.",
                style = MaterialTheme.typography.bodySmall,
                color = ink,
                modifier = Modifier.padding(vertical = 8.dp),
            )
            return@Column
        }

        val top = niceTop(samples.maxOf { maxOf(signalOf(it), thresholdOf(it)) })

        Box(Modifier.fillMaxWidth().height(170.dp)) {
            Canvas(Modifier.fillMaxWidth().height(170.dp)) {
                val h = size.height
                val w = size.width

                // Recessive chrome: hairline, solid, never dashed — that is
                // what the threshold is, and the two must not read alike.
                for (f in listOf(0f, 0.5f, 1f)) {
                    val y = h - f * h
                    drawLine(grid, Offset(0f, y), Offset(w, y), strokeWidth = 1f)
                }

                val t0 = samples.first().atMs
                val span = (samples.last().atMs - t0).coerceAtLeast(1L).toFloat()
                fun x(s: BenchSample) = (s.atMs - t0) / span * w
                fun y(v: Int) = h - (v.coerceIn(0, top).toFloat() / top) * h

                // The room first and thinnest: it is the baseline, not a
                // result.
                drawPath(
                    drawSeries(samples, ::x) { y(roomOf(it)) }, roomColor,
                    style = Stroke(width = 1.5.dp.toPx(),
                                   cap = StrokeCap.Round, join = StrokeJoin.Round),
                )

                // Dashed, and the only dashed thing here: it is a rule the
                // data is read against, not data and not chrome.
                drawPath(
                    drawSeries(samples, ::x) { y(thresholdOf(it)) }, thresholdColor,
                    style = Stroke(
                        width = 1.5.dp.toPx(),
                        pathEffect = PathEffect.dashPathEffect(
                            floatArrayOf(6.dp.toPx(), 5.dp.toPx())),
                    ),
                )

                // The signal last and thickest, so where it crosses the
                // threshold it is the line on top — the crossing is the whole
                // content of this chart.
                drawPath(
                    drawSeries(samples, ::x) { y(signalOf(it)) }, signalColor,
                    style = Stroke(width = 2.5.dp.toPx(),
                                   cap = StrokeCap.Round, join = StrokeJoin.Round),
                )
            }

            Text("$top", style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.TopStart))
            Text("0", style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.BottomStart))
            Text(window.label, style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.BottomEnd))
        }

        Spacer(Modifier.height(8.dp))

        // Identity never by colour alone: a key, the name, and the value it is
        // at right now. The value is also the relief the light-mode contrast
        // warning asks for.
        Row(Modifier.fillMaxWidth()) {
            Key(signalColor, "signal", last?.let { signalOf(it).toString() } ?: "-")
            Spacer(Modifier.width(14.dp))
            Key(thresholdColor, "needs", last?.let { thresholdOf(it).toString() } ?: "-")
            Spacer(Modifier.width(14.dp))
            Key(roomColor, "room", last?.let { roomOf(it).toString() } ?: "-")
        }
    }
}

@Composable
private fun Key(color: Color, name: String, value: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Spacer(Modifier.width(6.dp))
        Text("$name $value", style = MonoStyle,
             color = MaterialTheme.colorScheme.onSurface)
    }
}

/** A polyline through the window, left to right. */
private fun drawSeries(
    samples: List<BenchSample>,
    x: (BenchSample) -> Float,
    y: (BenchSample) -> Float,
): Path = Path().apply {
    samples.forEachIndexed { i, s ->
        if (i == 0) moveTo(x(s), y(s)) else lineTo(x(s), y(s))
    }
}

/** A round number above the data, so the axis reads 200 rather than 187. */
private fun niceTop(max: Int): Int {
    val m = maxOf(max, 40)
    val step = when {
        m <= 100 -> 25
        m <= 500 -> 50
        m <= 2000 -> 250
        else -> 1000
    }
    return ((m + step - 1) / step) * step
}
