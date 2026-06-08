package com.football.ar.ui.utils

import android.content.Context
import android.content.Intent
import com.football.ar.MainActivity

fun launchGameActivity(
    context: Context,
    mode: String,
    roomId: String? = null,
    token: String? = null,
    livekitUrl: String? = null,
    forceClassic: Boolean = false,
    forceMcoVsMca: Boolean = false,
    teamA: String? = null,
    teamB: String? = null
) {
    val intent = Intent(context, MainActivity::class.java).apply {
        putExtra("mode", mode)
        putExtra("force_classic", forceClassic)
        putExtra("force_mco_vs_mca", forceMcoVsMca)
        roomId?.let { putExtra("room_id", it) }
        token?.let { putExtra("token", it) }
        livekitUrl?.let { putExtra("livekit_url", it) }
        teamA?.let { putExtra("team_a", it) }
        teamB?.let { putExtra("team_b", it) }
    }
    context.startActivity(intent)
}
