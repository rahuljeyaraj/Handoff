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

    /** A person with a plus beside the head — "Create contact", the shape
     *  the system Contacts app uses for the same button. */
    val PersonAdd: ImageVector by lazy {
        stroked("PersonAdd", 1.75f,
            "M13 8a4 4 0 1 1 -8 0a4 4 0 0 1 8 0z",
            "M1.5 20v-0.5a5.5 5.5 0 0 1 5.5 -5.5h4a5.5 5.5 0 0 1 5.5 5.5v0.5",
            "M19.5 7.5v6M16.5 10.5h6")
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

    /** A body with motion lines either side — the Vibrate row. */
    val Vibrate: ImageVector by lazy {
        stroked("Vibrate", 1.75f,
            "M9 4.5h6a1 1 0 0 1 1 1v13a1 1 0 0 1 -1 1h-6a1 1 0 0 1 -1 -1v-13a1 1 0 0 1 1 -1z",
            "M4.5 9v2M4.5 13v2M2 10.5v3",
            "M19.5 9v2M19.5 13v2M22 10.5v3")
    }

    /** A chip with pins on four sides — the Firmware row. */
    val Chip: ImageVector by lazy {
        stroked("Chip", 1.6f,
            "M8 6.5h8a1.5 1.5 0 0 1 1.5 1.5v8a1.5 1.5 0 0 1 -1.5 1.5h-8a1.5 1.5 0 0 1 -1.5 -1.5v-8a1.5 1.5 0 0 1 1.5 -1.5z",
            "M10 10h4v4h-4z",
            "M9.5 6.5V3.5M14.5 6.5V3.5M9.5 20.5v-3M14.5 20.5v-3",
            "M6.5 9.5h-3M6.5 14.5h-3M20.5 9.5h-3M20.5 14.5h-3")
    }

    /**
     * The wordmark, traced from `design/brand/handoff-wordmark.svg` (Consolas
     * Bold outlines, not a font reference) so it renders identically with no
     * font installed and can be embossed straight off the same path data.
     */
    val Wordmark: ImageVector by lazy {
        ImageVector.Builder(
            name = "Wordmark", defaultWidth = 150.dp, defaultHeight = 28.dp,
            viewportWidth = 381.2f, viewportHeight = 71.05f,
        ).apply {
            addPath(
                pathData = PathParser().parsePathString(
                    "M38.33,69.92 L38.33,42.14 L16.66,42.14 L16.66,69.92 L4.64,69.92 L4.64,6.11 L16.66,6.11 L16.66,31.73 L38.33,31.73 L38.33,6.11 L50.34,6.11 L50.34,69.92 L38.33,69.92 Z M92.87,69.92 L92.57,63.42 Q91.01,65.09 89.31,66.48 Q87.61,67.88 85.54,68.91 Q83.5,69.92 81.07,70.48 Q78.67,71.05 75.78,71.05 Q71.98,71.05 69.09,69.92 Q66.21,68.8 64.26,66.8 Q62.31,64.8 61.31,61.97 Q60.31,59.12 60.31,55.72 Q60.31,52.2 61.79,49.22 Q63.28,46.23 66.32,44.09 Q69.39,41.94 73.95,40.72 Q78.51,39.5 84.67,39.5 L91.17,39.5 L91.17,36.52 Q91.17,34.62 90.62,33.09 Q90.09,31.55 88.89,30.47 Q87.7,29.39 85.79,28.81 Q83.89,28.22 81.11,28.22 Q76.71,28.22 72.43,29.22 Q68.17,30.22 64.17,32.03 L64.17,22.52 Q67.73,21.09 72.39,20.17 Q77.06,19.23 82.07,19.23 Q87.61,19.23 91.56,20.3 Q95.51,21.34 98.07,23.44 Q100.64,25.53 101.86,28.69 Q103.07,31.83 103.07,36.03 L103.07,69.92 L92.87,69.92 Z M91.17,48 L83.89,48 Q80.86,48 78.76,48.59 Q76.67,49.17 75.34,50.2 Q74.03,51.22 73.42,52.56 Q72.81,53.91 72.81,55.42 Q72.81,58.45 74.76,60.05 Q76.71,61.62 80.07,61.62 Q82.57,61.62 85.25,59.81 Q87.93,58 91.17,54.64 L91.17,48 Z M146.34,69.92 L146.34,37.59 Q146.34,29.44 140.29,29.44 Q137.26,29.44 134.49,31.89 Q131.74,34.33 128.66,38.53 L128.66,69.92 L116.74,69.92 L116.74,20.31 L127.05,20.31 L127.35,27.64 Q128.85,25.73 130.49,24.17 Q132.13,22.61 134.04,21.52 Q135.95,20.41 138.13,19.83 Q140.34,19.23 143.07,19.23 Q146.88,19.23 149.71,20.48 Q152.54,21.73 154.45,24 Q156.35,26.27 157.3,29.47 Q158.26,32.67 158.26,36.62 L158.26,69.92 L146.34,69.92 Z M202.93,69.92 L202.64,62.59 Q201.08,64.5 199.41,66.06 Q197.75,67.62 195.85,68.73 Q193.94,69.83 191.77,70.44 Q189.6,71.05 186.96,71.05 Q182.57,71.05 179.14,69.3 Q175.74,67.53 173.41,64.28 Q171.1,61.03 169.88,56.39 Q168.66,51.75 168.66,46.05 Q168.66,39.2 170.58,34.2 Q172.5,29.2 175.93,25.91 Q179.35,22.61 184.04,21.03 Q188.72,19.44 194.24,19.44 Q196.1,19.44 197.97,19.69 Q199.85,19.92 201.32,20.31 L201.32,0.92 L213.24,0.92 L213.24,69.92 L202.93,69.92 Z M181.16,45.5 Q181.16,49.61 181.71,52.52 Q182.27,55.42 183.36,57.28 Q184.47,59.12 186.08,59.98 Q187.69,60.84 189.75,60.84 Q192.77,60.84 195.5,58.41 Q198.24,55.95 201.32,51.7 L201.32,30.03 Q199.91,29.48 197.83,29.16 Q195.75,28.81 193.66,28.81 Q190.77,28.81 188.47,29.98 Q186.18,31.16 184.54,33.31 Q182.91,35.45 182.04,38.56 Q181.16,41.66 181.16,45.5 Z M271.39,44.72 Q271.39,50.58 269.72,55.44 Q268.06,60.3 264.94,63.77 Q261.81,67.23 257.27,69.14 Q252.73,71.05 246.92,71.05 Q241.41,71.05 237.06,69.44 Q232.72,67.83 229.69,64.62 Q226.66,61.42 225.05,56.62 Q223.44,51.81 223.44,45.41 Q223.44,39.5 225.12,34.67 Q226.81,29.83 229.95,26.42 Q233.11,23 237.64,21.12 Q242.19,19.23 247.91,19.23 Q253.47,19.23 257.81,20.88 Q262.16,22.52 265.19,25.73 Q268.22,28.95 269.8,33.72 Q271.39,38.47 271.39,44.72 Z M258.98,45.02 Q258.98,37.3 256.08,33.42 Q253.17,29.55 247.52,29.55 Q244.39,29.55 242.19,30.77 Q239.98,31.98 238.56,34.11 Q237.16,36.23 236.5,39.06 Q235.84,41.89 235.84,45.11 Q235.84,52.88 238.97,56.81 Q242.09,60.73 247.52,60.73 Q250.48,60.73 252.69,59.55 Q254.89,58.34 256.25,56.22 Q257.62,54.09 258.3,51.22 Q258.98,48.34 258.98,45.02 Z M326.21,11.03 Q324.07,10.34 321.12,9.89 Q318.17,9.42 315.09,9.42 Q313,9.42 311.18,9.98 Q309.37,10.55 308.06,11.77 Q306.75,12.98 305.98,14.88 Q305.23,16.75 305.23,19.44 L305.23,27.44 L324.71,27.44 L324.71,36.72 L305.23,36.72 L305.23,69.92 L293.21,69.92 L293.21,36.72 L278.81,36.72 L278.81,27.44 L293.21,27.44 L293.21,19.92 Q293.21,14.64 294.75,10.86 Q296.29,7.08 299.09,4.67 Q301.9,2.25 305.86,1.12 Q309.81,0 314.7,0 Q317.92,0 320.82,0.42 Q323.73,0.83 326.21,1.42 L326.21,11.03 Z M381.2,11.03 Q379.05,10.34 376.1,9.89 Q373.15,9.42 370.07,9.42 Q367.98,9.42 366.16,9.98 Q364.35,10.55 363.04,11.77 Q361.73,12.98 360.96,14.88 Q360.21,16.75 360.21,19.44 L360.21,27.44 L379.7,27.44 L379.7,36.72 L360.21,36.72 L360.21,69.92 L348.2,69.92 L348.2,36.72 L333.79,36.72 L333.79,27.44 L348.2,27.44 L348.2,19.92 Q348.2,14.64 349.73,10.86 Q351.27,7.08 354.07,4.67 Q356.88,2.25 360.84,1.12 Q364.79,0 369.68,0 Q372.9,0 375.8,0.42 Q378.71,0.83 381.2,1.42 L381.2,11.03 Z"
                ).toNodes(),
                fill = SolidColor(Color.Black),
            )
        }.build()
    }
}
