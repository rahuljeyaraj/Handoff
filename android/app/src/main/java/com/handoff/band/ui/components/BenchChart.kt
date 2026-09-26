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
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.handoff.band.ble.BenchSample
import com.handoff.band.ble.BenchTrace
import com.handoff.band.ui.theme.MonoStyle

/**
 * ONE LINE: how much of the time the band could hear the other band.
 *
 * Every half second the band reports two counters — the windows it spent
 * LISTENING, and how many of those its detector called busy. This draws the
 * second as a percentage of the first. Nothing else is on the canvas: no
 * threshold, because there is nothing to compare, and no level axis, because
 * 0 % and 100 % need no interpreting.
 *
 * WHY THE THREE-LINE PLOT WENT. It drew the band's spectrum — a MAXIMUM over
 * about ten thousand windows — against `sqrt(k) * room`. k is solved in the
 * firmware's config.h from a false-busy rate per WINDOW decision, one window in
 * roughly 1.2 million; the largest of ten thousand draws from noise is a
 * completely different statistic, and the line carried none of the margin its
 * derivation implied. Measured 26 Sep 2026 with the peer band's link switched
 * off at its console, so that nothing at all was on the channel: the plotted
 * signal sat three times above its own "needs" line continuously, while the
 * detector called 79 of 295 021 listening windows busy — 0.03 %. Two statistics
 * on the same samples, four orders of magnitude apart. The detector was right;
 * the picture was not.
 *
 * It also explained the second half of the symptom, the zigzag when the bands
 * were close. A peak is taken over whatever windows are left after the band's
 * own transmissions are cut out, and a handshaking band transmits far more, so
 * the pool it was maximised over shrank and shifted. A percentage of that pool
 * does not care how big the pool is.
 *
 * WHAT A VIEWER SHOULD SEE. Bands apart: flat on zero. Bands touching or worn
 * on two wrists: it climbs, and it climbs before any card has been exchanged —
 * which is the useful part, because it says the skin path is there while the
 * handshake is still being attempted.
 *
 * THE ONE THING TO KNOW ABOUT READING IT. It is a rate over the gap between two
 * blocks, not a level at an instant, so it is honest across a dropped block —
 * the gap just covers more time. A gap the phone slept through is refused
 * rather than drawn (BandBank.heardPercent).
 */

/* Categorical slot 1 of the validated palette, stepped per mode. One series, so
 * the palette question is only "is this readable on both surfaces": 4.6:1 light
 * and 5.9:1 dark against the surface, both over the 3:1 bar for a graphical
 * object. The value is printed beside the key regardless — identity never by
 * colour alone (theme §12). */
private val HeardLight = Color(0xFF2A78D6)
private val HeardDark = Color(0xFF3987E5)

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
    val heardColor = if (dark) HeardDark else HeardLight
    val ink = MaterialTheme.colorScheme.onSurfaceVariant
    val grid = MaterialTheme.colorScheme.outlineVariant

    val samples = trace.window(window.ms).filter { it.heardPct != null }
    val last = samples.lastOrNull()

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
                "Waiting for readings. The band sends these twice a second " +
                    "while the app is connected, and the first one has nothing " +
                    "to be measured against.",
                style = MaterialTheme.typography.bodySmall,
                color = ink,
                modifier = Modifier.padding(vertical = 8.dp),
            )
            return@Column
        }

        val top = niceTop(samples.maxOf { it.heardPct ?: 0f })

        Box(Modifier.fillMaxWidth().height(170.dp)) {
            Canvas(Modifier.fillMaxWidth().height(170.dp)) {
                val h = size.height
                val w = size.width
                // Half a stroke of room top and bottom. A reading of 0.0 % is
                // the normal state of this chart — bands apart — and centred
                // on the canvas edge it drew as half a line, which reads as a
                // rendering fault rather than as the answer.
                val stroke = 2.5.dp.toPx()
                val pad = stroke / 2f
                val plot = h - stroke

                // Recessive chrome, and the only other marks on the canvas.
                for (f in listOf(0f, 0.5f, 1f)) {
                    val y = h - pad - f * plot
                    drawLine(grid, Offset(0f, y), Offset(w, y), strokeWidth = 1f)
                }

                val t0 = samples.first().atMs
                val span = (samples.last().atMs - t0).coerceAtLeast(1L).toFloat()
                fun x(s: BenchSample) = (s.atMs - t0) / span * w
                fun y(v: Float) = h - pad - (v.coerceIn(0f, top) / top) * plot

                drawPath(
                    drawSeries(samples, ::x) { y(it.heardPct ?: 0f) }, heardColor,
                    style = Stroke(width = stroke,
                                   cap = StrokeCap.Round, join = StrokeJoin.Round),
                )
            }

            Text("${top.toInt()}%", style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.TopStart))
            Text("0", style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.BottomStart))
            Text(window.label, style = MonoStyle.copy(fontSize = 11.sp), color = ink,
                 modifier = Modifier.align(Alignment.BottomEnd))
        }

        Spacer(Modifier.height(8.dp))

        // The name in words, because "busy fraction" is the band's vocabulary
        // and not a viewer's, and the value because a line is hard to read a
        // number off.
        Row(Modifier.fillMaxWidth()) {
            Key(heardColor, "heard", last?.heardPct?.let { pct(it) } ?: "-")
        }
    }
}

/** One decimal, because an empty channel must read 0.0 and not round to it. */
private fun pct(v: Float) = "%.1f%%".format(v)

@Composable
private fun Key(color: Color, name: String, value: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Spacer(Modifier.width(6.dp))
        Text("$name $value of the time", style = MonoStyle,
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

/**
 * A round percentage above the data.
 *
 * The floor is 20 %, not the 100 % a percentage axis invites. A coupled pair
 * reads in the tens — the peer is on air about a tenth of the time when it is
 * only beaconing — so a fixed full-scale axis would draw every real reading
 * flat against the bottom, which is the failure the old chart had for a
 * different reason. The top is labelled, so a scale that moves is a scale that
 * says so.
 */
private fun niceTop(max: Float): Float {
    val steps = listOf(20f, 40f, 60f, 80f, 100f)
    return steps.firstOrNull { max <= it } ?: 100f
}
