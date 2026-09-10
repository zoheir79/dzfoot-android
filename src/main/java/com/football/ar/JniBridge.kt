package com.football.ar

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import java.io.ByteArrayOutputStream

/**
 * JniBridge — adapted for Godot integration.
 *
 * The old native rendering methods (nativeOnFrame, nativeSurfaceCreated, etc.)
 * have been removed. Godot handles all rendering via GodotAdapter + GDExtension.
 *
 * This class now serves only as a utility for:
 * - Asset decoding (decodeAssetRgba) used by GDExtension for texture loading
 * - Audio callback bridge (nativeAudioPlay etc. are called from GDExtension)
 * - ClassLoader access for Godot JNI
 */
class JniBridge {

    @Suppress("unused")
    fun getClassLoader(): ClassLoader {
        return JniBridge::class.java.classLoader!!
    }

    companion object {
        private const val TAG = "JniBridge"

        init {
            // Godot's libgodot_android.so is loaded by the Godot AAR.
            // The old football_ar native library is no longer needed.
            Log.i(TAG, "JniBridge initialized (Godot mode — no native rendering library)")
        }

        @JvmStatic
        fun getClassLoader(context: Context): ClassLoader {
            return context.classLoader
        }

        @JvmStatic
        @Suppress("unused")
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
                    val out = ByteArrayOutputStream(8 + (pixels.size * 4))
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
