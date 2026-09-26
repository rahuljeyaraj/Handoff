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
import com.handoff.band.ble.BenchSample
import com.handoff.band.ble.BenchTrace
import com.handoff.band.ui.theme.MonoStyle

/**
 * The CFAR pair against time, from the band's own readings: the peak signal of
 * each interval, the instantaneous signal, and the threshold the band judged
 * them by.
 *
 * WHY THIS EXISTS AS A PICTURE. The numbers are already printed above it, and
 * for "is it hearing anything right now" a number is better. This answers a
 * different question: WHEN was it hearing. Under link v2 a beacon is on air for
 * eleven milliseconds and the band sends a block twice a second, so the
 * instantaneous signal lands in the empty room nearly every time — on
 * 25 Sep 2026 exactly that drew a flat line under the threshold while the band
 * was tripping its detector about seven times a second. The peak series is the
 * loudest window of each whole interval, so a brief event cannot hide between
 * two samples.
 *
 * READ BOTH SIGNAL SERIES AGAINST THE DASHED THRESHOLD, which is k times the
 * guard reference and is the ONE test the detector applies. There is no floor
 * and no second additive gate any more; step 5 deleted the detector that had
 * them. The threshold is a series rather than a constant because the reference
 * is measured live, so the bar moves with the room — and the reference itself
 * is not drawn, because the threshold is the only thing about it a reader acts
 * on.
 *
 * PEAK ABOVE THE LINE WHILE THE INSTANT SITS UNDER IT IS THE HEALTHY SHAPE for
 * two bands beaconing at each other. Both under it is a channel that carried
 * nothing at all in that interval.
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
 * relief rule applies and both series carry a visible value label. */
private val PeakLight = Color(0xFF2A78D6)
private val PeakDark = Color(0xFF3987E5)
private val NowLight = Color(0xFFE87BA4)
private val NowDark = Color(0xFFD55181)

private enum class Window(val label: String, val ms: Long) {
    SHORT("30 s", 30_000),
    MEDIUM("2 min", 120_000),
    LONG("10 min", 600_000),
}

@Composable
fun BenchChart(trace: BenchTrace, modifier: Modifier = Modifier) {
    var window by remember { mutableStateOf(Window.MEDIUM) }
    // Off the surface rather than isSystemInDarkTheme(), because the app has
    // its own light/dark override (Prefs.Theme) and the chart has to follow
    // the surface it is actually drawn on.
    val dark = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    val peakColor = if (dark) PeakDark else PeakLight
    val nowColor = if (dark) NowDark else NowLight
    val ink = MaterialTheme.colorScheme.onSurfaceVariant
    val grid = MaterialTheme.colorScheme.outlineVariant

    val samples = trace.window(window.ms)
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
                "Waiting for readings. The band sends two a second while the " +
                    "app is connected; the chart needs a few seconds of them.",
                style = MaterialTheme.typography.bodySmall,
                color = ink,
                modifier = Modifier.padding(vertical = 8.dp),
            )
            return@Column
        }

        val top = niceTop(samples.maxOf { maxOf(maxOf(it.signal, it.peak ?: 0), it.threshold) })

        Box(Modifier.fillMaxWidth().height(160.dp)) {
            Canvas(Modifier.fillMaxWidth().height(160.dp)) {
                val h = size.height
                val w = size.width

                // Recessive chrome: hairline, solid, never dashed (that is what
                // the threshold is, and the two must not read as the same
                // thing).
                for (f in listOf(0f, 0.5f, 1f)) {
                    val y = h - f * h
                    drawLine(grid, Offset(0f, y), Offset(w, y), strokeWidth = 1f)
                }

                val t0 = samples.first().atMs
                val span = (samples.last().atMs - t0).coerceAtLeast(1L).toFloat()
                fun x(s: BenchSample) = (s.atMs - t0) / span * w
                fun y(v: Int) = h - (v.coerceIn(0, top).toFloat() / top) * h

                // Dashed, and the only dashed thing here: it is a rule the
                // data is read against, not data and not chrome.
                drawPath(
                    drawSeries(samples, ::x) { y(it.threshold) }, ink,
                    style = Stroke(
                        width = 1.dp.toPx(),
                        pathEffect = PathEffect.dashPathEffect(
                            floatArrayOf(6.dp.toPx(), 5.dp.toPx())),
                    ),
                )

                // The instant first, so the peak — the series a reader acts
                // on — is the one on top where the two cross.
                drawPath(
                    drawSeries(samples, ::x) { y(it.signal) }, nowColor,
                    style = Stroke(width = 2.dp.toPx(),
                                   cap = StrokeCap.Round, join = StrokeJoin.Round),
                )
                // Absent until a trigger block has landed; falling back to the
                // instant would draw one series twice and read as agreement
                // between two measurements.
                if (samples.any { it.peak != null }) {
                    drawPath(
                        drawSeries(samples.filter { it.peak != null }, ::x) { y(it.peak ?: 0) },
                        peakColor,
                        style = Stroke(width = 2.dp.toPx(),
                                       cap = StrokeCap.Round, join = StrokeJoin.Round),
                    )
                }
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
            Key(peakColor, "peak", last?.peak?.toString() ?: "-")
            Spacer(Modifier.width(16.dp))
            Key(nowColor, "now", last?.signal?.toString() ?: "-")
            Spacer(Modifier.width(16.dp))
            Key(ink, "busy above", last?.threshold?.toString() ?: "-")
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
