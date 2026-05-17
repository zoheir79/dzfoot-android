package com.football.ar

import android.opengl.GLSurfaceView
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
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
            setEGLContextClientVersion(3)
            setRenderer(this@MainActivity)
        }
        setContentView(glView)

        if (checkSelfPermission(android.Manifest.permission.CAMERA) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(android.Manifest.permission.CAMERA), 123)
        }

        jni.nativeInit(this, assets)

        val wsUrl = intent.getStringExtra("livekit_url") ?: ""
        val token = intent.getStringExtra("livekit_token") ?: ""
        if (wsUrl.isNotEmpty() && token.isNotEmpty()) {
            lkManager = LiveKitManager(this)
            lkManager.connect(wsUrl, token)
        }
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
