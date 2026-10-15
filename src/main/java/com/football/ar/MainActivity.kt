package com.football.ar

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Vibrator
import android.os.VibrationEffect
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import org.godotengine.godot.Godot
import org.godotengine.godot.GodotHost
import org.godotengine.godot.plugin.GodotPlugin

class MainActivity : AppCompatActivity(), GodotHost {

    lateinit var godotAdapter: GodotAdapter
    private var godotView: View? = null
    val jni = JniBridge()
    private lateinit var lkManager: LiveKitManager
    val audioSystem = AudioSystem()

    private var latestGameState = ByteArray(0)

    private lateinit var scoreText: TextView
    private lateinit var timerText: TextView
    private lateinit var eventText: TextView
    private lateinit var loadingContainer: FrameLayout
    private lateinit var loadingProgressBar: android.widget.ProgressBar
    private lateinit var loadingStatusText: TextView
    private val loadingHandler = Handler(Looper.getMainLooper())
    private val loadingRunnable = object : Runnable {
        override fun run() {
            updateLoadingOverlay()
            if (loadingContainer.visibility == View.VISIBLE) {
                loadingHandler.postDelayed(this, 100)
            }
        }
    }
    private var loadingStartTime = 0L
    private val LOADING_TIMEOUT_MS = 45000L
    private val GAMESTATE_TIMEOUT_MS = 12000L
    private val SERVER_TIMEOUT_MS = 120000L  // GF container spawn can take ~2min
    private var retryCount = 0
    private val MAX_RETRIES = 3
    private var firstGameStateReceived = false
    private var firstSetupReceived = false

    // Match-end tracking: the GF server pushes EVENT_END_MATCH then stops the
    // gamestate stream. If the event packet is lost (or the container dies),
    // the watchdog below detects the dead stream and ends the match anyway.
    @Volatile private var matchEnded = false
    @Volatile private var lastGameStateAt = 0L
    @Volatile private var lastScoreA = 0
    @Volatile private var lastScoreB = 0
    private val GAMESTATE_DEAD_MS = 20000L
    private val endWatchdog = object : Runnable {
        override fun run() {
            if (!matchEnded && firstGameStateReceived && lastGameStateAt > 0 &&
                System.currentTimeMillis() - lastGameStateAt > GAMESTATE_DEAD_MS) {
                Log.w("DZ_END", "No gamestate for >${GAMESTATE_DEAD_MS}ms — stream dead, ending match")
                onEndMatch(lastScoreA, lastScoreB)
            }
            if (!matchEnded) loadingHandler.postDelayed(this, 5000)
        }
    }

    // Retry state for the current connection
    private var lastLkUrl: String? = null
    private var lastToken: String? = null
    private var lastRoomId: String? = null
    private var lastMode: String? = null
    @Volatile private var expectingServer = false  // set as soon as a real match is requested
    private var lastForceMcoVsMca: Boolean = false
    private var lastTeamA: String? = null
    private var lastTeamB: String? = null
    private var retryDialogShown = false

    // Dynamic crowd volume: very light background murmur (0.045f) during normal play,
    // only rising noticeably on GOALS (0.18f, -50% vs previous levels) before returning to 0.045f.
    private val crowdBaseline = 0.045f
    private var crowdVolumeCurrent = 0.0f
    private var crowdVolumeTarget = 0.045f
    private var inPenaltyBox = false
    private val prevPlayerFuncType = IntArray(22) { -1 }
    private var lastBallTapSoundAt = 0L
    private var lastStateLogAt = 0L
    private var prevUiScoreA = -1
    private var prevUiScoreB = -1
    private var prevUiMinutes = -1
    private var prevUiSeconds = -1
    private val crowdHandler = Handler(Looper.getMainLooper())
    private val crowdRunnable = object : Runnable {
        override fun run() {
            if (kotlin.math.abs(crowdVolumeCurrent - crowdVolumeTarget) > 0.003f) {
                crowdVolumeCurrent += (crowdVolumeTarget - crowdVolumeCurrent) * 0.14f
                if (crowdVolumeCurrent < 0.006f && crowdVolumeTarget == 0.0f) {
                    crowdVolumeCurrent = 0.0f
                }
                audioSystem.setLoopVolume(crowdVolumeCurrent)
            }
            crowdHandler.postDelayed(this, 100)
        }
    }

    // 100 Hz input loop — decoupled from render frame rate to match server tick rate
    private val inputExecutor = java.util.concurrent.Executors.newSingleThreadScheduledExecutor()
    private var inputFuture: java.util.concurrent.ScheduledFuture<*>? = null
    private val inputLock = Object()

    private fun startInputLoop() {
        synchronized(inputLock) {
            if (inputFuture != null) return
            Log.i("DZ_INPUT", "START_INPUT_LOOP executor started")
            inputFuture = inputExecutor.scheduleAtFixedRate({
                try {
                    sendInput(force = true, fromExecutor = true)
                } catch (e: Exception) {
                    Log.e("DZ_INPUT", "EXECUTOR sendInput crashed: ${e.message}")
                }
            }, 0, 10, java.util.concurrent.TimeUnit.MILLISECONDS)
        }
    }

    private fun stopInputLoop() {
        synchronized(inputLock) {
            inputFuture?.cancel(false)
            inputFuture = null
        }
    }

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        Log.i("MainActivity_V9", "!!! MAIN ACTIVITY ONCREATE V9 !!!")

        // Initialize Godot engine as the 3D renderer
        godotAdapter = GodotAdapter(this)
        godotView = godotAdapter.initialize()
        Toast.makeText(this, "Godot engine initialized", Toast.LENGTH_LONG).show()

        audioSystem.init()
        Thread { loadSounds() }.start()
        crowdHandler.post(crowdRunnable)

        scoreText = TextView(this).apply {
            textSize = 24f
            setTextColor(0xFFFFFFFF.toInt())
            text = "0 - 0"
            visibility = View.INVISIBLE
        }
        timerText = TextView(this).apply {
            textSize = 20f
            setTextColor(0xFFFFFFFF.toInt())
            text = "00:00"
            visibility = View.INVISIBLE
        }
        eventText = TextView(this).apply {
            textSize = 18f
            setTextColor(0xFFFFFF00.toInt())
            text = ""
        }

        loadingContainer = FrameLayout(this).apply {
            setBackgroundColor(0xFF000000.toInt())
            visibility = View.VISIBLE
        }
        loadingProgressBar = android.widget.ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = false
            max = 100
            progress = 0
        }
        loadingStatusText = TextView(this).apply {
            textSize = 18f
            setTextColor(0xFFFFFFFF.toInt())
            text = "Initialisation..."
            textAlignment = View.TEXT_ALIGNMENT_CENTER
        }
        loadingContainer.addView(loadingProgressBar, FrameLayout.LayoutParams(600, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
            gravity = android.view.Gravity.CENTER
        })
        loadingContainer.addView(loadingStatusText, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = android.view.Gravity.CENTER
            topMargin = 80
        })

        val root = FrameLayout(this).apply {
            if (godotView != null) {
                addView(godotView, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            }

            // Score overlay (top center)
            addView(scoreText, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = android.view.Gravity.TOP or android.view.Gravity.CENTER_HORIZONTAL
                topMargin = 40
            })

            // Timer overlay (top right)
            addView(timerText, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = android.view.Gravity.TOP or android.view.Gravity.END
                topMargin = 40
                marginEnd = 40
            })

            // Event overlay (center)
            addView(eventText, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = android.view.Gravity.CENTER
            })

            // Loading overlay (covers whole screen until renderer + first game state)
            addView(loadingContainer, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)

            // On-screen controls are rendered by Godot (touch input via GDScript)
        }
        setContentView(root)

        // A match can run for minutes without touch input — never let the
        // display sleep (screen-off would pause GodotFragment mid-match and
        // silently freeze inputs + LiveKit callbacks).
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        loadingStartTime = System.currentTimeMillis()
        loadingHandler.post(loadingRunnable)

        if (checkSelfPermission(android.Manifest.permission.CAMERA) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(android.Manifest.permission.CAMERA), 123)
        }

        // Check if launched from MenuActivity with parameters
        val mode = intent.getStringExtra("mode")
        val roomId = intent.getStringExtra("room_id")
        val token = intent.getStringExtra("token")
        val lkUrl = intent.getStringExtra("livekit_url")
        val forceClassic = intent.getBooleanExtra("force_classic", false)
        val forceMcoVsMca = intent.getBooleanExtra("force_mco_vs_mca", false)
        val teamA = intent.getStringExtra("team_a")
        val teamB = intent.getStringExtra("team_b")
        val myTeam = intent.getIntExtra("my_team", 0)
        godotAdapter.setMyTeam(myTeam)
expectingServer = mode != null
        lastTeamA = teamA
        lastTeamB = teamB
        
        if (mode != null && roomId != null && token != null && lkUrl != null) {
            // Launched from menu with all parameters — connect directly
            connectToLiveKit(lkUrl, token, roomId, mode)
        } else if (mode != null) {
            // Single-player modes that need match creation
            when (mode) {
                "ai_vs_ai" -> createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = forceMcoVsMca)
                "vs_ai", "ar" -> createMatchAndConnect(mode = if (mode == "ar") "vs_ai" else mode, forceMcoVsMca = false, selectedTeamA = teamA, selectedTeamB = teamB)
                else -> showModeSelectionDialogFallback()
            }
        } else {
            // Direct launch without parameters — show fallback dialog
            showModeSelectionDialogFallback()
        }
    }

    private fun connectToLiveKit(lkUrl: String, token: String, roomId: String, mode: String, forceMcoVsMca: Boolean = false) {
        lastLkUrl = lkUrl
        lastToken = token
        lastRoomId = roomId
        lastMode = mode
        lastForceMcoVsMca = forceMcoVsMca
        retryCount = 0
        retryDialogShown = false
        lkManager = LiveKitManager(this)
        lkManager.connect(lkUrl, token)
        startInputLoop()
    }

    private fun showModeSelectionDialogFallback() {
        val modes = arrayOf(
            "Réalité Augmentée (AR) vs IA",
            "Rendu 3D Classique vs IA",
            "Test IA vs IA (MCO vs MCA)",
            "Créer Match vs Joueur (PvP)",
            "Rejoindre un Match (PvP)"
        )
        android.app.AlertDialog.Builder(this)
            .setTitle("Sélection du Mode de Jeu")
            .setItems(modes) { _, which ->
                when (which) {
                    0 -> createMatchAndConnect(mode = "vs_ai")
                    1 -> createMatchAndConnect(mode = "vs_ai")
                    2 -> createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = true)
                    3 -> createMatchPvP()
                    4 -> joinMatch()
                }
            }
            .setCancelable(false)
            .show()
    }

    private fun createMatchAndConnect(
        mode: String = "vs_ai",
        forceMcoVsMca: Boolean = false,
        selectedTeamA: String? = null,
        selectedTeamB: String? = null
    ) {
        Thread {
            try {
                // 1. Use teams chosen in the menu, otherwise fetch the list and resolve IDs
                var teamA: String? = selectedTeamA
                var teamB: String? = selectedTeamB
                if (teamA == null || teamB == null) try {
                    val teamsUrl = java.net.URL("${Config.catalogUrl}/teams")
                    val teamsConn = teamsUrl.openConnection() as java.net.HttpURLConnection
                    teamsConn.requestMethod = "GET"
                    teamsConn.connectTimeout = 5000
                    teamsConn.readTimeout = 5000
                    teamsConn.instanceFollowRedirects = false
                    if (teamsConn.responseCode == 200) {
                        val teamsResp = teamsConn.inputStream.bufferedReader().use { it.readText() }
                        val teamsArr = org.json.JSONArray(teamsResp)
                        if (forceMcoVsMca) {
                            for (i in 0 until teamsArr.length()) {
                                val obj = teamsArr.getJSONObject(i)
                                val name = obj.optString("name", "").lowercase()
                                val shortName = obj.optString("short_name", "").lowercase()
                                if ((name.contains("oranais")) || (shortName == "mco")) {
                                    teamA = obj.getString("id")
                                    /* found MCO */
                                }
                                if ((name.contains("moulodia club d'alger")) || (shortName == "mca")) {
                                    teamB = obj.getString("id")
                                    /* found MCA */
                                }
                            }
                            if ((teamA == null) || (teamB == null)) {
                                /* MCO/MCA not found */
                            }
                        }
                        if ((teamA == null) || (teamB == null)) {
                            if (teamsArr.length() >= 2) {
                                val idxA = (0 until teamsArr.length()).random()
                                var idxB = (0 until teamsArr.length()).random()
                                while (idxB == idxA) idxB = (0 until teamsArr.length()).random()
                                teamA = teamsArr.getJSONObject(idxA).getString("id")
                                teamB = teamsArr.getJSONObject(idxB).getString("id")
                                val nameA = teamsArr.getJSONObject(idxA).optString("name", "Team A")
                                val nameB = teamsArr.getJSONObject(idxB).optString("name", "Team B")
                                /* picked random teams */
                            }
                        }
                    }
                } catch (e: Exception) {
                    /* could not fetch teams */
                }

                // 2. Create the match
                val url = java.net.URL("${Config.sessionUrl}/internal/create-match")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/json")
                conn.doOutput = true
                conn.connectTimeout = 10000
                conn.readTimeout = 10000

                val playerA = if (mode == "ai_vs_ai") "bot" else java.util.UUID.randomUUID().toString()
                val playerB = "bot"
                val bodyBuilder = StringBuilder()
                bodyBuilder.append("{\"player_a\":\"$playerA\",\"player_b\":\"$playerB\",\"duration\":300,\"mode\":\"$mode\"")
                teamA?.let { bodyBuilder.append(",\"team_a\":\"$it\"") }
                teamB?.let { bodyBuilder.append(",\"team_b\":\"$it\"") }
                bodyBuilder.append("}")
                val body = bodyBuilder.toString()
                /* create match */
                conn.outputStream.use { os ->
                    os.write(body.toByteArray(Charsets.UTF_8))
                }

                val responseCode = conn.responseCode
                if (responseCode != 200) {
                    Log.e("MainActivity", "Create match failed: HTTP $responseCode")
                    return@Thread
                }

                val response = conn.inputStream.bufferedReader().use { it.readText() }
                val json = org.json.JSONObject(response)
                val roomId = json.getString("room_id")
                val rawLkUrl = json.getString("livekit_url")
                val lkUrl = when {
                    rawLkUrl.startsWith("wss://") || rawLkUrl.startsWith("ws://") -> rawLkUrl
                    rawLkUrl.startsWith("https://") -> rawLkUrl.replace("https://", "wss://")
                    rawLkUrl.startsWith("http://") -> rawLkUrl.replace("http://", "ws://")
                    else -> "wss://$rawLkUrl"
                }
                val token = json.getString("token")

                /* match created */

                runOnUiThread {
                    connectToLiveKit(lkUrl, token, roomId, mode, forceMcoVsMca)
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "Failed to create match: ${e.message}")
            }
        }.start()
    }

    private fun createMatchPvP() {
        Thread {
            try {
                var teamA: String? = null
                var teamB: String? = null
                try {
                    val teamsUrl = java.net.URL("${Config.catalogUrl}/teams")
                    val teamsConn = teamsUrl.openConnection() as java.net.HttpURLConnection
                    teamsConn.requestMethod = "GET"
                    teamsConn.connectTimeout = 5000
                    teamsConn.readTimeout = 5000
                    teamsConn.instanceFollowRedirects = false
                    if (teamsConn.responseCode == 200) {
                        val teamsResp = teamsConn.inputStream.bufferedReader().use { it.readText() }
                        val teamsArr = org.json.JSONArray(teamsResp)
                        if (teamsArr.length() >= 2) {
                            val idxA = (0 until teamsArr.length()).random()
                            var idxB = (0 until teamsArr.length()).random()
                            while (idxB == idxA) idxB = (0 until teamsArr.length()).random()
                            teamA = teamsArr.getJSONObject(idxA).getString("id")
                            teamB = teamsArr.getJSONObject(idxB).getString("id")
                        }
                    }
                } catch (e: Exception) {
                    /* could not fetch teams */
                }

                val playerA = java.util.UUID.randomUUID().toString()
                val url = java.net.URL("${Config.sessionUrl}/internal/create-match")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/json")
                conn.doOutput = true
                conn.connectTimeout = 10000
                conn.readTimeout = 10000

                val bodyBuilder = StringBuilder()
                bodyBuilder.append("{\"player_a\":\"$playerA\",\"duration\":300,\"mode\":\"1v1\"")
                teamA?.let { bodyBuilder.append(",\"team_a\":\"$it\"") }
                teamB?.let { bodyBuilder.append(",\"team_b\":\"$it\"") }
                bodyBuilder.append("}")
                val body = bodyBuilder.toString()
                /* create PvP */
                conn.outputStream.use { os -> os.write(body.toByteArray(Charsets.UTF_8)) }

                val responseCode = conn.responseCode
                if (responseCode != 200) {
                    Log.e("MainActivity", "Create PvP match failed: HTTP $responseCode")
                    return@Thread
                }

                val response = conn.inputStream.bufferedReader().use { it.readText() }
                val json = org.json.JSONObject(response)
                val roomId = json.getString("room_id")
                val rawLkUrl = json.getString("livekit_url")
                val lkUrl = when {
                    rawLkUrl.startsWith("wss://") || rawLkUrl.startsWith("ws://") -> rawLkUrl
                    rawLkUrl.startsWith("https://") -> rawLkUrl.replace("https://", "wss://")
                    rawLkUrl.startsWith("http://") -> rawLkUrl.replace("http://", "ws://")
                    else -> "wss://$rawLkUrl"
                }
                val token = json.getString("token")
                val status = json.optString("status", "running")

                runOnUiThread {
                    connectToLiveKit(lkUrl, token, roomId, "1v1")
                    if (status == "waiting") {
                        /* PvP waiting */
                        android.widget.Toast.makeText(this, "Match créé ! En attente d'un adversaire...", android.widget.Toast.LENGTH_LONG).show()
                    } else {
                        /* PvP connected */
                    }
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "Failed to create PvP match: ${e.message}")
                runOnUiThread {
                    android.widget.Toast.makeText(this, "Erreur création match PvP", android.widget.Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private fun joinMatch() {
        Thread {
            try {
                // 1. Fetch waiting matches
                val listUrl = java.net.URL("${Config.sessionUrl}/internal/waiting-matches")
                val listConn = listUrl.openConnection() as java.net.HttpURLConnection
                listConn.requestMethod = "GET"
                listConn.connectTimeout = 10000
                listConn.readTimeout = 10000
                val listCode = listConn.responseCode
                if (listCode != 200) {
                    Log.e("MainActivity", "Failed to fetch waiting matches: HTTP $listCode")
                    runOnUiThread {
                        android.widget.Toast.makeText(this, "Aucun match disponible", android.widget.Toast.LENGTH_LONG).show()
                    }
                    return@Thread
                }
                val listResp = listConn.inputStream.bufferedReader().use { it.readText() }
                val listJson = org.json.JSONObject(listResp)
                val matchesArr = listJson.optJSONArray("matches")
                if (matchesArr == null || matchesArr.length() == 0) {
                    runOnUiThread {
                        android.widget.Toast.makeText(this, "Aucun match en attente", android.widget.Toast.LENGTH_LONG).show()
                    }
                    return@Thread
                }

                val roomIds = mutableListOf<String>()
                val labels = mutableListOf<String>()
                for (i in 0 until matchesArr.length()) {
                    val obj = matchesArr.getJSONObject(i)
                    roomIds.add(obj.getString("room_id"))
                    val playerA = obj.optString("player_a", "?")
                    labels.add("Match de $playerA")
                }

                runOnUiThread {
                    android.app.AlertDialog.Builder(this)
                        .setTitle("Rejoindre un match")
                        .setItems(labels.toTypedArray()) { _, which ->
                            val selectedRoomId = roomIds[which]
                            joinSelectedMatch(selectedRoomId)
                        }
                        .setNegativeButton("Annuler", null)
                        .show()
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "Failed to list waiting matches: ${e.message}")
                runOnUiThread {
                    android.widget.Toast.makeText(this, "Erreur liste des matches", android.widget.Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private fun joinSelectedMatch(roomId: String) {
        Thread {
            try {
                val playerB = java.util.UUID.randomUUID().toString()
                val url = java.net.URL("${Config.sessionUrl}/internal/join-match")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/json")
                conn.doOutput = true
                conn.connectTimeout = 10000
                conn.readTimeout = 10000

                val body = "{\"room_id\":\"$roomId\",\"player_id\":\"$playerB\"}"
                /* join match */
                conn.outputStream.use { os -> os.write(body.toByteArray(Charsets.UTF_8)) }

                val responseCode = conn.responseCode
                if (responseCode != 200) {
                    val err = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: "HTTP $responseCode"
                    Log.e("MainActivity", "Join match failed: $err")
                    runOnUiThread {
                        android.widget.Toast.makeText(this, "Impossible de rejoindre le match", android.widget.Toast.LENGTH_LONG).show()
                    }
                    return@Thread
                }

                val response = conn.inputStream.bufferedReader().use { it.readText() }
                val json = org.json.JSONObject(response)
                val rawLkUrl = json.getString("livekit_url")
                val lkUrl = when {
                    rawLkUrl.startsWith("wss://") || rawLkUrl.startsWith("ws://") -> rawLkUrl
                    rawLkUrl.startsWith("https://") -> rawLkUrl.replace("https://", "wss://")
                    rawLkUrl.startsWith("http://") -> rawLkUrl.replace("http://", "ws://")
                    else -> "wss://$rawLkUrl"
                }
                val token = json.getString("token")

                runOnUiThread {
                    godotAdapter.setMyTeam(1)
                    connectToLiveKit(lkUrl, token, roomId, "1v1")
                    /* joined PvP */
                    android.widget.Toast.makeText(this, "Match rejoint !", android.widget.Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "Failed to join match: ${e.message}")
                runOnUiThread {
                    android.widget.Toast.makeText(this, "Erreur connexion au match", android.widget.Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private var sendInputLogCount = 0
    private val lastSendInputMsLock = Object()
    private var lastSendInputMs = 0L
    private var lastIdleSendMs = 0L
    private var lastSentButtons = 0
    private var lastWasMoving = false
    private val soundsLock = Object()
    private var soundsLoaded = false
    private fun sendInput(force: Boolean = false, fromExecutor: Boolean = false) {
        val now = System.currentTimeMillis()
        synchronized(lastSendInputMsLock) {
            if (!force && now - lastSendInputMs < 20) return
            lastSendInputMs = now
        }
        if (::lkManager.isInitialized) {
            val inputBytes = godotAdapter.getPlayerInput()
            if (inputBytes != null && inputBytes.isNotEmpty()) {
                var isReliable = false
                if (inputBytes.size >= 28) {
                    val buf = java.nio.ByteBuffer.wrap(inputBytes).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                    val magic = buf.getInt(0)
                    val version = buf.getShort(4).toInt() and 0xFFFF
                    val dirX = buf.getFloat(12)
                    val dirZ = buf.getFloat(16)
                    val buttons = buf.getShort(20).toInt() and 0xFFFF
                    val playerIdx = buf.get(22).toInt() and 0xFF
                    val team = buf.get(23).toInt() and 0xFF
                    val isMoving = (kotlin.math.abs(dirX) > 0.01f || kotlin.math.abs(dirZ) > 0.01f)
                    val buttonsChanged = (buttons != lastSentButtons)
                    val movingChanged = (isMoving != lastWasMoving)
                    isReliable = (buttons != 0) || buttonsChanged || movingChanged
                    if (!isMoving && buttons == 0 && !buttonsChanged && !movingChanged && (now - lastIdleSendMs < 150L)) {
                        return
                    }
                    if (!isMoving && buttons == 0) {
                        lastIdleSendMs = now
                    }
                    lastSentButtons = buttons
                    lastWasMoving = isMoving
                    if (buttons != 0 || buttonsChanged || movingChanged || (isMoving && (++sendInputLogCount % 12 == 0))) {
                        val src = if (fromExecutor) "EXEC" else "UI"
                        Log.i("DZ_INPUT", "ANDROID_OUT[$src] team=$team player=$playerIdx dir=($dirX,$dirZ) buttons=0x%04X magic=0x%08X ver=$version".format(buttons, magic))
                    }
                }
                lkManager.sendInput(inputBytes, reliable = isReliable)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent?): Boolean {
        event ?: return false
        // Touch input is now handled by Godot's input system (InputEventScreenTouch / InputEventScreenDrag)
        // Godot's GodotRenderView intercepts touch events natively.
        // We still trigger input send on touch events for immediate response.
        sendInput(force = true)
        return true
    }

    fun onGameStateReceived(data: ByteArray) {
        synchronized(this) {
            latestGameState = data.copyOf()
        }
        // Forward to Godot for 3D rendering
        godotAdapter.sendGameState(data)
        if (data.size < 1256) {
            Log.w("DZ_STATE", "ANDROID_IN GameState incomplete: ${data.size} bytes (expected 1256)")
            return
        }
        // Parse binary GameStatePacket (offset 12=tick, 32=ball pos, 26=score, 28=timer)
        val bb = java.nio.ByteBuffer.wrap(data).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        val tick = bb.getInt(12)
        val gameFlags = data[25].toInt() and 0xFF
        val inPlay = (gameFlags and 1) != 0
        val scoreA = data[26].toInt()
        val scoreB = data[27].toInt()
        lastScoreA = scoreA
        lastScoreB = scoreB
        lastGameStateAt = System.currentTimeMillis()
        val timerBytes = data.sliceArray(28 until 32)
        val timer = java.nio.ByteBuffer.wrap(timerBytes).order(java.nio.ByteOrder.LITTLE_ENDIAN).float
        val minutes = (timer / 60).toInt()
        val seconds = (timer % 60).toInt()
        val ballX = bb.getFloat(32)
        val ballY = bb.getFloat(36)
        val ballZ = bb.getFloat(40)
        // Prepare crowd MediaPlayer with very light background murmur (0.045f)
        if (!firstGameStateReceived) {
            crowdVolumeTarget = crowdBaseline
            runOnUiThread {
                audioSystem.startCrowdLoopFromAssets(assets, "sounds/crowd02.ogg", crowdBaseline)
            }
        }
        // 1. Detect ball tap / kick for ANY of the 22 players (both teams):
        //    e_FunctionType: ShortPass=4 (soft), Header=7 (medium), LongPass=5 / HighPass=6 (firm), Shot=8 (strong)
        val nowMs = System.currentTimeMillis()
        for (pi in 0 until 22) {
            val baseOff = 72 + pi * 48
            val funcType = data[baseOff + 44].toInt() and 0xFF
            val prevFt = prevPlayerFuncType[pi]
            prevPlayerFuncType[pi] = funcType
            if (inPlay && funcType != prevFt && nowMs - lastBallTapSoundAt > 160L) {
                when (funcType) {
                    4 -> {
                        // Soft subtle ball tap for short pass (much quieter than long pass / lob / shot)
                        lastBallTapSoundAt = nowMs
                        audioSystem.playSound("ballsound", 0.11f, 1.16f)
                    }
                    7 -> {
                        // Header ball contact
                        lastBallTapSoundAt = nowMs
                        audioSystem.playSound("ballsound", 0.20f, 1.05f)
                    }
                    5, 6 -> {
                        // Firm ball kick for long pass or lob / high pass
                        lastBallTapSoundAt = nowMs
                        audioSystem.playSound("ballsound", 0.34f, 0.98f)
                    }
                    8 -> {
                        // Powerful ball strike for shot
                        lastBallTapSoundAt = nowMs
                        audioSystem.playSound("ballsound", 0.42f, 0.92f)
                    }
                }
            }
        }
        // 2. Keep crowd as a light background murmur during normal play (only goals trigger a real cheer)
        val nowInBox = inPlay && kotlin.math.abs(ballX) > 0.68f && kotlin.math.abs(ballY) < 0.25f
        if (nowInBox != inPenaltyBox) {
            inPenaltyBox = nowInBox
            if (nowInBox && crowdVolumeTarget <= crowdBaseline + 0.02f) {
                crowdVolumeTarget = 0.065f
                Handler(Looper.getMainLooper()).postDelayed({
                    if (crowdVolumeTarget <= 0.08f) crowdVolumeTarget = crowdBaseline
                }, 2200)
            } else if (!nowInBox && crowdVolumeTarget <= 0.08f) {
                crowdVolumeTarget = crowdBaseline
            }
        }
        if (!firstGameStateReceived || nowMs - lastStateLogAt >= 5000L) {
            lastStateLogAt = nowMs
            // NetworkPlayerState (48B): pos(12)+vel(8)+dir(12)+rotY(4)+anim+team+role+flags@39+tired(4)+functionType+enumVelocity+animProgress(2)
            val activeIdx = (0 until 22).firstOrNull { data[72 + it * 48 + 39].toInt() and 4 != 0 } ?: -1
            val camX = bb.getFloat(1224)
            val camY = bb.getFloat(1228)
            val camZ = bb.getFloat(1232)
            val camRot0 = bb.getFloat(1236)
            val camRot1 = bb.getFloat(1240)
            val camRot2 = bb.getFloat(1244)
            val camRot3 = bb.getFloat(1248)
            val camFov = bb.getFloat(1252)
            Log.i("DZ_STATE", "ANDROID_IN size=${data.size} tick=$tick inPlay=$inPlay active=$activeIdx ball=($ballX,$ballY,$ballZ) score=$scoreA-$scoreB timer=${minutes}:${seconds} cam=($camX,$camY,$camZ) rot=($camRot0,$camRot1,$camRot2,$camRot3) fov=$camFov")
        }
        val wasFirst = !firstGameStateReceived
        if (wasFirst) {
            loadingHandler.postDelayed(endWatchdog, 5000)
        }
        firstGameStateReceived = true
        val uiChanged = wasFirst || scoreA != prevUiScoreA || scoreB != prevUiScoreB || minutes != prevUiMinutes || seconds != prevUiSeconds
        if (uiChanged) {
            prevUiScoreA = scoreA
            prevUiScoreB = scoreB
            prevUiMinutes = minutes
            prevUiSeconds = seconds
            runOnUiThread {
                scoreText.text = "$scoreA - $scoreB"
                timerText.text = String.format(java.util.Locale.US, "%02d:%02d", minutes, seconds)
                if (loadingContainer.visibility == View.VISIBLE) {
                    updateLoadingOverlay()
                }
            }
        }
    }

    private fun updateLoadingOverlay() {
        if (!::loadingContainer.isInitialized) return
        // Godot is ready when the adapter reports it's initialized
        val ready = godotAdapter.isReady()
        val progress = if (ready) 100 else 50
        val state = if (ready) 0 else 0
        val elapsed = System.currentTimeMillis() - loadingStartTime

        val text = when {
            state == 7 -> "Échec du chargement moteur"
            !ready && elapsed < 5000 -> "Initialisation du moteur..."
            !ready && elapsed < 10000 -> "Compilation des shaders..."
            !ready && elapsed < 15000 -> "Chargement des textures..."
            !ready -> "Chargement des animations..."
            !firstSetupReceived -> "En attente des données du match..."
            !firstGameStateReceived -> "En attente du premier état de jeu..."
            else -> "Prêt"
        }
        loadingProgressBar.progress = progress
        loadingStatusText.text = text

        if (state == 7) {
            if (!retryDialogShown) {
                retryDialogShown = true
                showRetryDialog("Le moteur de rendu n'a pas pu s'initialiser.")
            }
            return
        }

        // Demo mode timeout: ONLY when no server connection was requested
        // (offline launch without mode/params). A real match goes through the
        // GF Docker worker which can take ~2min to spawn — wait much longer.
        if (ready && elapsed > GAMESTATE_TIMEOUT_MS && !firstSetupReceived && !retryDialogShown) {
            if (!expectingServer) {
                Log.w("DZ_LOADING", "Timeout waiting for match setup after ${elapsed}ms — switching to demo mode")
                firstSetupReceived = true
                firstGameStateReceived = true
                loadingStatusText.text = "Mode démo (aucun serveur)"
                loadingProgressBar.progress = 100
                loadingHandler.postDelayed({
                    if (loadingContainer.visibility == View.VISIBLE) {
                        Log.i("DZ_LOADING", "Hiding loading overlay in demo mode after ${elapsed}ms")
                        loadingContainer.visibility = View.GONE
                    }
                }, 1500)
                return
            }
            // Server match expected but setup hasn't arrived yet — keep waiting
            // up to SERVER_TIMEOUT_MS, then offer a retry instead of fake demo data.
            // BUT if live gamestates are already flowing, the connection is fine —
            // degrade to the match without setup (retry would block a working match).
            if (elapsed > SERVER_TIMEOUT_MS) {
                if (firstGameStateReceived) {
                    Log.w("DZ_LOADING", "Setup lost but gamestates flowing — entering match after ${elapsed}ms")
                    firstSetupReceived = true
                    return
                }
                retryDialogShown = true
                showRetryDialog("Le serveur de match ne répond pas. Réessayer ?")
                return
            }
        }

        // Hard timeout: hide the overlay regardless after 25s offline,
        // or SERVER_TIMEOUT_MS when a real server connection is expected.
        val hardTimeout = if (!expectingServer) 25000L else SERVER_TIMEOUT_MS
        if (elapsed > hardTimeout && loadingContainer.visibility == View.VISIBLE) {
            Log.w("DZ_LOADING", "Hard timeout — hiding loading overlay after ${elapsed}ms (godotReady=$ready)")
            firstSetupReceived = true
            firstGameStateReceived = true
            loadingContainer.visibility = View.GONE
            return
        }

        // gf.setup is published one-shot by the GF server; if the relay bot
        // joined late it's lost. If live gamestates arrive anyway, don't hold
        // the overlay hostage — degrade to default kits after 20s.
        if (ready && !firstSetupReceived && firstGameStateReceived && elapsed > 20000L) {
            Log.w("DZ_LOADING", "Gamestates flowing but setup never arrived — entering match without it after ${elapsed}ms")
            firstSetupReceived = true
        }

        if (ready && firstSetupReceived && firstGameStateReceived && loadingContainer.visibility == View.VISIBLE) {
            Log.i("DZ_LOADING", "Hiding loading overlay after ${elapsed}ms")
            loadingContainer.visibility = View.GONE
        }
    }

    private fun showRetryDialog(message: String) {
        runOnUiThread {
            android.app.AlertDialog.Builder(this)
                .setTitle("Chargement")
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton("Réessayer") { _, _ ->
                    retryCount = 0
                    retryLastConnection()
                }
                .setNegativeButton("Quitter") { _, _ -> finishAffinity() }
                .show()
        }
    }

    private fun retryLastConnection() {
        if (retryCount >= MAX_RETRIES) {
            showRetryDialog("Trop de tentatives. Vérifiez votre connexion.")
            return
        }
        retryCount++
        retryDialogShown = false
        loadingStartTime = System.currentTimeMillis()
        firstGameStateReceived = false
        firstSetupReceived = false
        loadingContainer.visibility = View.VISIBLE
        loadingHandler.post(loadingRunnable)
        Log.i("DZ_LOADING", "Retry ${retryCount}/$MAX_RETRIES")

        val lkUrl = lastLkUrl
        val token = lastToken
        if (lkUrl != null && token != null) {
            if (::lkManager.isInitialized) {
                lkManager.disconnect()
            }
            lkManager = LiveKitManager(this)
            lkManager.connect(lkUrl, token)
            startInputLoop()
        } else {
            // No stored connection params: recreate match from mode
            when (lastMode) {
                "ai_vs_ai" -> createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = lastForceMcoVsMca)
                "vs_ai", "ar" -> createMatchAndConnect(mode = "vs_ai", forceMcoVsMca = false, selectedTeamA = lastTeamA, selectedTeamB = lastTeamB)
                else -> showModeSelectionDialogFallback()
            }
        }
    }

    fun onMatchSetupReceived(data: ByteArray) {
        if (!firstSetupReceived) {
            firstSetupReceived = true
            Log.i("DZ_LOADING", "Match setup received, ${data.size} bytes")
            runOnUiThread { updateLoadingOverlay() }
        }
        godotAdapter.sendMatchSetup(data)
    }

    override fun onResume() {
        super.onResume()
        godotAdapter.onResume()
        audioSystem.init()
        Thread { loadSounds() }.start()
    }

    override fun onStart() {
        super.onStart()
        godotAdapter.onStart()
    }

    override fun onStop() {
        super.onStop()
        godotAdapter.onStop()
    }

    override fun onPause() {
        super.onPause()
        godotAdapter.onPause()
        audioSystem.destroy()
        synchronized(soundsLock) { soundsLoaded = false }
    }

    override fun onDestroy() {
        super.onDestroy()
        loadingHandler.removeCallbacksAndMessages(null)
        stopInputLoop()
        if (::lkManager.isInitialized) {
            lkManager.disconnect()
        }
        godotAdapter.destroy()
        // MainActivity runs in dedicated ":godot" process; terminate it on exit
        // so the next match starts with a clean libgodot_android.so state.
        android.os.Process.killProcess(android.os.Process.myPid())
    }

    // Called from C++ via JNI (AudioManager bridge)
    @Suppress("unused")
    fun nativeAudioPlay(name: String) { audioSystem.play(name) }
    @Suppress("unused")
    fun nativeAudioStopAll() { audioSystem.stopAll() }
    @Suppress("unused")
    fun nativeAudioSetVolume(vol: Float) { audioSystem.setVolume(vol) }
    @Suppress("unused")
    fun nativeAudioPlayLoop(name: String) { audioSystem.playLoop(name) }
    @Suppress("unused")
    fun nativeAudioStopLoop() { audioSystem.stopLoop() }

    private fun loadSounds() {
        synchronized(soundsLock) {
            if (soundsLoaded) return
        }
        val sounds = listOf(
            "ballsound", "crowd01", "crowd02",
            "goalpost", "whistle2", "whistle3"
        )
        for (name in sounds) {
            try {
                assets.open("sounds/$name.ogg").use { stream ->
                    val data = stream.readBytes()
                    audioSystem.loadSound(name, data)
                }
            } catch (e: Exception) {
                /* sound not found */
            }
        }
        synchronized(soundsLock) {
            soundsLoaded = true
        }
    }

    fun handleMatchEvent(data: ByteArray) {
        if (data.size < 36) return

        val eventType = data[12].toInt() and 0xFF
        val team      = data[13].toInt() and 0xFF
        val playerIdx = data[14].toInt() and 0xFF
        val scoreA    = data[32].toInt() and 0xFF
        val scoreB    = data[33].toInt() and 0xFF

        when (eventType) {
            0  -> onGoal(team, playerIdx, scoreA, scoreB)
            1  -> onYellowCard(team, playerIdx)
            2  -> onRedCard(team, playerIdx)
            3  -> onSubstitution(team, playerIdx)
            4  -> onSetPiece("CORNER", team)
            5  -> onSetPiece("THROW-IN", team)
            6  -> onSetPiece("FREE KICK", team)
            7  -> onSetPiece("PENALTY", team)
            8  -> onKickOff()
            9  -> onEndMatch(scoreA, scoreB)
            10 -> onHalfTime()
            11 -> onSetPiece("GOAL KICK", team)
            12 -> onSetPiece("OFFSIDE", team)
            13 -> onFoul(team, playerIdx)
            15 -> onShot()
            16 -> onPass()
        }
    }

    private fun onGoal(team: Int, @Suppress("UNUSED_PARAMETER") playerIdx: Int, scoreA: Int, scoreB: Int) {
        audioSystem.playSound("whistle2", 0.28f, 1.0f)
        crowdVolumeTarget = 0.18f
        Handler(Looper.getMainLooper()).postDelayed({
            audioSystem.playSound("crowd01", 0.17f, 1.0f)
        }, 350)
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = crowdBaseline }, 3800)
        vibrate(500)

        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("⚽ GOAL! $teamName\n$scoreA - $scoreB", 3000)

        runOnUiThread {
            scoreText.text = "$scoreA - $scoreB"
        }
    }

    private fun onYellowCard(team: Int, playerIdx: Int) {
        audioSystem.playSound("whistle3", 0.26f, 1.0f)
        crowdVolumeTarget = 0.075f
        Handler(Looper.getMainLooper()).postDelayed({
            if (crowdVolumeTarget <= 0.09f) crowdVolumeTarget = crowdBaseline
        }, 1600)
        vibrate(200)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟨 YELLOW CARD — $teamName #$playerIdx", 2000)
    }

    private fun onRedCard(team: Int, playerIdx: Int) {
        audioSystem.playSound("whistle3", 0.30f, 1.0f)
        crowdVolumeTarget = 0.09f
        Handler(Looper.getMainLooper()).postDelayed({
            if (crowdVolumeTarget <= 0.10f) crowdVolumeTarget = crowdBaseline
        }, 2200)
        vibrate(400)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟥 RED CARD — $teamName #$playerIdx", 3000)
    }

    private fun onSubstitution(team: Int, @Suppress("UNUSED_PARAMETER") playerIdx: Int) {
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🔄 SUBSTITUTION — $teamName", 2000)
    }

    private fun onSetPiece(name: String, team: Int) {
        audioSystem.playSound("whistle2", 0.24f, 1.0f)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("$name — $teamName", 1500)
    }

    private fun onFoul(@Suppress("UNUSED_PARAMETER") team: Int, @Suppress("UNUSED_PARAMETER") playerIdx: Int) {
        audioSystem.playSound("whistle3", 0.25f, 1.0f)
        crowdVolumeTarget = 0.07f
        Handler(Looper.getMainLooper()).postDelayed({
            if (crowdVolumeTarget <= 0.08f) crowdVolumeTarget = crowdBaseline
        }, 1200)
        vibrate(100)
        showEventOverlay("⚠️ FOUL", 1000)
    }

    private fun onShot() {
        val nowMs = System.currentTimeMillis()
        if (nowMs - lastBallTapSoundAt > 140L) {
            lastBallTapSoundAt = nowMs
            audioSystem.playSound("ballsound", 0.42f, 0.92f)
        }
        if (crowdVolumeTarget <= crowdBaseline + 0.02f) {
            crowdVolumeTarget = 0.08f
            Handler(Looper.getMainLooper()).postDelayed({
                if (crowdVolumeTarget <= 0.09f) crowdVolumeTarget = crowdBaseline
            }, 1400)
        }
    }

    private fun onPass() {
        val nowMs = System.currentTimeMillis()
        if (nowMs - lastBallTapSoundAt > 140L) {
            lastBallTapSoundAt = nowMs
            audioSystem.playSound("ballsound", 0.11f, 1.16f)
        }
    }

    private fun onKickOff() {
        audioSystem.playSound("whistle2", 0.25f, 1.0f)
        showEventOverlay("🔥 KICK OFF!", 2000)
        audioSystem.startCrowdLoopFromAssets(assets, "sounds/crowd02.ogg", crowdBaseline)
        crowdVolumeTarget = crowdBaseline
    }

    private fun onHalfTime() {
        audioSystem.play("whistle2")
        audioSystem.stopLoop()
        showEventOverlay("🏁 HALF TIME", 3000)
    }

    private fun onEndMatch(scoreA: Int, scoreB: Int) {
        if (matchEnded) return
        matchEnded = true
        Log.i("DZ_END", "Match over ($scoreA-$scoreB) — stopping input, disconnecting, exiting")
        audioSystem.play("whistle2")
        audioSystem.stopLoop()
        // The GF container is gone — stop pumping inputs and drop LiveKit.
        stopInputLoop()
        if (::lkManager.isInitialized) {
            lkManager.disconnect()
        }
        // Shut Godot down now (covered by the overlay) so the GL thread exits
        // while the activity is alive — leaving it to die during finish()
        // teardown segfaults inside libgodot (onRenderThreadExiting).
        godotAdapter.destroy()
        // Final score stays up briefly, then back to the menu instead of a
        // frozen last frame with players still looping their anims.
        showEventOverlay("🏆 FIN DU MATCH\n$scoreA - $scoreB", 4500)
        loadingHandler.postDelayed({
            if (!isFinishing && !isDestroyed) finish()
        }, 5000)
    }

    private fun showEventOverlay(@Suppress("UNUSED_PARAMETER") text: String, @Suppress("UNUSED_PARAMETER") durationMs: Long) {
        // Disabled yellow Android overlay: Godot renders clean white broadcast notifications directly.
        runOnUiThread {
            eventText.visibility = View.GONE
        }
    }

    private fun vibrate(ms: Long) {
        try {
            @Suppress("DEPRECATION")
            val vibrator = getSystemService(Activity.VIBRATOR_SERVICE) as? Vibrator
            if (vibrator != null && vibrator.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE))
                } else {
                    @Suppress("DEPRECATION")
                    vibrator.vibrate(ms)
                }
            }
        } catch (e: Exception) {
            /* vibration failed */
        }
    }

    override fun getCommandLine(): List<String> = godotAdapter.getCommandLine()
    override fun getHostPlugins(godot: Godot): Set<GodotPlugin> = godotAdapter.getHostPlugins(godot)
    override fun getGodot(): Godot = godotAdapter.getGodot()
    override fun getActivity(): Activity = this
}
