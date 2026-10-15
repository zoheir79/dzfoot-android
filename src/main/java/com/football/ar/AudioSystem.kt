package com.football.ar

import android.content.res.AssetManager
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
import android.util.Log

class AudioSystem {
    private var soundPool: SoundPool? = null
    private val soundMap = mutableMapOf<String, Int>()
    private var sfxVolume = 0.35f
    private var loopVolume = 0.045f
    private var crowdPlayer: MediaPlayer? = null
    private var currentLoopName = ""

    fun init() {
        if (soundPool != null) return
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
            val tempFile = java.io.File.createTempFile("snd_", "_.ogg")
            tempFile.deleteOnExit()
            tempFile.writeBytes(data)
            val id = pool.load(tempFile.absolutePath, 1)
            soundMap[name] = id
            Log.i("AudioSystem", "Loaded sound $name -> id=$id")
        }
    }

    fun play(name: String) {
        playSound(name, sfxVolume, 1.0f)
    }

    fun playSound(name: String, vol: Float, rate: Float = 1.0f) {
        val id = soundMap[name] ?: return
        val v = vol.coerceIn(0f, 1f)
        val r = rate.coerceIn(0.5f, 1.5f)
        soundPool?.play(id, v, v, 1, 0, r)
    }

    fun stopAll() {
        soundPool?.autoPause()
    }

    fun setVolume(vol: Float) {
        sfxVolume = vol.coerceIn(0f, 1f)
    }

    fun startCrowdLoopFromAssets(assets: AssetManager, assetPath: String = "sounds/crowd02.ogg", initialVol: Float = 0.0f) {
        if (crowdPlayer != null && currentLoopName == assetPath) return
        stopLoop()
        try {
            val afd = assets.openFd(assetPath)
            val mp = MediaPlayer()
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            mp.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
            afd.close()
            mp.isLooping = true
            mp.prepare()
            loopVolume = initialVol.coerceIn(0f, 0.20f)
            mp.setVolume(loopVolume, loopVolume)
            if (loopVolume > 0.005f) {
                mp.start()
            }
            crowdPlayer = mp
            currentLoopName = assetPath
        } catch (e: Exception) {
            Log.w("AudioSystem", "Failed to start crowd MediaPlayer: ${e.message}")
        }
    }

    fun playLoop(name: String) {
        if (currentLoopName == name && crowdPlayer != null) return
    }

    fun stopLoop() {
        try {
            crowdPlayer?.let {
                if (it.isPlaying) it.stop()
                it.release()
            }
        } catch (_: Exception) {
        }
        crowdPlayer = null
        currentLoopName = ""
    }

    fun setLoopVolume(vol: Float) {
        loopVolume = vol.coerceIn(0f, 0.20f)
        try {
            crowdPlayer?.let { mp ->
                if (loopVolume > 0.006f) {
                    mp.setVolume(loopVolume, loopVolume)
                    if (!mp.isPlaying) mp.start()
                } else {
                    if (mp.isPlaying) mp.pause()
                }
            }
        } catch (_: Exception) {
        }
    }

    fun destroy() {
        stopLoop()
        soundPool?.release()
        soundPool = null
        soundMap.clear()
    }
}
