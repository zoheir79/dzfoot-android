package com.football.ar

import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Vibrator
import android.os.VibrationEffect
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.ar.core.ArCoreApk
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : AppCompatActivity(), GLSurfaceView.Renderer {

    private lateinit var glView: GLSurfaceView
    val jni = JniBridge()
    private lateinit var lkManager: LiveKitManager
    val audioSystem = AudioSystem()

    private var latestGameState = ByteArray(0)
    private val viewMatrix   = FloatArray(16)
    private val projMatrix   = FloatArray(16)
    private val anchorMatrix = FloatArray(16)

    private lateinit var scoreText: TextView
    private lateinit var timerText: TextView
    private lateinit var eventText: TextView

    // Dynamic crowd volume (FIFA-style reactive audio)
    private var crowdVolumeCurrent = 0.3f
    private var crowdVolumeTarget = 0.3f
    private val crowdHandler = Handler(Looper.getMainLooper())
    private val crowdRunnable = object : Runnable {
        override fun run() {
            // Smooth volume transition toward target
            if (kotlin.math.abs(crowdVolumeCurrent - crowdVolumeTarget) > 0.01f) {
                crowdVolumeCurrent += (crowdVolumeTarget - crowdVolumeCurrent) * 0.1f
                audioSystem.setLoopVolume(crowdVolumeCurrent)
            }
            crowdHandler.postDelayed(this, 100)
        }
    }

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)

        // Initialize JNI and AssetManager immediately before GLSurfaceView starts rendering
        val initOk = jni.nativeInit(this, assets, isEmulator())
        val marker = jni.nativeGetBuildMarker()
        Toast.makeText(this, "Native: $marker", Toast.LENGTH_LONG).show()

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(this@MainActivity)
        }

        audioSystem.init()
        Thread { loadSounds() }.start() // background thread: disk I/O from assets must not block UI
        crowdHandler.post(crowdRunnable)

        scoreText = TextView(this).apply {
            textSize = 24f
            setTextColor(0xFFFFFFFF.toInt())
            text = "0 - 0"
        }
        timerText = TextView(this).apply {
            textSize = 20f
            setTextColor(0xFFFFFFFF.toInt())
            text = "00:00"
        }
        eventText = TextView(this).apply {
            textSize = 18f
            setTextColor(0xFFFFFF00.toInt())
            text = ""
        }

        val root = FrameLayout(this).apply {
            addView(glView, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)

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

            // On-screen controls are now rendered natively via OpenGL (TouchController)
            // The old Android Button layout has been removed to avoid overlap.
        }
        setContentView(root)

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

        if (mode != null && roomId != null && token != null && lkUrl != null) {
            // Launched from menu with all parameters — connect directly
            checkArCoreAndInit(forceClassic = forceClassic)
            connectToLiveKit(lkUrl, token, roomId, mode)
        } else if (mode != null) {
            // Single-player modes that need match creation
            checkArCoreAndInit(forceClassic = forceClassic)
            when (mode) {
                "ai_vs_ai" -> createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = forceMcoVsMca)
                "vs_ai", "ar" -> createMatchAndConnect(mode = if (mode == "ar") "vs_ai" else mode, forceMcoVsMca = false)
                else -> showModeSelectionDialogFallback()
            }
        } else {
            // Direct launch without parameters — show fallback dialog
            showModeSelectionDialogFallback()
        }
    }

    private fun connectToLiveKit(lkUrl: String, token: String, roomId: String, mode: String) {
        lkManager = LiveKitManager(this)
        lkManager.connect(lkUrl, token)
        /* connected */
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
                    0 -> {
                        checkArCoreAndInit(forceClassic = false)
                        createMatchAndConnect(mode = "vs_ai")
                    }
                    1 -> {
                        checkArCoreAndInit(forceClassic = true)
                        createMatchAndConnect(mode = "vs_ai")
                    }
                    2 -> {
                        checkArCoreAndInit(forceClassic = true)
                        createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = true)
                    }
                    3 -> {
                        checkArCoreAndInit(forceClassic = true)
                        createMatchPvP()
                    }
                    4 -> {
                        checkArCoreAndInit(forceClassic = true)
                        joinMatch()
                    }
                }
            }
            .setCancelable(false)
            .show()
    }

    private fun createMatchAndConnect(
        mode: String = "vs_ai",
        forceMcoVsMca: Boolean = false
    ) {
        Thread {
            try {
                // 1. Fetch teams list and resolve IDs
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
                    lkManager = LiveKitManager(this)
                    lkManager.connect(lkUrl, token)
                    /* connecting */
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
                    lkManager = LiveKitManager(this)
                    lkManager.connect(lkUrl, token)
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
                    lkManager = LiveKitManager(this)
                    lkManager.connect(lkUrl, token)
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

    private var userRequestedInstall = false

    private fun isEmulator(): Boolean {
        return (Build.FINGERPRINT.contains("generic") ||
                Build.FINGERPRINT.startsWith("google/sdk_gphone") ||
                Build.MODEL.contains("Emulator") ||
                Build.MODEL.contains("sdk_gphone") ||
                Build.MANUFACTURER.contains("Google") && Build.BRAND.contains("generic"))
    }

    private fun checkArCoreAndInit(forceClassic: Boolean) {
        if (forceClassic) {
            jni.nativeInit(this, assets, isEmulator = true)
            return
        }

        val availability = ArCoreApk.getInstance().checkAvailability(this)
        if (availability.isTransient) {
            Handler(Looper.getMainLooper()).postDelayed({ checkArCoreAndInit(forceClassic) }, 200)
            return
        }

        if (availability.isSupported) {
            try {
                when (ArCoreApk.getInstance().requestInstall(this, !userRequestedInstall)) {
                    ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                        userRequestedInstall = true
                        return
                    }
                    ArCoreApk.InstallStatus.INSTALLED -> {}
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "ARCore install request failed: ${e.message}")
            }
            try {
                jni.nativeInit(this, assets, isEmulator = false)
            } catch (e: Exception) {
                Log.e("MainActivity", "ARCore native init failed: ${e.message} - using fallback")
                jni.nativeInit(this, assets, isEmulator = true)
            }
        } else {
            jni.nativeInit(this, assets, isEmulator = true)
        }
    }

    private var sendInputLogCount = 0
    private var lastSendInputMs = 0L
    private val soundsLock = Object()
    private var soundsLoaded = false
    private fun sendInput() {
        val now = System.currentTimeMillis()
        if (now - lastSendInputMs < 50) return // throttle to ~20 Hz max
        lastSendInputMs = now
        if (::lkManager.isInitialized) {
            val inputBytes = jni.nativeGetInputBytes()
            if (inputBytes.isNotEmpty()) {
                // Parse PlayerInputPacket (36 bytes) to log real values
                if (inputBytes.size >= 20) {
                    val buf = java.nio.ByteBuffer.wrap(inputBytes).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                    val magic = buf.getShort(0).toInt() and 0xFFFF
                    val version = buf.get(2).toInt() and 0xFF
                    val type = buf.get(3).toInt() and 0xFF
                    val team = buf.get(8).toInt() and 0xFF
                    val playerIdx = buf.get(9).toInt() and 0xFF
                    val dirX = buf.getFloat(10)
                    val dirZ = buf.getFloat(14)
                    val buttons = buf.getShort(16).toInt() and 0xFFFF
                    if (sendInputLogCount++ % 10 == 0) {
                        Log.i("gamestates", "ANDROID_OUT team=$team player=$playerIdx dir=($dirX,$dirZ) buttons=0x%04X magic=0x%04X ver=$version".format(buttons, magic))
                    }
                }
                lkManager.sendInput(inputBytes)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent?): Boolean {
        event ?: return false
        val action = event.actionMasked
        val pointerIdx = event.actionIndex
        val pointerId = event.getPointerId(pointerIdx)
        val x = event.getX(pointerIdx)
        val y = event.getY(pointerIdx)
        jni.nativeOnTouch(x, y, action, pointerId)
        sendInput()
        return true
    }

    fun onGameStateReceived(data: ByteArray) {
        synchronized(this) {
            latestGameState = data.copyOf()
        }
        if (data.size < 1256) {
            Log.w("gamestates", "ANDROID_IN GameState incomplete: ${data.size} bytes (expected 1256)")
            return
        }
        // Parse binary GameStatePacket (offset 8=tick, 32=ball pos, 26=score, 28=timer)
        val bb = java.nio.ByteBuffer.wrap(data).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        val tick = bb.getInt(8)
        val scoreA = data[26].toInt()
        val scoreB = data[27].toInt()
        val timerBytes = data.sliceArray(28 until 32)
        val timer = java.nio.ByteBuffer.wrap(timerBytes).order(java.nio.ByteOrder.LITTLE_ENDIAN).float
        val minutes = (timer / 60).toInt()
        val seconds = (timer % 60).toInt()
        val ballX = bb.getFloat(32)
        val ballY = bb.getFloat(36)
        val ballZ = bb.getFloat(40)
        val activeIdx = (0 until 22).firstOrNull { data[72 + it * 48 + 44].toInt() and 4 != 0 } ?: -1
        // Camera offset: 12(header)+4(tick)+8(ts)+1(mode)+1(flags)+2(score)+4(timer)+40(ball)+1056(players)+96(officials)=1224
        val camX = bb.getFloat(1224)
        val camY = bb.getFloat(1228)
        val camZ = bb.getFloat(1232)
        val camRot0 = bb.getFloat(1236)
        val camRot1 = bb.getFloat(1240)
        val camRot2 = bb.getFloat(1244)
        val camRot3 = bb.getFloat(1248)
        val camFov = bb.getFloat(1252)
        Log.i("gamestates", "ANDROID_IN size=${data.size} tick=$tick ball=($ballX,$ballY,$ballZ) score=$scoreA-$scoreB timer=${minutes}:${seconds} active=$activeIdx cam=($camX,$camY,$camZ) rot=($camRot0,$camRot1,$camRot2,$camRot3) fov=$camFov")
        runOnUiThread {
            scoreText.text = "$scoreA - $scoreB"
            timerText.text = String.format(java.util.Locale.US, "%02d:%02d", minutes, seconds)
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val stateCopy = synchronized(this) {
            latestGameState.copyOf()
        }
        jni.nativeOnFrame(viewMatrix, projMatrix, anchorMatrix, stateCopy)
        sendInput() // continuous send: buttons held while finger stays down
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        jni.nativeSurfaceCreated()
    }

    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        @Suppress("DEPRECATION")
        val rotation = windowManager.defaultDisplay.rotation
        jni.nativeDisplayChanged(rotation, w, h)
    }

    override fun onResume() {
        super.onResume()
        glView.onResume()
        jni.nativeResume(this)
        audioSystem.init()
        Thread { loadSounds() }.start()
    }

    override fun onPause() {
        super.onPause()
        glView.onPause()
        jni.nativePause()
        audioSystem.destroy()
        synchronized(soundsLock) { soundsLoaded = false }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (::lkManager.isInitialized) {
            lkManager.disconnect()
        }
        jni.nativeDestroy()
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
        audioSystem.play("whistle2")
        crowdVolumeTarget = 1.0f // crowd erupts
        Handler(Looper.getMainLooper()).postDelayed({
            audioSystem.play("crowd01")
        }, 500)
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.5f }, 4000)
        vibrate(500)

        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("⚽ GOAL! $teamName\n$scoreA - $scoreB", 3000)

        runOnUiThread {
            scoreText.text = "$scoreA - $scoreB"
        }
    }

    private fun onYellowCard(team: Int, playerIdx: Int) {
        audioSystem.play("whistle3")
        crowdVolumeTarget = 0.6f
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.35f }, 2000)
        vibrate(200)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟨 YELLOW CARD — $teamName #$playerIdx", 2000)
    }

    private fun onRedCard(team: Int, playerIdx: Int) {
        audioSystem.play("whistle3")
        crowdVolumeTarget = 0.2f // shocked silence
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.4f }, 3000)
        vibrate(400)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟥 RED CARD — $teamName #$playerIdx", 3000)
    }

    private fun onSubstitution(team: Int, @Suppress("UNUSED_PARAMETER") playerIdx: Int) {
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🔄 SUBSTITUTION — $teamName", 2000)
    }

    private fun onSetPiece(name: String, team: Int) {
        audioSystem.play("whistle2")
        crowdVolumeTarget = 0.5f // tension builds
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.3f }, 2000)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("$name — $teamName", 1500)
    }

    private fun onFoul(@Suppress("UNUSED_PARAMETER") team: Int, @Suppress("UNUSED_PARAMETER") playerIdx: Int) {
        audioSystem.play("whistle3")
        crowdVolumeTarget = 0.7f // crowd reacts to foul
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.35f }, 1500)
        vibrate(100)
        showEventOverlay("⚠️ FOUL", 1000)
    }

    private fun onShot() {
        audioSystem.play("ballsound")
        crowdVolumeTarget = 0.8f // anticipation
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.35f }, 2000)
    }

    private fun onPass() {
        // Subtle pass sound can be played here if desired
    }

    private fun onKickOff() {
        audioSystem.play("whistle2")
        showEventOverlay("🔥 KICK OFF!", 2000)
        audioSystem.playLoop("crowd02")
        crowdVolumeTarget = 0.5f
        Handler(Looper.getMainLooper()).postDelayed({ crowdVolumeTarget = 0.35f }, 3000)
    }

    private fun onHalfTime() {
        audioSystem.play("whistle2")
        audioSystem.stopLoop()
        showEventOverlay("🏁 HALF TIME", 3000)
    }

    private fun onEndMatch(scoreA: Int, scoreB: Int) {
        audioSystem.play("whistle2")
        audioSystem.stopLoop()
        showEventOverlay("🏆 FULL TIME\n$scoreA - $scoreB", 5000)
    }

    private fun showEventOverlay(text: String, durationMs: Long) {
        runOnUiThread {
            eventText.text = text
            eventText.visibility = View.VISIBLE
            eventText.alpha = 0f
            eventText.animate().alpha(1f).setDuration(200).start()

            Handler(Looper.getMainLooper()).postDelayed({
                eventText.animate().alpha(0f).setDuration(500).withEndAction {
                    eventText.visibility = View.GONE
                }.start()
            }, durationMs)
        }
    }

    private fun vibrate(ms: Long) {
        try {
            @Suppress("DEPRECATION")
            val vibrator = getSystemService(VIBRATOR_SERVICE) as? Vibrator
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
}
