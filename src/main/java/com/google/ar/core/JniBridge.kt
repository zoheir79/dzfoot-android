package com.google.ar.core

import android.content.Context
import android.content.res.AssetManager

class JniBridge {

    external fun nativeOnFrame(
        viewMatrix: FloatArray,
        projMatrix: FloatArray,
        anchorMatrix: FloatArray,
        gameStateData: ByteArray
    )

    external fun nativeOnGameStateReceived(data: ByteArray)
    external fun nativeOnGameEvent(data: ByteArray)
    external fun nativeInit(context: Context, assetManager: AssetManager): Boolean
    external fun nativeResume(context: Context)
    external fun nativePause()
    external fun nativeSurfaceCreated()
    external fun nativeDisplayChanged(rotation: Int, width: Int, height: Int)

    fun getClassLoader(): ClassLoader {
        return JniBridge::class.java.classLoader!!
    }

    companion object {
        init {
            System.loadLibrary("football_ar")
        }
    }
}
