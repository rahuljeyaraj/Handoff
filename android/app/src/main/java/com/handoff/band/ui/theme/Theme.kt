package com.handoff.band.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import com.handoff.band.data.Prefs

/**
 * The app's visual language, design decisions §12.
 *
 * Material 3 with one deliberate choice in the palette: the primary is a deep
 * indigo that stays clear of green, amber and red, so those three remain free
 * to mean "connected / low battery / critical" and nothing else. The values
 * are the tokens from the artboards in `design/android-redesign/`, which are
 * the reference — if a colour here and a colour there disagree, the artboard
 * is right.
 */

private val Light = lightColorScheme(
    primary = Color(0xFF40518B),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFDDE1FF),
    onPrimaryContainer = Color(0xFF172052),

    surface = Color(0xFFFCF9F6),
    onSurface = Color(0xFF1C1B1A),
    onSurfaceVariant = Color(0xFF5B5852),
    surfaceContainerLow = Color(0xFFF8F4F0),
    surfaceContainer = Color(0xFFF3EFEA),
    surfaceContainerHigh = Color(0xFFEDE8E2),
    surfaceContainerHighest = Color(0xFFE7E1DB),
    surfaceVariant = Color(0xFFE7E1DB),
    background = Color(0xFFFCF9F6),
    onBackground = Color(0xFF1C1B1A),

    outline = Color(0xFF8D8A84),
    outlineVariant = Color(0xFFDCD7D0),

    error = Color(0xFFBA1A1A),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
)

private val Dark = darkColorScheme(
    primary = Color(0xFFBBC4FF),
    onPrimary = Color(0xFF0A1D5A),
    primaryContainer = Color(0xFF2A3A72),
    onPrimaryContainer = Color(0xFFDDE1FF),

    surface = Color(0xFF141413),
    onSurface = Color(0xFFE8E4DD),
    onSurfaceVariant = Color(0xFFADA8A1),
    surfaceContainerLow = Color(0xFF1B1A18),
    surfaceContainer = Color(0xFF211F1D),
    surfaceContainerHigh = Color(0xFF2B2926),
    surfaceContainerHighest = Color(0xFF363330),
    surfaceVariant = Color(0xFF363330),
    background = Color(0xFF141413),
    onBackground = Color(0xFFE8E4DD),

    outline = Color(0xFF95918A),
    outlineVariant = Color(0xFF3A3733),

    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
)

/**
 * The two colours Material has no slot for. `ok` is the connected dot and the
 * healthy battery; `warn` is the low battery and the "no card on the band"
 * glyph. Error is Material's own.
 */
@Immutable
data class SemanticColors(val ok: Color, val warn: Color)

private val LightSemantic = SemanticColors(ok = Color(0xFF2E6B4F), warn = Color(0xFF8A5A00))
private val DarkSemantic = SemanticColors(ok = Color(0xFF7FD6A8), warn = Color(0xFFE7BE72))

private val LocalSemanticColors = staticCompositionLocalOf { LightSemantic }

/** `MaterialTheme.semantic.ok`, alongside `MaterialTheme.colorScheme`. */
val MaterialTheme.semantic: SemanticColors
    @Composable @ReadOnlyComposable get() = LocalSemanticColors.current

@Composable
fun HandoffTheme(
    mode: Prefs.Theme = Prefs.Theme.SYSTEM,
    content: @Composable () -> Unit,
) {
    val dark = when (mode) {
        Prefs.Theme.SYSTEM -> isSystemInDarkTheme()
        Prefs.Theme.LIGHT -> false
        Prefs.Theme.DARK -> true
    }
    val scheme: ColorScheme = if (dark) Dark else Light
    CompositionLocalProvider(LocalSemanticColors provides if (dark) DarkSemantic else LightSemantic) {
        MaterialTheme(colorScheme = scheme, typography = HandoffTypography, content = content)
    }
}
