package com.handoff.band.ui.components

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.unit.dp

/**
 * The few glyphs Material's core icon set does not carry, traced from the
 * artboards in `design/android-redesign/`. Everything else — the cog, search,
 * back, the chevron — is `Icons.Filled` / `Icons.AutoMirrored`, because the
 * decisions doc is explicit that the settings cog has to be the one Android
 * itself draws, not a hand-drawn approximation of it.
 *
 * Strokes are black and tinted at the call site; `Icon(tint = …)` applies a
 * colour filter over the whole vector.
 */
object HandoffIcons {

    private fun stroked(name: String, width: Float, vararg paths: String): ImageVector =
        ImageVector.Builder(
            name = name, defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            for (d in paths) addPath(
                pathData = PathParser().parsePathString(d).toNodes(),
                fill = null,
                stroke = SolidColor(Color.Black),
                strokeLineWidth = width,
                strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
            )
        }.build()

    /** The wristband: a body with a strap arc top and bottom. */
    val Band: ImageVector by lazy {
        stroked("Band", 1.75f,
            "M8 8h8a2 2 0 0 1 2 2v4a2 2 0 0 1 -2 2h-8a2 2 0 0 1 -2 -2v-4a2 2 0 0 1 2 -2z",
            "M8 8V6.4a4 4 0 0 1 8 0V8M8 16v1.6a4 4 0 0 0 8 0V16")
    }

    /** Three lines of decreasing length — the sort control. */
    val Sort: ImageVector by lazy { stroked("Sort", 1.75f, "M4 7h13M4 12h9M4 17h5") }

    /** An ID card: your contact card is on the band. */
    val Card: ImageVector by lazy {
        stroked("Card", 1.6f,
            "M5 5.5h14a2.5 2.5 0 0 1 2.5 2.5v8a2.5 2.5 0 0 1 -2.5 2.5h-14a2.5 2.5 0 0 1 -2.5 -2.5v-8a2.5 2.5 0 0 1 2.5 -2.5z",
            "M8.5 9.1a1.9 1.9 0 1 0 0.001 0z",
            "M5.6 15.8a3.2 3.2 0 0 1 5.8 0M14 10.6h4M14 13.6h4")
    }

    /**
     * The same card with a diagonal slash — no card on the band. The slash is
     * the muted-microphone convention and needs no legend.
     */
    val CardOff: ImageVector by lazy {
        stroked("CardOff", 1.6f,
            "M5 5.5h14a2.5 2.5 0 0 1 2.5 2.5v8a2.5 2.5 0 0 1 -2.5 2.5h-14a2.5 2.5 0 0 1 -2.5 -2.5v-8a2.5 2.5 0 0 1 2.5 -2.5z",
            "M8.5 9.1a1.9 1.9 0 1 0 0.001 0z",
            "M5.6 15.8a3.2 3.2 0 0 1 5.8 0M14 10.6h4M14 13.6h4",
            "M3.5 20.5l17 -17")
    }

    /** A half-filled circle — the Theme row. */
    val Theme: ImageVector by lazy {
        ImageVector.Builder("Theme", 24.dp, 24.dp, 24f, 24f).apply {
            addPath(
                pathData = PathParser().parsePathString("M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0 -17z").toNodes(),
                fill = null, stroke = SolidColor(Color.Black), strokeLineWidth = 1.75f,
            )
            addPath(
                pathData = PathParser().parsePathString("M12 3.5a8.5 8.5 0 0 0 0 17z").toNodes(),
                fill = SolidColor(Color.Black),
            )
        }.build()
    }

    /** Four corner brackets around a dot — the Advanced row. */
    val Advanced: ImageVector by lazy {
        stroked("Advanced", 1.75f,
            "M10 4H5.5A1.5 1.5 0 0 0 4 5.5V10M14 4h4.5A1.5 1.5 0 0 1 20 5.5V10M10 20H5.5A1.5 1.5 0 0 1 4 18.5V14M14 20h4.5a1.5 1.5 0 0 0 1.5 -1.5V14",
            "M12 9.6a2.4 2.4 0 1 0 0.001 0z")
    }

    /** A phone with a plus — the auto-save row. */
    val PhoneAdd: ImageVector by lazy {
        stroked("PhoneAdd", 1.75f,
            "M8.5 2.5h7a2.5 2.5 0 0 1 2.5 2.5v14a2.5 2.5 0 0 1 -2.5 2.5h-7a2.5 2.5 0 0 1 -2.5 -2.5v-14a2.5 2.5 0 0 1 2.5 -2.5z",
            "M12 8.5v6M9 11.5h6")
    }

    /** A USB plug — power from the Pico's USB, the cell unmeasured. */
    val Plug: ImageVector by lazy {
        stroked("Plug", 1.75f,
            "M9 3v5M15 3v5",
            "M6 8h12v4a6 6 0 0 1 -12 0z",
            "M12 18v3")
    }
}
