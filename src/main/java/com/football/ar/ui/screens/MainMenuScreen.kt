package com.football.ar.ui.screens

import android.util.Log
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.football.ar.Screen
import com.football.ar.ui.components.MenuButton
import com.football.ar.ui.components.PitchBackground
import com.football.ar.ui.theme.*
import kotlinx.coroutines.delay

@Composable
fun MainMenuScreen(navController: NavHostController) {
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        delay(200)
        visible = true
    }

    val infiniteTransition = rememberInfiniteTransition(label = "logo")
    val scale by infiniteTransition.animateFloat(
        initialValue = 1f,
        targetValue = 1.05f,
        animationSpec = infiniteRepeatable(
            animation = tween(2000, easing = EaseInOutSine),
            repeatMode = RepeatMode.Reverse
        ),
        label = "scale"
    )

    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        AnimatedVisibility(
            visible = visible,
            enter = fadeIn(animationSpec = tween(1000)),
            exit = fadeOut()
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 48.dp, vertical = 32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                Spacer(modifier = Modifier.weight(0.15f))

                Box(
                    modifier = Modifier
                        .scale(scale)
                        .size(140.dp)
                        .clip(RoundedCornerShape(24.dp))
                        .background(
                            brush = Brush.radialGradient(
                                colors = listOf(AccentGreen, PitchGreenDark)
                            )
                        )
                        .border(3.dp, AccentGold, RoundedCornerShape(24.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text = "DZ",
                            style = MaterialTheme.typography.displayLarge,
                            color = TextPrimary,
                            modifier = Modifier.padding(bottom = 2.dp)
                        )
                        Text(
                            text = "FOOT",
                            style = MaterialTheme.typography.headlineLarge,
                            color = AccentGold,
                            textAlign = TextAlign.Center
                        )
                    }
                }

                Text(
                    text = "LIGUE 1 ALGERIE",
                    style = MaterialTheme.typography.titleMedium,
                    color = AccentGold.copy(alpha = 0.8f),
                    letterSpacing = 6.sp,
                    modifier = Modifier.padding(top = 16.dp, bottom = 48.dp)
                )

                Spacer(modifier = Modifier.weight(0.1f))

                MenuButton(
                    icon = Icons.Filled.SportsSoccer,
                    text = "JOUER",
                    subtext = "Match rapide, vs IA ou PvP",
                    onClick = { navController.navigate(Screen.ModeSelect.route) }
                )
                Spacer(modifier = Modifier.height(16.dp))
                MenuButton(
                    icon = Icons.Filled.Settings,
                    text = "PARAMETRES",
                    subtext = "Configuration et options",
                    onClick = { Log.i("Menu", "Settings clicked") }
                )
                Spacer(modifier = Modifier.height(16.dp))
                MenuButton(
                    icon = Icons.Filled.Info,
                    text = "A PROPOS",
                    subtext = "Credits et informations",
                    onClick = { Log.i("Menu", "About clicked") }
                )

                Spacer(modifier = Modifier.weight(0.2f))

                Text(
                    text = "Version 1.0 - Beta",
                    style = MaterialTheme.typography.labelMedium,
                    color = TextSecondary.copy(alpha = 0.6f)
                )
            }
        }
    }
}
