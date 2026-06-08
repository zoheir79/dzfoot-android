package com.football.ar.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.football.ar.ui.theme.*

@Composable
fun PitchBackground(modifier: Modifier = Modifier) {
    Box(modifier = modifier) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    brush = Brush.verticalGradient(
                        colors = listOf(PitchGreenDark, PitchGreen, PitchGreenDark)
                    )
                )
        )
        repeat(12) { i ->
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(40.dp)
                    .offset(y = (i * 65).dp)
                    .background(
                        if (i % 2 == 0) GrassStripeLight.copy(alpha = 0.15f)
                        else GrassStripeDark.copy(alpha = 0.08f)
                    )
            )
        }
        Box(
            modifier = Modifier
                .size(200.dp)
                .align(Alignment.Center)
                .border(
                    width = 2.dp,
                    color = AccentGreen.copy(alpha = 0.2f),
                    shape = CircleShape
                )
        )
    }
}

@Composable
fun MenuButton(
    icon: ImageVector,
    text: String,
    subtext: String,
    onClick: () -> Unit
) {
    var pressed by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(
        targetValue = if (pressed) 0.96f else 1f,
        animationSpec = tween(100),
        label = "press"
    )

    Button(
        onClick = {
            pressed = true
            onClick()
        },
        modifier = Modifier
            .fillMaxWidth()
            .height(80.dp)
            .scale(scale)
            .shadow(
                elevation = 12.dp,
                shape = RoundedCornerShape(20.dp),
                ambientColor = AccentGreen.copy(alpha = 0.3f)
            ),
        shape = RoundedCornerShape(20.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = SurfaceElevated.copy(alpha = 0.85f)
        ),
        border = BorderStroke(1.5.dp, AccentGreen.copy(alpha = 0.5f))
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .clip(CircleShape)
                    .background(AccentGreen.copy(alpha = 0.2f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = AccentGreen,
                    modifier = Modifier.size(28.dp)
                )
            }
            Spacer(modifier = Modifier.width(20.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = text,
                    style = MaterialTheme.typography.titleLarge,
                    color = TextPrimary
                )
                Text(
                    text = subtext,
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondary
                )
            }
            Icon(
                imageVector = Icons.Filled.ChevronRight,
                contentDescription = null,
                tint = AccentGold,
                modifier = Modifier.size(28.dp)
            )
        }
    }
}

@Composable
fun ModeCard(
    title: String,
    subtitle: String,
    icon: ImageVector,
    description: String,
    color: Color,
    onClick: () -> Unit
) {
    var pressed by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(
        targetValue = if (pressed) 0.97f else 1f,
        animationSpec = tween(100),
        label = "press"
    )

    Card(
        onClick = {
            pressed = true
            onClick()
        },
        modifier = Modifier
            .fillMaxWidth()
            .scale(scale)
            .shadow(
                elevation = 8.dp,
                shape = RoundedCornerShape(20.dp),
                ambientColor = color.copy(alpha = 0.2f)
            ),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(
            containerColor = SurfaceElevated.copy(alpha = 0.9f)
        ),
        border = BorderStroke(2.dp, color.copy(alpha = 0.6f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(56.dp)
                    .clip(CircleShape)
                    .background(color.copy(alpha = 0.2f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = color,
                    modifier = Modifier.size(32.dp)
                )
            }
            Spacer(modifier = Modifier.width(16.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleLarge,
                    color = TextPrimary
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.labelLarge,
                    color = color.copy(alpha = 0.9f)
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondary
                )
            }
            Icon(
                imageVector = Icons.Filled.PlayArrow,
                contentDescription = null,
                tint = color,
                modifier = Modifier.size(32.dp)
            )
        }
    }
}

@Composable
fun ScreenHeader(title: String, onBack: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconButton(onClick = onBack) {
            Icon(
                imageVector = Icons.Filled.ArrowBack,
                contentDescription = "Retour",
                tint = TextPrimary,
                modifier = Modifier.size(32.dp)
            )
        }
        Text(
            text = title,
            style = MaterialTheme.typography.headlineMedium,
            color = TextPrimary,
            modifier = Modifier.weight(1f),
            textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.width(48.dp))
    }
}

@Composable
fun TeamCard(
    team: TeamInfo,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    val borderColor = if (isSelected) AccentGold else AccentGreen.copy(alpha = 0.3f)
    val bgColor = if (isSelected) SurfaceElevated.copy(alpha = 0.95f) else SurfaceElevated.copy(alpha = 0.7f)

    val primaryColor = try {
        team.colorPrimary?.let { Color(android.graphics.Color.parseColor(it)) } ?: AccentGreen
    } catch (_: Exception) { AccentGreen }

    Card(
        onClick = onClick,
        modifier = Modifier
            .fillMaxWidth()
            .height(110.dp)
            .shadow(4.dp, RoundedCornerShape(16.dp)),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = bgColor),
        border = BorderStroke(if (isSelected) 3.dp else 1.dp, borderColor)
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(56.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(primaryColor.copy(alpha = 0.25f))
                    .border(2.dp, primaryColor.copy(alpha = 0.6f), RoundedCornerShape(14.dp)),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = team.shortName.take(2).uppercase(),
                    color = primaryColor,
                    style = MaterialTheme.typography.headlineSmall
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = team.name,
                    style = MaterialTheme.typography.titleMedium,
                    color = TextPrimary,
                    maxLines = 1
                )
                Text(
                    text = listOfNotNull(team.city, team.league).joinToString(" • "),
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondary
                )
                if (team.country != null) {
                    Text(
                        text = team.country,
                        style = MaterialTheme.typography.labelMedium,
                        color = TextSecondary.copy(alpha = 0.7f)
                    )
                }
            }
            if (isSelected) {
                Icon(
                    imageVector = Icons.Filled.CheckCircle,
                    contentDescription = "Selected",
                    tint = AccentGold,
                    modifier = Modifier.size(28.dp)
                )
            }
        }
    }
}

@Composable
fun MatchPreviewBar(teamA: TeamInfo?, teamB: TeamInfo?) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .height(70.dp),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = SurfaceElevated.copy(alpha = 0.9f)),
        border = BorderStroke(1.5.dp, AccentGold.copy(alpha = 0.5f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            TeamPreview(team = teamA, isHome = true)
            Text(
                text = "VS",
                style = MaterialTheme.typography.headlineMedium,
                color = AccentGold,
                modifier = Modifier.padding(horizontal = 16.dp)
            )
            TeamPreview(team = teamB, isHome = false)
        }
    }
}

@Composable
fun TeamPreview(team: TeamInfo?, isHome: Boolean) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(
                    if (team != null) AccentGreen.copy(alpha = 0.3f)
                    else TextSecondary.copy(alpha = 0.2f)
                ),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = team?.shortName?.take(1) ?: "?",
                color = if (team != null) TextPrimary else TextSecondary,
                style = MaterialTheme.typography.titleMedium
            )
        }
        Text(
            text = team?.shortName ?: if (isHome) "HOME" else "AWAY",
            style = MaterialTheme.typography.labelMedium,
            color = if (team != null) TextPrimary else TextSecondary
        )
    }
}

data class TeamInfo(
    val id: String,
    val name: String,
    val shortName: String,
    val city: String? = null,
    val country: String? = null,
    val logoUrl: String? = null,
    val colorPrimary: String? = null,
    val colorSecondary: String? = null,
    val league: String? = null
)

data class MatchListing(
    val roomId: String,
    val playerA: String,
    val teamA: String?,
    val teamB: String?,
    val duration: Int?
)

data class PlayerInfo(
    val name: String,
    val position: String,
    val number: Int = 0,
    val skills: Map<String, Float> = emptyMap(),
    val skinColor: Int = 3,
    val hairStyle: String = "short",
    val hairColor: String = "black",
    val height: Float = 1.78f,
    val bodyType: Int = 1,
    val beardStyle: Int = 0,
    val eyeColor: Int = 0,
    val photoUrl: String = ""
) {
    fun stat(key: String): Float = skills[key] ?: 0.7f
    fun formattedStat(key: String): String = "${(stat(key) * 100).toInt()}"
}

data class FormationResponse(
    val teamName: String,
    val shortName: String? = null,
    val formation: List<FormationEntry> = emptyList(),
    val players: List<PlayerInfo> = emptyList(),
    val colorPrimary: String? = null,
    val colorSecondary: String? = null,
    val league: String? = null
)

data class FormationEntry(
    val role: String,
    val x: Float = 0f,
    val y: Float = 0f,
    val controllable: Boolean = false
)
