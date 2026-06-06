package com.football.ar

import android.content.Context
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

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)

        // Initialize JNI and AssetManager immediately before GLSurfaceView starts rendering
        Log.i("MainActivity", "About to call nativeInit ...")
        val initOk = jni.nativeInit(this, assets, isEmulator())
        Log.i("MainActivity", "nativeInit returned: $initOk")
        val marker = jni.nativeGetBuildMarker()
        Log.i("MainActivity", "Native build marker: $marker")
        Toast.makeText(this, "Native: $marker", Toast.LENGTH_LONG).show()

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(this@MainActivity)
        }

        audioSystem.init()
        loadSounds()

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

            val btnLayout = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(20, 100, 20, 20)
                val lp = FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT).apply {
                    marginEnd = 20
                    topMargin = 200
                }
                lp.gravity = android.view.Gravity.END or android.view.Gravity.CENTER_VERTICAL
                layoutParams = lp

                fun makeBtn(text: String, onDown: () -> Unit, onUp: () -> Unit): Button {
                    return Button(context).apply {
                        this.text = text
                        textSize = 12f
                        setOnTouchListener { _, event ->
                            when (event.action) {
                                MotionEvent.ACTION_DOWN -> { onDown(); sendInput(); true }
                                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { onUp(); sendInput(); true }
                                else -> false
                            }
                        }
                    }
                }

                addView(makeBtn("Sprint", { jni.nativeSetSprint(true) }, { jni.nativeSetSprint(false) }))
                addView(makeBtn("Dribble", { jni.nativeSetActionDribble(true) }, { jni.nativeSetActionDribble(false) }))
                addView(makeBtn("Pass", { jni.nativeSetActionPass(true) }, { jni.nativeSetActionPass(false) }))
                addView(makeBtn("Shot", { jni.nativeSetActionShot(true) }, { jni.nativeSetActionShot(false) }))
                addView(makeBtn("Kick", { jni.nativeSetActionKick(true) }, { jni.nativeSetActionKick(false) }))
            }
            addView(btnLayout)
        }
        setContentView(root)

        if (checkSelfPermission(android.Manifest.permission.CAMERA) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(android.Manifest.permission.CAMERA), 123)
        }

        showModeSelectionDialog()
    }

    private fun showModeSelectionDialog() {
        val modes = arrayOf("Réalité Augmentée (AR)", "Rendu 3D Classique", "Test IA vs IA (MCO vs MCA)")
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
                        // Test mode: bot vs bot, MCO vs MCA, classic rendering
                        checkArCoreAndInit(forceClassic = true)
                        createMatchAndConnect(mode = "ai_vs_ai", forceMcoVsMca = true)
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
                    val teamsUrl = java.net.URL("http://102.220.31.70:8005/teams")
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
                                if (name.contains("oranais") || shortName == "mco") {
                                    teamA = obj.getString("id")
                                    Log.i("MainActivity", "Found MCO: ${obj.optString("name")} id=$teamA")
                                }
                                if (name.contains("moulodia club d'alger") || shortName == "mca") {
                                    teamB = obj.getString("id")
                                    Log.i("MainActivity", "Found MCA: ${obj.optString("name")} id=$teamB")
                                }
                            }
                            if (teamA == null || teamB == null) {
                                Log.w("MainActivity", "MCO/MCA not found in catalog, falling back to random")
                            }
                        }
                        if (teamA == null || teamB == null) {
                            if (teamsArr.length() >= 2) {
                                val idxA = (0 until teamsArr.length()).random()
                                var idxB = (0 until teamsArr.length()).random()
                                while (idxB == idxA) idxB = (0 until teamsArr.length()).random()
                                teamA = teamsArr.getJSONObject(idxA).getString("id")
                                teamB = teamsArr.getJSONObject(idxB).getString("id")
                                val nameA = teamsArr.getJSONObject(idxA).optString("name", "Team A")
                                val nameB = teamsArr.getJSONObject(idxB).optString("name", "Team B")
                                Log.i("MainActivity", "Picked random teams: $nameA vs $nameB")
                            }
                        }
                    }
                } catch (e: Exception) {
                    Log.w("MainActivity", "Could not fetch DZ teams, using fallback: ${e.message}")
                }

                // 2. Create the match
                val url = java.net.URL("http://102.220.31.70:8002/internal/create-match")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/json")
                conn.doOutput = true
                conn.connectTimeout = 10000
                conn.readTimeout = 10000

                val playerA = if (mode == "ai_vs_ai") "bot" else "user1"
                val playerB = "bot"
                val bodyBuilder = StringBuilder()
                bodyBuilder.append("{\"player_a\":\"$playerA\",\"player_b\":\"$playerB\",\"duration\":300,\"mode\":\"$mode\"")
                if (teamA != null) bodyBuilder.append(",\"team_a\":\"$teamA\"")
                if (teamB != null) bodyBuilder.append(",\"team_b\":\"$teamB\"")
                bodyBuilder.append("}")
                val body = bodyBuilder.toString()
                Log.i("MainActivity", "Create-match body: $body")
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

                runOnUiThread {
                    lkManager = LiveKitManager(this)
                    lkManager.connect(lkUrl, token)
                    Log.i("MainActivity", "Connecting to LiveKit room: $roomId (mode=$mode)")
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "Failed to create match: ${e.message}")
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
            Log.i("MainActivity", "Classic mode requested: skipping ARCore session creation")
            jni.nativeInit(this, assets, true)
            return
        }

        val availability = ArCoreApk.getInstance().checkAvailability(this)
        if (availability.isTransient) {
            Handler(Looper.getMainLooper()).postDelayed({ checkArCoreAndInit(forceClassic) }, 200)
            return
        }

        Log.i("MainActivity", "ARCore availability: $availability (supported=${availability.isSupported})")

        if (availability.isSupported) {
            try {
                when (ArCoreApk.getInstance().requestInstall(this, !userRequestedInstall)) {
                    ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                        userRequestedInstall = true
                        Log.i("MainActivity", "ARCore install requested - will retry on resume")
                        return
                    }
                    ArCoreApk.InstallStatus.INSTALLED -> {
                        Log.i("MainActivity", "ARCore installed - creating session")
                    }
                }
            } catch (e: Exception) {
                Log.e("MainActivity", "ARCore install request failed: ${e.message}")
            }
            try {
                jni.nativeInit(this, assets, false)
                Log.i("MainActivity", "ARCore native init done")
            } catch (e: Exception) {
                Log.e("MainActivity", "ARCore native init failed: ${e.message} - using fallback")
                jni.nativeInit(this, assets, true)
            }
        } else {
            Log.w("MainActivity", "ARCore NOT supported on this device - using fallback rendering")
            jni.nativeInit(this, assets, true)
        }
    }

    private var sendInputLogCount = 0
    private fun sendInput() {
        if (::lkManager.isInitialized) {
            val inputBytes = jni.nativeGetInputBytes()
            if (inputBytes.isNotEmpty()) {
                if (sendInputLogCount++ % 20 == 0) {
                    Log.d("MainActivity", "sendInput called, size=${inputBytes.size}")
                }
                lkManager.sendInput(inputBytes)
            }
        } else {
            Log.w("MainActivity", "sendInput: lkManager not initialized yet")
        }
    }

    override fun onTouchEvent(event: MotionEvent?): Boolean {
        event ?: return false
        jni.nativeOnTouch(event.x, event.y, event.action)
        sendInput()
        return true
    }

    fun onGameStateReceived(data: ByteArray) {
        synchronized(this) {
            latestGameState = data.copyOf()
        }
        if (data.size < 72) return
        // Parse binary GameStatePacket (offset 26 = score[2], offset 28 = timer float)
        val scoreA = data[26].toInt()
        val scoreB = data[27].toInt()
        val timerBytes = data.sliceArray(28 until 32)
        val timer = java.nio.ByteBuffer.wrap(timerBytes).order(java.nio.ByteOrder.LITTLE_ENDIAN).float
        val minutes = (timer / 60).toInt()
        val seconds = (timer % 60).toInt()
        runOnUiThread {
            scoreText.text = "$scoreA - $scoreB"
            timerText.text = String.format("%02d:%02d", minutes, seconds)
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val stateCopy = synchronized(this) {
            latestGameState.copyOf()
        }
        jni.nativeOnFrame(viewMatrix, projMatrix, anchorMatrix, stateCopy)
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        jni.nativeSurfaceCreated()
    }

    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        jni.nativeDisplayChanged(windowManager.defaultDisplay.rotation, w, h)
    }

    override fun onResume() {
        super.onResume()
        glView.onResume()
        jni.nativeResume(this)
        audioSystem.init()
        loadSounds()
    }

    override fun onPause() {
        super.onPause()
        glView.onPause()
        jni.nativePause()
        audioSystem.destroy()
    }

    override fun onDestroy() {
        super.onDestroy()
        jni.nativeDestroy()
    }

    // Called from C++ via JNI (AudioManager bridge)
    fun nativeAudioPlay(name: String) { audioSystem.play(name) }
    fun nativeAudioStopAll() { audioSystem.stopAll() }
    fun nativeAudioSetVolume(vol: Float) { audioSystem.setVolume(vol) }
    fun nativeAudioPlayLoop(name: String) { audioSystem.playLoop(name) }
    fun nativeAudioStopLoop() { audioSystem.stopLoop() }

    private fun loadSounds() {
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
                Log.w("MainActivity", "Sound $name not found in assets: ${e.message}")
            }
        }
    }

    fun handleMatchEvent(data: ByteArray) {
        if (data.size < 36) return

        val eventType = data[12].toInt() and 0xFF
        val team      = data[13].toInt() and 0xFF
        val playerIdx = data[14].toInt() and 0xFF
        val scoreA    = data[32].toInt() and 0xFF
        val scoreB    = data[33].toInt() and 0xFF

        Log.i("MainActivity", "handleMatchEvent: type=$eventType team=$team player=$playerIdx score=$scoreA-$scoreB")

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

    private fun onGoal(team: Int, playerIdx: Int, scoreA: Int, scoreB: Int) {
        audioSystem.play("whistle2")
        Handler(Looper.getMainLooper()).postDelayed({
            audioSystem.play("crowd01")
        }, 500)
        vibrate(500)

        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("⚽ GOAL! $teamName\n$scoreA - $scoreB", 3000)

        runOnUiThread {
            scoreText.text = "$scoreA - $scoreB"
        }
    }

    private fun onYellowCard(team: Int, playerIdx: Int) {
        audioSystem.play("whistle3")
        vibrate(200)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟨 YELLOW CARD — $teamName #$playerIdx", 2000)
    }

    private fun onRedCard(team: Int, playerIdx: Int) {
        audioSystem.play("whistle3")
        vibrate(400)
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🟥 RED CARD — $teamName #$playerIdx", 3000)
    }

    private fun onSubstitution(team: Int, playerIdx: Int) {
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("🔄 SUBSTITUTION — $teamName", 2000)
    }

    private fun onSetPiece(name: String, team: Int) {
        audioSystem.play("whistle2")
        val teamName = if (team == 0) "Team A" else "Team B"
        showEventOverlay("$name — $teamName", 1500)
    }

    private fun onFoul(team: Int, playerIdx: Int) {
        audioSystem.play("whistle3")
        vibrate(100)
        showEventOverlay("⚠️ FOUL", 1000)
    }

    private fun onShot() {
        audioSystem.play("ballsound")
    }

    private fun onPass() {
        // Subtle pass sound can be played here if desired
    }

    private fun onKickOff() {
        audioSystem.play("whistle2")
        showEventOverlay("🔥 KICK OFF!", 2000)
        audioSystem.playLoop("crowd02")
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
            val vibrator = getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
            if (vibrator != null && vibrator.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE))
                } else {
                    @Suppress("DEPRECATION")
                    vibrator.vibrate(ms)
                }
            }
        } catch (e: Exception) {
            Log.w("MainActivity", "Vibration failed: ${e.message}")
        }
    }
}
