package com.football.ar

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import io.livekit.android.LiveKit
import io.livekit.android.events.RoomEvent
import io.livekit.android.events.collect
import io.livekit.android.room.Room
import io.livekit.android.room.track.DataPublishReliability

class LiveKitManager(private val activity: MainActivity) {

    private var room: Room? = null
    private val scope = CoroutineScope(Dispatchers.IO)

    fun connect(wsUrl: String, token: String) {
        room = LiveKit.create(activity)

        scope.launch {
            room!!.events.collect { event ->
                when (event) {
                    is RoomEvent.DataReceived -> {
                        when (event.topic) {
                            "gs" -> activity.jni.nativeOnGameStateReceived(event.data)
                            "ev" -> activity.jni.nativeOnGameEvent(event.data)
                        }
                    }
                    is RoomEvent.ParticipantDisconnected -> {
                        // Notify opponent disconnected
                    }
                    else -> {}
                }
            }
        }

        scope.launch {
            room!!.connect(wsUrl, token)
        }
    }

    fun sendInput(inputBytes: ByteArray) {
        scope.launch {
            room?.localParticipant?.publishData(
                data = inputBytes,
                reliability = DataPublishReliability.LOSSY,
                topic = "in"
            )
        }
    }

    fun disconnect() {
        room?.disconnect()
    }
}
