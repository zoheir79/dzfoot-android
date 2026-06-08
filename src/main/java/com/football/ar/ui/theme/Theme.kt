package com.football.ar.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val DZFootColorScheme = darkColorScheme(
    primary = AccentGreen,
    onPrimary = TextPrimary,
    secondary = AccentGold,
    onSecondary = SurfaceDark,
    tertiary = AlgeriaRed,
    background = PitchGreenDark,
    onBackground = TextPrimary,
    surface = SurfaceDark,
    onSurface = TextPrimary,
    surfaceVariant = SurfaceElevated,
    onSurfaceVariant = TextSecondary,
    error = AlgeriaRed,
    onError = TextPrimary
)

@Composable
fun DZFootTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = DZFootColorScheme,
        typography = DZFootTypography,
        content = content
    )
}
