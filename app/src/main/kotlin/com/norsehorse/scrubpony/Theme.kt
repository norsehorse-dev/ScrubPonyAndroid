package com.norsehorse.scrubpony

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Design tokens for ScrubPony, matched to scrubpony.app: a near-black
 * background, a lavender accent, off-white ink, and the site's serif-display /
 * monospace-label flavor. Dark only, the way the site and the sibling apps
 * present themselves.
 */
object ScrubPonyTheme {
    val background = Color(0xFF0B0A0F)
    val panel = Color(0xFF16141D)
    val panelHigh = Color(0xFF1C1926)
    val field = Color(0xFF201D2A)
    val line = Color(0xFF2A2636)
    val ink = Color(0xFFF1ECFA)
    val dim = Color(0xFF948EA6)
    val accent = Color(0xFFA878E6)        // warm lavender, tuned to the app icon
    val accentBright = Color(0xFFCBB2F6)  // the italic "Keep the photo."
    val accentDeep = Color(0xFF7A56A8)
    val onAccent = Color(0xFF130F1E)      // dark ink on a lavender button
    val success = Color(0xFF4CC38A)       // terminal green / "clean"
    val danger = Color(0xFFEB5757)
}

private val ScrubPonyColorScheme = darkColorScheme(
    primary = ScrubPonyTheme.accent,
    onPrimary = ScrubPonyTheme.onAccent,
    secondary = ScrubPonyTheme.accentBright,
    onSecondary = ScrubPonyTheme.onAccent,
    tertiary = ScrubPonyTheme.success,
    background = ScrubPonyTheme.background,
    onBackground = ScrubPonyTheme.ink,
    surface = ScrubPonyTheme.panel,
    onSurface = ScrubPonyTheme.ink,
    surfaceVariant = ScrubPonyTheme.field,
    onSurfaceVariant = ScrubPonyTheme.dim,
    outline = ScrubPonyTheme.line,
    error = ScrubPonyTheme.danger,
    onError = ScrubPonyTheme.ink,
    surfaceContainer = ScrubPonyTheme.panel,
    surfaceContainerHigh = ScrubPonyTheme.panelHigh,
    surfaceContainerHighest = ScrubPonyTheme.panelHigh,
    surfaceContainerLow = ScrubPonyTheme.background,
    surfaceContainerLowest = ScrubPonyTheme.background,
)

// Serif for display/headlines (the site's wordmark and hero), monospace for
// labels and the version line, the default sans for body copy.
private val ScrubPonyTypography = Typography(
    displaySmall = TextStyle(
        fontFamily = FontFamily.Serif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 34.sp,
        lineHeight = 40.sp,
    ),
    headlineMedium = TextStyle(
        fontFamily = FontFamily.Serif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 26.sp,
        lineHeight = 32.sp,
    ),
    titleLarge = TextStyle(
        fontFamily = FontFamily.Serif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 22.sp,
        lineHeight = 28.sp,
    ),
    labelSmall = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        letterSpacing = 1.sp,
    ),
)

// The italic-serif style used for the lavender "Pony" and hero accent words.
val ScrubPonySerifItalic = TextStyle(
    fontFamily = FontFamily.Serif,
    fontStyle = FontStyle.Italic,
    fontWeight = FontWeight.SemiBold,
)

val ScrubPonyMono = TextStyle(fontFamily = FontFamily.Monospace)

@Composable
fun ScrubPonyTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = ScrubPonyColorScheme,
        typography = ScrubPonyTypography,
        content = content,
    )
}
