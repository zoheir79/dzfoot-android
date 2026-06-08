package com.football.ar

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.football.ar.ui.screens.*
import com.football.ar.ui.theme.DZFootTheme

class MenuActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            DZFootTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    val navController = rememberNavController()
                    DZFootNavHost(navController = navController)
                }
            }
        }
    }
}

sealed class Screen(val route: String) {
    object MainMenu : Screen("main_menu")
    object ModeSelect : Screen("mode_select")
    object TeamSelect : Screen("team_select/{mode}") {
        fun createRoute(mode: String) = "team_select/$mode"
    }
    object TeamComposition : Screen("team_composition/{mode}/{teamA}/{teamB}") {
        fun createRoute(mode: String, teamA: String, teamB: String) = "team_composition/$mode/$teamA/$teamB"
    }
    object WaitingPvP : Screen("waiting_pvp/{teamA}/{teamB}") {
        fun createRoute(teamA: String, teamB: String) = "waiting_pvp/$teamA/$teamB"
    }
    object WaitingJoin : Screen("waiting_join")
}

@Composable
fun DZFootNavHost(navController: NavHostController) {
    NavHost(
        navController = navController,
        startDestination = Screen.MainMenu.route
    ) {
        composable(Screen.MainMenu.route) {
            MainMenuScreen(navController = navController)
        }
        composable(Screen.ModeSelect.route) {
            ModeSelectScreen(navController = navController)
        }
        composable(Screen.TeamSelect.route) { backStackEntry ->
            val mode = backStackEntry.arguments?.getString("mode") ?: "vs_ai"
            TeamSelectScreen(navController = navController, mode = mode)
        }
        composable(Screen.TeamComposition.route) { backStackEntry ->
            val mode = backStackEntry.arguments?.getString("mode") ?: "vs_ai"
            val teamA = backStackEntry.arguments?.getString("teamA") ?: ""
            val teamB = backStackEntry.arguments?.getString("teamB") ?: ""
            TeamCompositionScreen(
                navController = navController,
                mode = mode,
                teamAId = teamA,
                teamBId = teamB
            )
        }
        composable(Screen.WaitingPvP.route) { backStackEntry ->
            val teamA = backStackEntry.arguments?.getString("teamA") ?: ""
            val teamB = backStackEntry.arguments?.getString("teamB") ?: ""
            WaitingPvPScreen(navController = navController, teamA = teamA, teamB = teamB, isHost = true)
        }
        composable(Screen.WaitingJoin.route) {
            WaitingJoinScreen(navController = navController)
        }
    }
}
