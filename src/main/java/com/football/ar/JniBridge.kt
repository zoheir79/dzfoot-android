package com.football.ar

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
    external fun nativeInit(assetManager: AssetManager): Boolean
    external fun nativeResume()
    external fun nativePause()
    external fun nativeSurfaceCreated()
    external fun nativeDisplayChanged(rotation: Int, width: Int, height: Int)

    companion object {
        init {
            System.loadLibrary("football_ar")
        }
    }
}
