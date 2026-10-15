package com.football.ar

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import android.os.Handler
import android.os.Looper
import io.livekit.android.LiveKit
import io.livekit.android.events.RoomEvent
import io.livekit.android.events.collect
import io.livekit.android.room.Room
import io.livekit.android.room.track.DataPublishReliability

class LiveKitManager(private val activity: MainActivity) {

    private var room: Room? = null
    private val scope = CoroutineScope(Dispatchers.IO)
    private var wsUrl: String = ""
    private var token: String = ""
    private var retryCount = 0
    private val maxRetries = 12
    private val retryHandler = Handler(Looper.getMainLooper())
    private var connectAttempt = 0
    private var stopped = false

    fun connect(wsUrl: String, token: String) {
        this.wsUrl = wsUrl
        this.token = token
        retryCount = 0
        connectAttempt++
        connectInternal()
    }

    private fun connectInternal() {
        val attempt = connectAttempt
        room = LiveKit.create(activity)

        scope.launch {
            room!!.events.collect { event ->
                if (attempt != connectAttempt) return@collect
                when (event) {
                    is RoomEvent.Connected -> {
                        android.util.Log.i("DZ_LK", "LK Connected")
                        retryCount = 0
                    }
                    is RoomEvent.Disconnected -> {
                        android.util.Log.i("DZ_LK", "LK Disconnected")
                        scheduleRetry("Disconnected")
                    }
                    is RoomEvent.DataReceived -> {
                        if (event.topic != "gs" && event.topic != "tac") {
                            android.util.Log.i("DZ_LK", "LK_RECV topic=${event.topic} size=${event.data.size}")
                        }
                        when (event.topic) {
                            "gs" -> activity.onGameStateReceived(event.data)
                            "setup" -> activity.onMatchSetupReceived(event.data)
                            "ev" -> {
                                if (event.data.size >= 8) {
                                    val type = (event.data[6].toInt() and 0xFF) or ((event.data[7].toInt() and 0xFF) shl 8)
                                    if (type == 4) { // PACKET_MATCH_SETUP (DZFootProtocol.h)
                                        activity.onMatchSetupReceived(event.data)
                                    } else {
                                        activity.godotAdapter.sendMatchEvent(event.data)
                                        activity.handleMatchEvent(event.data)
                                    }
                                } else {
                                    activity.godotAdapter.sendMatchEvent(event.data)
                                    activity.handleMatchEvent(event.data)
                                }
                            }
                            "tac" -> {
                                activity.godotAdapter.sendTacticalState(event.data)
                            }
                        }
                    }
                    is RoomEvent.FailedToConnect -> {
                        android.util.Log.e("DZ_LK", "LK FailedToConnect: ${event.error.message}")
                        scheduleRetry("FailedToConnect: ${event.error.message}")
                    }
                    else -> {} // ignore other events
                }
            }
        }

        scope.launch {
            try {
                room!!.connect(wsUrl, token)
                if (attempt == connectAttempt) {
                    android.util.Log.i("DZ_LK", "LK connect call completed")
                }
            } catch (e: Exception) {
                if (attempt == connectAttempt) {
                    android.util.Log.e("DZ_LK", "LK Failed to connect: ${e.message}")
                    scheduleRetry("Exception: ${e.message}")
                }
            }
        }
    }

    private fun scheduleRetry(reason: String) {
        if (stopped) return
        if (retryCount >= maxRetries) {
            android.util.Log.e("DZ_LK", "LK giving up after $maxRetries retries ($reason)")
            return
        }
        retryCount++
        val delayMs = (1000 * retryCount).toLong()
        android.util.Log.w("DZ_LK", "LK retry $retryCount/$maxRetries in ${delayMs}ms ($reason)")
        retryHandler.postDelayed({
            if (!stopped && room?.state != Room.State.CONNECTED) {
                disconnect()
                connectInternal()
            }
        }, delayMs)
    }

    fun sendInput(inputBytes: ByteArray, reliable: Boolean = false) {
        val r = room
        if (r == null) {
            return
        }
        if (r.state != Room.State.CONNECTED) {
            return
        }
        scope.launch {
            try {
                @Suppress("CheckResult")
                r.localParticipant.publishData(
                    data = inputBytes,
                    reliability = if (reliable) DataPublishReliability.RELIABLE else DataPublishReliability.LOSSY,
                    topic = "in",
                )
            } catch (e: Exception) {
                android.util.Log.w("DZ_NET", "LK_SEND publishData failed: ${e.message}")
            }
        }
    }

    fun disconnect() {
        stopped = true
        retryHandler.removeCallbacksAndMessages(null)
        room?.disconnect()
    }
}
