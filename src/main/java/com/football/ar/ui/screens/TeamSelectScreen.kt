package com.football.ar.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import com.football.ar.Screen
import com.football.ar.ui.components.*
import com.football.ar.ui.theme.*
import com.football.ar.ui.utils.launchGameActivity

@Composable
fun TeamSelectScreen(navController: NavHostController, mode: String) {
    var teams by remember { mutableStateOf<List<TeamInfo>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var selectedTeamA by remember { mutableStateOf<TeamInfo?>(null) }
    var selectedTeamB by remember { mutableStateOf<TeamInfo?>(null) }
    var step by remember { mutableStateOf(1) }

    LaunchedEffect(Unit) {
        teams = fetchTeamsFromCatalog()
        isLoading = false
    }

    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            ScreenHeader(
                title = if (step == 1) "CHOISIR TON EQUIPE" else "CHOISIR L'ADVERSAIRE",
                onBack = { navController.popBackStack() }
            )

            Spacer(modifier = Modifier.height(16.dp))
            MatchPreviewBar(selectedTeamA, selectedTeamB)
            Spacer(modifier = Modifier.height(16.dp))

            if (isLoading) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = AccentGreen, modifier = Modifier.size(64.dp))
                }
            } else {
                LazyVerticalGrid(
                    columns = GridCells.Fixed(2),
                    modifier = Modifier.weight(1f),
                    contentPadding = PaddingValues(8.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    items(teams) { team ->
                        TeamCard(
                            team = team,
                            isSelected = (step == 1 && selectedTeamA?.id == team.id) ||
                                    (step == 2 && selectedTeamB?.id == team.id),
                            onClick = {
                                if (step == 1) {
                                    selectedTeamA = team
                                    if (mode == "1v1") {
                                        step = 2
                                    } else {
                                        // For vs_ai and ar, go to team composition screen
                                        val opponent = teams.firstOrNull { it.id != team.id }
                                        navController.navigate(
                                            Screen.TeamComposition.createRoute(
                                                mode,
                                                team.id,
                                                opponent?.id ?: ""
                                            )
                                        )
                                    }
                                } else {
                                    selectedTeamB = team
                                    if (mode == "1v1") {
                                        navController.navigate(
                                            Screen.WaitingPvP.createRoute(selectedTeamA!!.id, team.id)
                                        )
                                    }
                                }
                            }
                        )
                    }
                }
            }
        }
    }
}

fun fetchTeamsFromCatalog(): List<TeamInfo> {
    return try {
        val url = java.net.URL("${com.football.ar.Config.catalogUrl}/teams")
        val conn = url.openConnection() as java.net.HttpURLConnection
        conn.requestMethod = "GET"
        conn.connectTimeout = 5000
        conn.readTimeout = 5000
        conn.instanceFollowRedirects = false
        if (conn.responseCode == 200) {
            val response = conn.inputStream.bufferedReader().use { it.readText() }
            val jsonArr = org.json.JSONArray(response)
            val result = mutableListOf<TeamInfo>()
            for (i in 0 until jsonArr.length()) {
                val obj = jsonArr.getJSONObject(i)
                result.add(
                    TeamInfo(
                        id = obj.optString("id", ""),
                        name = obj.optString("name", ""),
                        shortName = obj.optString("short_name", ""),
                        city = obj.optString("city", null),
                        country = obj.optString("country", null),
                        logoUrl = obj.optString("logo_url", null),
                        colorPrimary = obj.optString("color_primary", null),
                        colorSecondary = obj.optString("color_secondary", null),
                        league = obj.optString("league", null)
                    )
                )
            }
            result
        } else {
            android.util.Log.w("TeamSelect", "Catalog returned HTTP ${conn.responseCode}, using fallback")
            fetchTeamsFallback()
        }
    } catch (e: Exception) {
        android.util.Log.e("TeamSelect", "Failed to fetch teams from catalog: ${e.javaClass.simpleName}: ${e.message}")
        e.printStackTrace()
        fetchTeamsFallback()
    }
}

fun fetchTeamsFallback(): List<TeamInfo> {
    return listOf(
        TeamInfo("mca", "Mouloudia Club d'Alger", "MCA", "Alger", "Algerie", null, "#D32F2F", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("mco", "MC Oran", "MCO", "Oran", "Algerie", null, "#1976D2", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("ess", "ES Setif", "ESS", "Setif", "Algerie", null, "#388E3C", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("jsk", "JS Kabylie", "JSK", "Tizi Ouzou", "Algerie", null, "#FBC02D", "#1976D2", "Ligue 1 Mobilis"),
        TeamInfo("crb", "CR Belouizdad", "CRB", "Alger", "Algerie", null, "#D32F2F", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("usb", "USM Bel Abbes", "USB", "Bel Abbes", "Algerie", null, "#388E3C", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("na", "NA Hussein Dey", "NAHD", "Alger", "Algerie", null, "#1976D2", "#FFEB3B", "Ligue 1 Mobilis"),
        TeamInfo("pac", "Paradou AC", "PAC", "Alger", "Algerie", null, "#388E3C", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("csc", "CS Constantine", "CSC", "Constantine", "Algerie", null, "#D32F2F", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("usmk", "USM Khenchela", "USMK", "Khenchela", "Algerie", null, "#FF8F00", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("asoc", "ASO Chlef", "ASO", "Chlef", "Algerie", null, "#1976D2", "#FFFFFF", "Ligue 1 Mobilis"),
        TeamInfo("mbrou", "MB Rouissat", "MBR", "Rouissat", "Algerie", null, "#388E3C", "#FFFFFF", "Ligue 1 Mobilis")
    )
}

fun launchGameWithTeams(context: android.content.Context, mode: String, teamA: String?, teamB: String?) {
    launchGameActivity(
        context = context,
        mode = mode,
        forceClassic = (mode != "ar"),
        teamA = teamA,
        teamB = teamB
    )
}
