package com.football.ar

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import java.io.ByteArrayOutputStream

class JniBridge {

    external fun nativeOnFrame(
        viewMatrix: FloatArray,
        projMatrix: FloatArray,
        anchorMatrix: FloatArray,
        gameStateData: ByteArray
    )

    external fun nativeOnGameStateReceived(data: ByteArray)
    external fun nativeOnGameEvent(data: ByteArray)
    external fun nativeOnTacticalState(data: ByteArray)
    external fun nativeInit(context: Context, assetManager: AssetManager, isEmulator: Boolean): Boolean
    external fun nativeGetBuildMarker(): String
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
            Log.i("JniBridge", "Loading native library football_ar ...")
            System.loadLibrary("football_ar")
            Log.i("JniBridge", "Native library football_ar loaded OK")
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

        @JvmStatic
        fun decodeAssetRgba(context: Context, path: String): ByteArray? {
            return try {
                context.assets.open(path).use { stream ->
                    val bitmap = BitmapFactory.decodeStream(stream) ?: return null
                    val rgba = bitmap.copy(Bitmap.Config.ARGB_8888, false)
                    val width = rgba.width
                    val height = rgba.height
                    val pixels = IntArray(width * height)
                    rgba.getPixels(pixels, 0, width, 0, 0, width, height)
                    val out = ByteArrayOutputStream(8 + pixels.size * 4)
                    out.write(width and 0xFF)
                    out.write((width shr 8) and 0xFF)
                    out.write((width shr 16) and 0xFF)
                    out.write((width shr 24) and 0xFF)
                    out.write(height and 0xFF)
                    out.write((height shr 8) and 0xFF)
                    out.write((height shr 16) and 0xFF)
                    out.write((height shr 24) and 0xFF)
                    for (argb in pixels) {
                        out.write((argb shr 16) and 0xFF)
                        out.write((argb shr 8) and 0xFF)
                        out.write(argb and 0xFF)
                        out.write((argb ushr 24) and 0xFF)
                    }
                    if (rgba !== bitmap) rgba.recycle()
                    bitmap.recycle()
                    out.toByteArray()
                }
            } catch (_: Throwable) {
                null
            }
        }
    }
}
