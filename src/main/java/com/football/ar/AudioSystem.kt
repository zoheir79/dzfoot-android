package com.football.ar

import android.media.AudioAttributes
import android.media.SoundPool
import android.util.Log

class AudioSystem {
    private var soundPool: SoundPool? = null
    private val soundMap = mutableMapOf<String, Int>()
    private var volume = 1.0f
    private var currentLoopId = 0
    private var currentLoopName = ""

    fun init() {
        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        soundPool = SoundPool.Builder()
            .setMaxStreams(8)
            .setAudioAttributes(attrs)
            .build()
        Log.i("AudioSystem", "SoundPool initialized")
    }

    fun loadSound(name: String, data: ByteArray) {
        soundPool?.let { pool ->
            // Write to temp file then load (SoundPool needs a file descriptor)
            val tempFile = java.io.File.createTempFile("snd_", "_.ogg")
            tempFile.deleteOnExit()
            tempFile.writeBytes(data)
            val id = pool.load(tempFile.absolutePath, 1)
            soundMap[name] = id
            Log.i("AudioSystem", "Loaded sound $name -> id=$id")
        }
    }

    fun play(name: String) {
        val id = soundMap[name] ?: return
        soundPool?.play(id, volume, volume, 1, 0, 1.0f)
    }

    fun stopAll() {
        soundPool?.autoPause()
    }

    fun setVolume(vol: Float) {
        volume = vol.coerceIn(0f, 1f)
    }

    fun playLoop(name: String) {
        if (currentLoopName == name) return
        stopLoop()
        val id = soundMap[name] ?: return
        currentLoopId = soundPool?.play(id, volume, volume, 1, -1, 1.0f) ?: 0
        currentLoopName = name
    }

    fun stopLoop() {
        if (currentLoopId != 0) {
            soundPool?.stop(currentLoopId)
            currentLoopId = 0
            currentLoopName = ""
        }
    }

    fun setLoopVolume(vol: Float) {
        volume = vol.coerceIn(0f, 1f)
        if (currentLoopId != 0 && currentLoopName.isNotEmpty()) {
            soundPool?.setVolume(currentLoopId, volume, volume)
        }
    }

    fun destroy() {
        stopLoop()
        soundPool?.release()
        soundPool = null
        soundMap.clear()
    }
}
