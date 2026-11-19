package com.karyon.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

private val KaryonWorkbenchColors = darkColorScheme(
    primary = Color(0xFFC6F432),
    onPrimary = Color(0xFF0E0F0C),
    primaryContainer = Color(0xFF1E211B),
    onPrimaryContainer = Color(0xFFC6F432),
    secondary = Color(0xFFD6FA63),
    onSecondary = Color(0xFF0E0F0C),
    secondaryContainer = Color(0xFF1E211B),
    onSecondaryContainer = Color(0xFFC6F432),
    tertiary = Color(0xFFA9D41E),
    background = Color(0xFF0E0F0C),
    onBackground = Color(0xFFF2F3EC),
    surface = Color(0xFF161814),
    onSurface = Color(0xFFF2F3EC),
    surfaceVariant = Color(0xFF1E211B),
    onSurfaceVariant = Color(0xFFA4A99A),
    surfaceContainer = Color(0xFF161814),
    surfaceContainerLow = Color(0xFF121410),
    surfaceContainerHigh = Color(0xFF1E211B),
    outline = Color(0xFF2E3329),
    outlineVariant = Color(0xFF22261F),
    error = Color(0xFFFF6B4A)
)

private val KaryonTypography = Typography(
    headlineSmall = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 20.sp,
        letterSpacing = (-0.3).sp
    ),
    titleMedium = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 15.sp
    ),
    bodyMedium = TextStyle(
        fontSize = 13.5.sp
    ),
    labelMedium = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontSize = 11.5.sp
    )
)

@Composable
fun KaryonTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = KaryonWorkbenchColors,
        typography = KaryonTypography,
        content = content
    )
}
