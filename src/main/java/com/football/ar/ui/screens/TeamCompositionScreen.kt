package com.football.ar.ui.screens

import android.util.Log
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import coil.compose.AsyncImage
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.football.ar.ui.components.*
import com.football.ar.ui.theme.*
import com.football.ar.ui.utils.launchGameActivity
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

@Composable
fun TeamCompositionScreen(
    navController: NavHostController,
    mode: String,
    teamAId: String,
    teamBId: String
) {
    val context = LocalContext.current
    var teamA by remember { mutableStateOf<FormationResponse?>(null) }
    var teamB by remember { mutableStateOf<FormationResponse?>(null) }
    var isLoading by remember { mutableStateOf(true) }
    var selectedTab by remember { mutableStateOf(0) }

    LaunchedEffect(teamAId, teamBId) {
        teamA = fetchFormation(teamAId)
        teamB = fetchFormation(teamBId)
        isLoading = false
    }

    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            ScreenHeader(
                title = "COMPOSITION",
                onBack = { navController.popBackStack() }
            )

            Spacer(modifier = Modifier.height(12.dp))

            // Team tabs
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(SurfaceElevated.copy(alpha = 0.7f))
                    .padding(4.dp),
                horizontalArrangement = Arrangement.SpaceEvenly
            ) {
                val tabs = listOf(
                    teamA?.teamName ?: "Equipe A",
                    teamB?.teamName ?: "Equipe B"
                )
                tabs.forEachIndexed { index, label ->
                    val selected = selectedTab == index
                    val bgColor = if (selected) AccentGreen.copy(alpha = 0.3f) else Color.Transparent
                    val textColor = if (selected) AccentGreen else TextSecondary
                    TextButton(
                        onClick = { selectedTab = index },
                        modifier = Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(8.dp))
                            .background(bgColor),
                        colors = ButtonDefaults.textButtonColors(contentColor = textColor)
                    ) {
                        Text(
                            text = label,
                            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
                            fontSize = 14.sp,
                            maxLines = 1
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            if (isLoading) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = AccentGreen)
                }
            } else {
                val team = if (selectedTab == 0) teamA else teamB
                val primaryColor = try {
                    team?.colorPrimary?.let { Color(android.graphics.Color.parseColor(it)) } ?: AccentGreen
                } catch (_: Exception) { AccentGreen }

                Text(
                    text = "Formation: ${team?.formation?.size ?: 0} joueurs",
                    color = TextSecondary,
                    style = MaterialTheme.typography.labelMedium,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )

                Spacer(modifier = Modifier.height(8.dp))

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .verticalScroll(rememberScrollState())
                ) {
                    team?.players?.forEachIndexed { index, player ->
                        PlayerCard(
                            player = player,
                            index = index,
                            primaryColor = primaryColor
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Launch button
                if (selectedTab == 1 || teamB != null) {
                    Button(
                        onClick = {
                            launchGameActivity(
                                context = context,
                                mode = mode,
                                forceClassic = (mode != "ar"),
                                teamA = teamAId,
                                teamB = teamBId
                            )
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(56.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentGreen),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Text(
                            text = "LANCER LE MATCH",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun PlayerCard(
    player: PlayerInfo,
    index: Int,
    primaryColor: Color
) {
    val statKeys = listOf(
        "physical_velocity" to "VIT",
        "technical_shot" to "TIR",
        "technical_shortpass" to "PAS",
        "technical_standingtackle" to "DEF",
        "technical_dribble" to "DRI",
        "physical_stamina" to "END"
    )

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .wrapContentHeight(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = SurfaceElevated.copy(alpha = 0.85f))
    ) {
        Column(
            modifier = Modifier.padding(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(48.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(primaryColor.copy(alpha = 0.2f)),
                    contentAlignment = Alignment.Center
                ) {
                    if (player.photoUrl.isNotEmpty()) {
                        AsyncImage(
                            model = player.photoUrl,
                            contentDescription = player.name,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = androidx.compose.ui.layout.ContentScale.Crop
                        )
                    } else {
                        Text(
                            text = "${player.number}",
                            color = primaryColor,
                            fontWeight = FontWeight.Bold,
                            fontSize = 18.sp
                        )
                    }
                }
                Spacer(modifier = Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = player.name,
                        color = TextPrimary,
                        fontWeight = FontWeight.Bold,
                        fontSize = 16.sp,
                        maxLines = 1
                    )
                    Text(
                        text = player.position,
                        color = TextSecondary,
                        fontSize = 13.sp
                    )
                }
                Text(
                    text = "#${index + 1}",
                    color = TextSecondary.copy(alpha = 0.5f),
                    fontSize = 12.sp
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Stats bars
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                statKeys.forEach { (key, label) ->
                    val value = player.stat(key)
                    StatBar(label = label, value = value, color = primaryColor)
                }
            }
        }
    }
}

@Composable
fun StatBar(label: String, value: Float, color: Color) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth()
    ) {
        Text(
            text = label,
            color = TextSecondary,
            fontSize = 11.sp,
            modifier = Modifier.width(36.dp)
        )
        Box(
            modifier = Modifier
                .weight(1f)
                .height(6.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(TextSecondary.copy(alpha = 0.15f))
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(value)
                    .clip(RoundedCornerShape(3.dp))
                    .background(
                        when {
                            value >= 0.85f -> Color(0xFF4CAF50)
                            value >= 0.75f -> color
                            value >= 0.60f -> Color(0xFFFFA000)
                            else -> Color(0xFFF44336)
                        }
                    )
            )
        }
        Spacer(modifier = Modifier.width(8.dp))
        Text(
            text = "${(value * 100).toInt()}",
            color = TextPrimary,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.width(28.dp)
        )
    }
}

fun fetchFormation(teamId: String): FormationResponse? {
    return try {
        val url = URL("${com.football.ar.Config.catalogUrl}/teams/$teamId/formation")
        val conn = url.openConnection() as HttpURLConnection
        conn.requestMethod = "GET"
        conn.connectTimeout = 5000
        conn.readTimeout = 5000
        if (conn.responseCode == 200) {
            val response = conn.inputStream.bufferedReader().use { it.readText() }
            val obj = JSONObject(response)
            val playersArr = obj.optJSONArray("players") ?: return null
            val players = mutableListOf<PlayerInfo>()
            for (i in 0 until playersArr.length()) {
                val p = playersArr.getJSONObject(i)
                val skills = mutableMapOf<String, Float>()
                val skillsObj = p.optJSONObject("skills")
                if (skillsObj != null) {
                    val keys = skillsObj.keys()
                    while (keys.hasNext()) {
                        val key = keys.next()
                        skills[key] = skillsObj.optDouble(key, 0.7).toFloat()
                    }
                }
                players.add(
                    PlayerInfo(
                        name = p.optString("name", ""),
                        position = p.optString("position", "CM"),
                        number = p.optInt("number", i + 1),
                        skills = skills,
                        skinColor = p.optInt("skin_color", 3),
                        hairStyle = p.optString("hair_style", "short"),
                        hairColor = p.optString("hair_color", "black"),
                        height = p.optDouble("height", 1.78).toFloat(),
                        bodyType = p.optInt("body_type", 1),
                        beardStyle = p.optInt("beard_style", 0),
                        eyeColor = p.optInt("eye_color", 0),
                        photoUrl = p.optString("photo_url", "")
                    )
                )
            }
            FormationResponse(
                teamName = obj.optString("team_name", ""),
                shortName = obj.optString("short_name", null),
                colorPrimary = obj.optString("color_primary", null),
                colorSecondary = obj.optString("color_secondary", null),
                league = obj.optString("league", null),
                players = players
            )
        } else {
            Log.w("TeamComposition", "Catalog returned HTTP ${conn.responseCode}")
            null
        }
    } catch (e: Exception) {
        Log.e("TeamComposition", "Failed to fetch formation: ${e.message}")
        null
    }
}
