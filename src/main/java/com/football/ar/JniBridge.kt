package com.football.ar

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
    external fun nativeInit(context: Context, assetManager: AssetManager, isEmulator: Boolean): Boolean
    external fun nativeDestroy()
    external fun nativeResume(context: Context)
    external fun nativePause()
    external fun nativeSurfaceCreated()
    external fun nativeDisplayChanged(rotation: Int, width: Int, height: Int)
    external fun nativeOnTouch(x: Float, y: Float, action: Int)
    external fun nativeGetInputBytes(): ByteArray
    external fun nativeSetActionKick(on: Boolean)
    external fun nativeSetActionPass(on: Boolean)
    external fun nativeSetActionShot(on: Boolean)
    external fun nativeSetActionDribble(on: Boolean)
    external fun nativeSetSprint(on: Boolean)

    // VERY explicit instance method
    fun getClassLoader(): ClassLoader {
        return JniBridge::class.java.classLoader!!
    }

    companion object {
        init {
            System.loadLibrary("football_ar")
        }

        // VERY explicit static method
        @JvmStatic
        fun getClassLoader(context: Context): ClassLoader {
            return context.classLoader
        }
        
        // Static variant without args if needed
        @JvmStatic
        fun getClassLoaderStatic(): ClassLoader {
            return JniBridge::class.java.classLoader!!
        }
    }
}
