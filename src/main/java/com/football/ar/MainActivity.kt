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
import androidx.appcompat.app.AppCompatActivity
import com.google.ar.core.ArCoreApk
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : AppCompatActivity(), GLSurfaceView.Renderer {

    private lateinit var glView: GLSurfaceView
    val jni = JniBridge()
    private lateinit var lkManager: LiveKitManager

    private var latestGameState = ByteArray(0)
    private val viewMatrix   = FloatArray(16)
    private val projMatrix   = FloatArray(16)
    private val anchorMatrix = FloatArray(16)

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(2)
            setRenderer(this@MainActivity)
        }

        val root = FrameLayout(this).apply {
            addView(glView, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)

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

        checkArCoreAndInit()
        createMatchAndConnect()
    }

    private fun createMatchAndConnect() {
        Thread {
            try {
                val url = java.net.URL("http://10.0.2.2:8002/internal/create-match")
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

    private fun checkArCoreAndInit() {
        // Set to true to skip ARCore on emulator (use fallback rendering).
        // Set to false to FORCE ARCore attempt — useful to test AR code path on emulator.
        val skipArCore = false

        if (isEmulator() && skipArCore) {
            Log.i("MainActivity", "Emulator: fallback mode (ARCore skipped)")
            jni.nativeInit(this, assets, true)
            return
        }

        if (isEmulator()) {
            Log.i("MainActivity", "Emulator detected - forcing ARCore attempt (will fall back if it fails)")
        }

        val availability = ArCoreApk.getInstance().checkAvailability(this)
        if (availability.isTransient) {
            Handler(Looper.getMainLooper()).postDelayed({ checkArCoreAndInit() }, 200)
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
        latestGameState = data.copyOf()
    }

    override fun onDrawFrame(gl: GL10?) {
        jni.nativeOnFrame(viewMatrix, projMatrix, anchorMatrix, latestGameState)
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
    }

    override fun onPause() {
        super.onPause()
        glView.onPause()
        jni.nativePause()
    }
}
