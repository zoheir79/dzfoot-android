package com.football.ar

import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
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
        jni.nativeInit(this, assets, isEmulator())

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(this@MainActivity)
        }

        audioSystem.init()

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
        android.app.AlertDialog.Builder(this)
            .setTitle("Sélection du Mode de Jeu")
            .setMessage("Choisissez votre expérience visuelle de GameplayFootball :")
            .setCancelable(false)
            .setPositiveButton("Réalité Augmentée (AR)") { _, _ ->
                checkArCoreAndInit(forceClassic = false)
                createMatchAndConnect()
            }
            .setNegativeButton("Rendu 3D Classique") { _, _ ->
                checkArCoreAndInit(forceClassic = true)
                createMatchAndConnect()
            }
            .show()
    }

    private fun createMatchAndConnect() {
        Thread {
            try {
                val url = java.net.URL("http://102.220.31.70:8002/internal/create-match")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/json")
                conn.doOutput = true
                conn.connectTimeout = 10000
                conn.readTimeout = 10000

                val body = "{\"player_a\":\"user1\",\"player_b\":\"bot\",\"duration\":300}"
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
                val lkUrl = json.getString("livekit_url").replace("https://", "wss://")
                val token = json.getString("token")

                runOnUiThread {
                    lkManager = LiveKitManager(this)
                    lkManager.connect(lkUrl, token)
                    Log.i("MainActivity", "Connecting to LiveKit room: $roomId")
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
}
