package com.football.ar.ui.screens

import android.util.Log
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import com.football.ar.Screen
import com.football.ar.ui.components.ModeCard
import com.football.ar.ui.components.PitchBackground
import com.football.ar.ui.components.ScreenHeader
import com.football.ar.ui.theme.*
import com.football.ar.ui.utils.launchGameActivity

@Composable
fun ModeSelectScreen(navController: NavHostController) {
    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            ScreenHeader(
                title = "CHOISIR LE MODE",
                onBack = { navController.popBackStack() }
            )

            Spacer(modifier = Modifier.height(32.dp))

            ModeCard(
                title = "MATCH RAPIDE",
                subtitle = "vs Intelligence Artificielle",
                icon = Icons.Filled.Computer,
                description = "Affronte l'IA en solo avec le rendu 3D classique",
                color = AccentGreen,
                onClick = {
                    navController.navigate(Screen.TeamSelect.createRoute("vs_ai"))
                }
            )

            Spacer(modifier = Modifier.height(20.dp))

            ModeCard(
                title = "REALITE AUGMENTEE",
                subtitle = "vs IA en AR",
                icon = Icons.Filled.CameraAlt,
                description = "Joue sur ton terrain physique avec la camera AR",
                color = AlgeriaGreen,
                onClick = {
                    navController.navigate(Screen.TeamSelect.createRoute("ar"))
                }
            )

            Spacer(modifier = Modifier.height(20.dp))

            ModeCard(
                title = "1 vs 1 JOUEUR",
                subtitle = "Creer ou rejoindre un match PvP",
                icon = Icons.Filled.Group,
                description = "Affronte un autre joueur en ligne",
                color = AccentGold,
                onClick = {
                    navController.navigate(Screen.TeamSelect.createRoute("1v1"))
                }
            )

            Spacer(modifier = Modifier.height(20.dp))

            val context = LocalContext.current
            ModeCard(
                title = "TEST IA vs IA",
                subtitle = "MCO vs MCA (Bots)",
                icon = Icons.Filled.SmartToy,
                description = "Observe un match entre deux equipes bots",
                color = AlgeriaRed,
                onClick = {
                    launchGameActivity(
                        context = context,
                        mode = "ai_vs_ai",
                        forceClassic = true,
                        forceMcoVsMca = true
                    )
                }
            )
        }
    }
}
