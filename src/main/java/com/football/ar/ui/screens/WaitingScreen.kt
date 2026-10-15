package com.football.ar.ui.screens

import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.football.ar.Config
import com.football.ar.Screen
import com.football.ar.ui.components.MatchListing
import com.football.ar.ui.components.PitchBackground
import com.football.ar.ui.theme.*
import com.football.ar.ui.utils.launchGameActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

private fun normalizeLkUrl(rawLkUrl: String): String {
    return when {
        rawLkUrl.startsWith("wss://") || rawLkUrl.startsWith("ws://") -> rawLkUrl
        rawLkUrl.startsWith("https://") -> rawLkUrl.replace("https://", "wss://")
        rawLkUrl.startsWith("http://") -> rawLkUrl.replace("http://", "ws://")
        else -> "wss://$rawLkUrl"
    }
}

@Composable
fun WaitingPvPScreen(
    navController: NavHostController,
    teamA: String,
    teamB: String,
    isHost: Boolean
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val isJoinFlow = teamB.startsWith("join_")

    var statusText by remember {
        mutableStateOf(if (isJoinFlow) "Connexion au salon PvP..." else "Creation du salon PvP...")
    }
    var subStatusText by remember {
        mutableStateOf(
            if (isJoinFlow) "Preparation de ton equipe et lancement du serveur..."
            else "Generation du Code Match en cours..."
        )
    }
    var errorText by remember { mutableStateOf<String?>(null) }
    var dots by remember { mutableStateOf(0) }
    var roomId by remember { mutableStateOf<String?>(null) }
    var matchCode by remember { mutableStateOf<String?>(null) }
    var launched by remember { mutableStateOf(false) }

    val infiniteTransition = rememberInfiniteTransition(label = "ball")
    val bounce by infiniteTransition.animateFloat(
        initialValue = -16f,
        targetValue = 16f,
        animationSpec = infiniteRepeatable(
            animation = tween(800, easing = EaseInOutSine),
            repeatMode = RepeatMode.Reverse
        ),
        label = "bounce"
    )

    LaunchedEffect(Unit) {
        while (!launched) {
            delay(500)
            dots = (dots + 1) % 4
        }
    }

    LaunchedEffect(teamA, teamB) {
        if (isJoinFlow) {
            // Format: "join_<roomIdOrCode>_<chosenTeamB>"
            val raw = teamB.removePrefix("join_")
            val lastUnderscore = raw.lastIndexOf('_')
            val target = if (lastUnderscore > 0) raw.substring(0, lastUnderscore) else raw
            val chosenTeamB = if (lastUnderscore > 0) raw.substring(lastUnderscore + 1) else "mco"

            val joinResult = withContext(Dispatchers.IO) {
                try {
                    val playerB = UUID.randomUUID().toString()
                    val url = URL("${Config.sessionUrl}/internal/join-match")
                    val conn = (url.openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        setRequestProperty("Content-Type", "application/json")
                        doOutput = true
                        connectTimeout = 10000
                        readTimeout = 10000
                    }
                    val bodyObj = JSONObject().apply {
                        if (target.startsWith("match-")) {
                            put("room_id", target)
                        } else {
                            put("match_code", target.uppercase())
                        }
                        put("player_id", playerB)
                        put("team_b", chosenTeamB)
                    }
                    conn.outputStream.use { it.write(bodyObj.toString().toByteArray(Charsets.UTF_8)) }
                    val code = conn.responseCode
                    if (code == 200) {
                        val resp = conn.inputStream.bufferedReader().use { it.readText() }
                        Result.success(JSONObject(resp))
                    } else {
                        val err = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: "HTTP $code"
                        Result.failure(Exception("Code ou salon introuvable ($err)"))
                    }
                } catch (e: Exception) {
                    Result.failure(e)
                }
            }

            joinResult.onSuccess { json ->
                val resolvedRoomId = json.getString("room_id")
                val lkUrl = normalizeLkUrl(json.getString("livekit_url"))
                val token = json.getString("token")
                val realTeamA = json.optString("team_a", teamA)
                val realTeamB = json.optString("team_b", chosenTeamB)
                roomId = resolvedRoomId
                statusText = "Match rejoint !"
                subStatusText = "${realTeamA.uppercase()} vs ${realTeamB.uppercase()} — Demarrage..."
                launched = true
                delay(400)
                launchGameActivity(
                    context = context,
                    mode = "1v1",
                    roomId = resolvedRoomId,
                    token = token,
                    livekitUrl = lkUrl,
                    forceClassic = true,
                    teamA = realTeamA,
                    teamB = realTeamB,
                    myTeam = 1
                )
            }.onFailure { err ->
                errorText = err.message ?: "Impossible de rejoindre ce match"
                statusText = "Echec de connexion"
            }
        } else {
            // Host flow: create waiting match with Creator's chosen teamA only
            val createResult = withContext(Dispatchers.IO) {
                try {
                    val playerA = UUID.randomUUID().toString()
                    val url = URL("${Config.sessionUrl}/internal/create-match")
                    val conn = (url.openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        setRequestProperty("Content-Type", "application/json")
                        doOutput = true
                        connectTimeout = 10000
                        readTimeout = 10000
                    }
                    val bodyObj = JSONObject().apply {
                        put("player_a", playerA)
                        put("duration", 300)
                        put("mode", "1v1")
                        put("team_a", teamA)
                    }
                    conn.outputStream.use { it.write(bodyObj.toString().toByteArray(Charsets.UTF_8)) }
                    val code = conn.responseCode
                    if (code == 200) {
                        val resp = conn.inputStream.bufferedReader().use { it.readText() }
                        Result.success(JSONObject(resp))
                    } else {
                        Result.failure(Exception("HTTP $code"))
                    }
                } catch (e: Exception) {
                    Result.failure(e)
                }
            }

            createResult.onSuccess { json ->
                val createdRoomId = json.getString("room_id")
                val createdCode = json.optString("match_code", createdRoomId.takeLast(5).uppercase())
                val lkUrl = normalizeLkUrl(json.getString("livekit_url"))
                val hostToken = json.getString("token")
                roomId = createdRoomId
                matchCode = createdCode
                statusText = "En attente d'un adversaire"
                subStatusText = "Donne ce Code Match a ton ami ou attends qu'il rejoigne depuis la liste des salons."

                // Poll match status every 1.5s until Player B joins (status == "running")
                while (!launched) {
                    delay(1500)
                    val poll = withContext(Dispatchers.IO) {
                        try {
                            val stUrl = URL("${Config.sessionUrl}/internal/match-status/$createdRoomId")
                            val stConn = (stUrl.openConnection() as HttpURLConnection).apply {
                                requestMethod = "GET"
                                connectTimeout = 5000
                                readTimeout = 5000
                            }
                            if (stConn.responseCode == 200) {
                                JSONObject(stConn.inputStream.bufferedReader().use { it.readText() })
                            } else null
                        } catch (_: Exception) {
                            null
                        }
                    }
                    if (poll != null && poll.optString("status") == "running") {
                        val joinedTeamB = poll.optString("team_b", "mco")
                        val joinedTeamBName = poll.optString("team_b_name", joinedTeamB.uppercase())
                        statusText = "Adversaire connecte !"
                        subStatusText = "Adversaire : $joinedTeamBName — Lancement du match..."
                        launched = true
                        delay(500)
                        launchGameActivity(
                            context = context,
                            mode = "1v1",
                            roomId = createdRoomId,
                            token = hostToken,
                            livekitUrl = lkUrl,
                            forceClassic = true,
                            teamA = teamA,
                            teamB = joinedTeamB,
                            myTeam = 0
                        )
                        navController.popBackStack(Screen.ModeSelect.route, inclusive = false)
                        break
                    }
                }
            }.onFailure { err ->
                errorText = "Erreur creation salon PvP: ${err.message}"
                statusText = "Erreur serveur"
            }
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Box(
                modifier = Modifier
                    .offset(y = bounce.dp)
                    .size(76.dp)
                    .clip(CircleShape)
                    .background(
                        brush = Brush.radialGradient(
                            colors = listOf(AccentGold, AccentGoldDark)
                        )
                    )
                    .border(3.dp, TextPrimary.copy(alpha = 0.5f), CircleShape),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "1v1",
                    style = MaterialTheme.typography.headlineMedium,
                    color = TextPrimary
                )
            }

            Spacer(modifier = Modifier.height(32.dp))

            Text(
                text = if (errorText != null) statusText else statusText + ".".repeat(dots),
                style = MaterialTheme.typography.headlineMedium,
                color = if (errorText != null) AlgeriaRed else TextPrimary,
                textAlign = TextAlign.Center
            )

            Spacer(modifier = Modifier.height(12.dp))

            Text(
                text = errorText ?: subStatusText,
                style = MaterialTheme.typography.bodyLarge,
                color = if (errorText != null) AlgeriaRed else TextSecondary,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 24.dp)
            )

            if (matchCode != null) {
                Spacer(modifier = Modifier.height(24.dp))
                Card(
                    shape = RoundedCornerShape(18.dp),
                    colors = CardDefaults.cardColors(containerColor = SurfaceElevated),
                    border = BorderStroke(2.5.dp, AccentGold)
                ) {
                    Column(
                        modifier = Modifier.padding(horizontal = 32.dp, vertical = 16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "CODE MATCH",
                            style = MaterialTheme.typography.labelLarge,
                            color = TextSecondary,
                            letterSpacing = 3.sp
                        )
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = matchCode!!,
                            fontSize = 34.sp,
                            fontWeight = FontWeight.ExtraBold,
                            letterSpacing = 6.sp,
                            color = AccentGold
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = "Ton equipe (Domicile) : ${teamA.uppercase()}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = AccentGreen
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(36.dp))

            Button(
                onClick = {
                    val rId = roomId
                    if (!isJoinFlow && rId != null) {
                        scope.launch(Dispatchers.IO) {
                            try {
                                val u = URL("${Config.sessionUrl}/internal/cancel-match/$rId")
                                (u.openConnection() as HttpURLConnection).apply {
                                    requestMethod = "POST"
                                    connectTimeout = 3000
                                    responseCode
                                }
                            } catch (_: Exception) {}
                        }
                    }
                    navController.popBackStack()
                },
                colors = ButtonDefaults.buttonColors(containerColor = AlgeriaRed.copy(alpha = 0.85f)),
                shape = RoundedCornerShape(16.dp)
            ) {
                Icon(Icons.Filled.Close, contentDescription = null, tint = TextPrimary)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Annuler", color = TextPrimary)
            }
        }
    }
}

private suspend fun fetchWaitingMatchesFromServer(): List<MatchListing> {
    return withContext(Dispatchers.IO) {
        try {
            val url = URL("${Config.sessionUrl}/internal/waiting-matches")
            val conn = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                connectTimeout = 5000
                readTimeout = 5000
            }
            if (conn.responseCode == 200) {
                val resp = conn.inputStream.bufferedReader().use { it.readText() }
                val root = JSONObject(resp)
                val arr = root.optJSONArray("matches") ?: return@withContext emptyList()
                val list = mutableListOf<MatchListing>()
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    val rId = o.optString("room_id", "")
                    val mCode = o.optString("match_code", "").ifBlank { rId.takeLast(5).uppercase() }
                    list.add(
                        MatchListing(
                            roomId = rId,
                            matchCode = mCode,
                            playerA = o.optString("player_a", "Joueur"),
                            teamA = o.optString("team_a", null),
                            teamAName = o.optString("team_a_name", null),
                            teamB = o.optString("team_b", null),
                            duration = o.optInt("duration", 300)
                        )
                    )
                }
                list
            } else {
                emptyList()
            }
        } catch (_: Exception) {
            emptyList()
        }
    }
}

@Composable
fun WaitingJoinScreen(navController: NavHostController) {
    val scope = rememberCoroutineScope()
    var isLoading by remember { mutableStateOf(true) }
    var matches by remember { mutableStateOf<List<MatchListing>>(emptyList()) }
    var codeInput by remember { mutableStateOf("") }
    var codeError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        while (true) {
            matches = fetchWaitingMatchesFromServer()
            isLoading = false
            delay(3000)
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        PitchBackground()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(20.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = { navController.popBackStack() }) {
                    Icon(
                        imageVector = Icons.Filled.ArrowBack,
                        contentDescription = "Retour",
                        tint = TextPrimary,
                        modifier = Modifier.size(32.dp)
                    )
                }
                Text(
                    text = "REJOINDRE UN MATCH 1v1",
                    style = MaterialTheme.typography.headlineMedium,
                    color = TextPrimary,
                    modifier = Modifier.weight(1f),
                    textAlign = TextAlign.Center
                )
                IconButton(onClick = {
                    scope.launch {
                        isLoading = true
                        matches = fetchWaitingMatchesFromServer()
                        isLoading = false
                    }
                }) {
                    Icon(
                        imageVector = Icons.Filled.Refresh,
                        contentDescription = "Actualiser",
                        tint = AccentGold,
                        modifier = Modifier.size(28.dp)
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Direct Match Code Entry Box
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = SurfaceElevated.copy(alpha = 0.92f)),
                border = BorderStroke(1.5.dp, AccentGold.copy(alpha = 0.6f))
            ) {
                Column(modifier = Modifier.padding(14.dp)) {
                    Text(
                        text = "REJOINDRE AVEC UN CODE MATCH",
                        style = MaterialTheme.typography.labelLarge,
                        color = AccentGold
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedTextField(
                            value = codeInput,
                            onValueChange = {
                                codeInput = it.uppercase().take(12)
                                codeError = null
                            },
                            placeholder = { Text("Ex: K7M4P", color = TextSecondary.copy(alpha = 0.6f)) },
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = AccentGold,
                                unfocusedBorderColor = TextSecondary.copy(alpha = 0.4f),
                                focusedTextColor = TextPrimary,
                                unfocusedTextColor = TextPrimary
                            )
                        )
                        Spacer(modifier = Modifier.width(12.dp))
                        Button(
                            onClick = {
                                val cleaned = codeInput.trim().uppercase()
                                if (cleaned.length < 3) {
                                    codeError = "Entre un code valide"
                                } else {
                                    val found = matches.find {
                                        it.matchCode.equals(cleaned, ignoreCase = true) ||
                                                it.roomId.uppercase().endsWith(cleaned)
                                    }
                                    val target = found?.roomId ?: cleaned
                                    val hostTeam = found?.teamA ?: "DOM"
                                    navController.navigate(
                                        Screen.TeamSelect.createRoute("1v1_join:$target:$hostTeam")
                                    )
                                }
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = AccentGold),
                            shape = RoundedCornerShape(12.dp),
                            modifier = Modifier.height(54.dp)
                        ) {
                            Text("Choisir Equipe", color = PitchGreenDark, fontWeight = FontWeight.Bold)
                        }
                    }
                    if (codeError != null) {
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(text = codeError!!, color = AlgeriaRed, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            Text(
                text = "SALONS EN ATTENTE (${matches.size})",
                style = MaterialTheme.typography.labelLarge,
                color = TextSecondary
            )

            Spacer(modifier = Modifier.height(8.dp))

            if (isLoading && matches.isEmpty()) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = AccentGreen, modifier = Modifier.size(56.dp))
                }
            } else if (matches.isEmpty()) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            Icons.Filled.Search,
                            contentDescription = null,
                            tint = TextSecondary,
                            modifier = Modifier.size(56.dp)
                        )
                        Spacer(modifier = Modifier.height(12.dp))
                        Text(
                            text = "Aucun match en attente",
                            color = TextSecondary,
                            style = MaterialTheme.typography.headlineSmall
                        )
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = "Entre un Code Match ci-dessus ou cree ton propre salon 1v1 !",
                            color = TextSecondary.copy(alpha = 0.7f),
                            style = MaterialTheme.typography.bodyMedium,
                            textAlign = TextAlign.Center
                        )
                    }
                }
            } else {
                LazyVerticalGrid(
                    columns = GridCells.Fixed(1),
                    modifier = Modifier.weight(1f),
                    contentPadding = PaddingValues(4.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    items(matches) { match ->
                        MatchJoinCard(match = match, onClick = {
                            val hostTeam = match.teamA ?: "DOM"
                            navController.navigate(
                                Screen.TeamSelect.createRoute("1v1_join:${match.roomId}:$hostTeam")
                            )
                        })
                    }
                }
            }
        }
    }
}

@Composable
fun MatchJoinCard(match: MatchListing, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        modifier = Modifier
            .fillMaxWidth()
            .height(94.dp)
            .shadow(6.dp, RoundedCornerShape(16.dp)),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = SurfaceElevated.copy(alpha = 0.92f)),
        border = BorderStroke(1.5.dp, AccentGreen.copy(alpha = 0.5f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(52.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(AccentGold.copy(alpha = 0.2f))
                    .border(1.5.dp, AccentGold, RoundedCornerShape(12.dp)),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = match.matchCode.ifBlank { "1v1" },
                    color = AccentGold,
                    fontWeight = FontWeight.ExtraBold,
                    fontSize = 13.sp
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "Code : ${match.matchCode} • Domicile : ${match.teamAName ?: match.teamA?.uppercase() ?: "?"}",
                    style = MaterialTheme.typography.titleMedium,
                    color = TextPrimary
                )
                Text(
                    text = "Duree : ${(match.duration ?: 300) / 60} min • En attente d'adversaire",
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondary
                )
            }
            Button(
                onClick = onClick,
                colors = ButtonDefaults.buttonColors(containerColor = AccentGreen),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("Rejoindre", color = TextPrimary, fontWeight = FontWeight.Bold)
            }
        }
    }
}


