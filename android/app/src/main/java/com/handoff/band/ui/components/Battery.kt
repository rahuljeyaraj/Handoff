package com.handoff.band.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.handoff.band.ble.BandStatus
import com.handoff.band.ui.theme.semantic

/**
 * The band's battery, design decisions §8. Three blocks the way a phone used
 * to show it, plus two states that are not levels at all.
 *
 * UNKNOWN IS ITS OWN STATE AND NEVER RENDERS AS CRITICAL. Before the first
 * status arrives, or from a band whose firmware does not report a voltage,
 * you genuinely do not know — and a false empty battery sends someone
 * chasing a dead cell that is not dead.
 *
 * USB is not charging. The charger is off-board on J5 and the app cannot see
 * it; VSYS above any Li-ion voltage only says the Pico is on USB and the cell
 * is behind D1, unmeasured. So it is a plug, never a bolt.
 */
enum class BatteryLevel(val label: String) {
    FULL("Full"),
    GOOD("Good"),
    LOW("Low"),
    CRITICAL("Critical"),
    UNKNOWN("Unknown"),
    USB("USB power");

    val blocks: Int get() = when (this) { FULL -> 3; GOOD -> 2; LOW -> 1; else -> 0 }

    companion object {
        /** D1's drop between the cell and VSYS: 0.3 V idle, 0.4 V with the radio on. */
        const val DIODE_MV = 350

        /**
         * From the band's status. VSYS is after D1, so the cell is VSYS plus
         * the drop. Thresholds are coarse on purpose: a Li-ion curve is nearly
         * flat between 3.7 and 3.9 V, so a percentage would be fiction.
         */
        fun from(status: BandStatus?): BatteryLevel {
            val vsys = status?.vsysMv ?: return UNKNOWN
            if (status.usbPower) return USB
            val cell = vsys + DIODE_MV
            return when {
                cell >= 4000 -> FULL
                cell >= 3700 -> GOOD
                cell >= 3500 -> LOW
                else -> CRITICAL
            }
        }
    }
}

/** Battery is always the icon, never words or volts (§12). */
@Composable
fun BatteryIcon(level: BatteryLevel, modifier: Modifier = Modifier) {
    val colour = when (level) {
        BatteryLevel.FULL, BatteryLevel.GOOD -> MaterialTheme.semantic.ok
        BatteryLevel.LOW -> MaterialTheme.semantic.warn
        BatteryLevel.CRITICAL -> MaterialTheme.colorScheme.error
        BatteryLevel.UNKNOWN -> MaterialTheme.colorScheme.outline
        BatteryLevel.USB -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    if (level == BatteryLevel.USB) {
        Icon(HandoffIcons.Plug, contentDescription = level.label,
             tint = colour, modifier = modifier.size(20.dp))
        return
    }

    Canvas(
        modifier
            .size(width = 26.dp, height = 15.dp)
            .semantics { contentDescription = "Battery ${level.label.lowercase()}" }
    ) {
        // The artboard's 28×16 viewBox, scaled to whatever we were given.
        val s = size.width / 28f
        val stroke = 1.4f * s
        val dashed = if (level == BatteryLevel.UNKNOWN)
            PathEffect.dashPathEffect(floatArrayOf(2.6f * s, 2f * s)) else null

        drawRoundRect(
            color = colour,
            topLeft = Offset(0.9f * s, 2.9f * s),
            size = Size(22.2f * s, 10.2f * s),
            cornerRadius = CornerRadius(2.6f * s),
            style = Stroke(width = stroke, pathEffect = dashed),
        )
        // The terminal nub.
        drawRoundRect(
            color = colour,
            topLeft = Offset(24.6f * s, 5.8f * s),
            size = Size(2.6f * s, 4.4f * s),
            cornerRadius = CornerRadius(1.2f * s),
        )

        if (level == BatteryLevel.UNKNOWN) {
            // A dash where the blocks would be: "no reading", not "empty".
            drawRoundRect(
                color = colour,
                topLeft = Offset(8.5f * s, 7.3f * s),
                size = Size(7f * s, 1.4f * s),
                cornerRadius = CornerRadius(0.7f * s),
            )
            return@Canvas
        }

        for (i in 0 until level.blocks) {
            drawRoundRect(
                color = colour,
                topLeft = Offset((2.9f + 6.5f * i) * s, 5f * s),
                size = Size(5.2f * s, 6f * s),
                cornerRadius = CornerRadius(0.9f * s),
            )
        }
    }
}
