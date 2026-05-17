package com.football.ar

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import io.livekit.android.LiveKit
import io.livekit.android.events.RoomEvent
import io.livekit.android.room.Room
import io.livekit.android.room.participant.DataPublishOptions

class LiveKitManager(private val context: Context) {

    private var room: Room? = null
    private val jniBridge = JniBridge()
    private val scope = CoroutineScope(Dispatchers.IO)

    fun connect(wsUrl: String, token: String) {
        room = LiveKit.create(context)

        scope.launch {
            room!!.events.collect { event ->
                when (event) {
                    is RoomEvent.DataReceived -> {
                        when (event.topic) {
                            "gs" -> jniBridge.nativeOnGameStateReceived(event.data)
                            "ev" -> jniBridge.nativeOnGameEvent(event.data)
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
        room?.localParticipant?.publishData(
            data = inputBytes,
            options = DataPublishOptions(
                reliable = false,
                topic = "in"
            )
        )
    }

    fun disconnect() {
        room?.disconnect()
    }
}
