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
                    is RoomEvent.Connected -> {
                        android.util.Log.i("LiveKitManager", "Connected to room")
                    }
                    is RoomEvent.Disconnected -> {
                        android.util.Log.i("LiveKitManager", "Disconnected from room")
                    }
                    is RoomEvent.DataReceived -> {
                        android.util.Log.d("LiveKitManager", "DataReceived topic=${event.topic} size=${event.data.size}")
                        when (event.topic) {
                            "gs" -> activity.onGameStateReceived(event.data)
                            "ev" -> activity.jni.nativeOnGameEvent(event.data)
                        }
                    }
                    is RoomEvent.ParticipantConnected -> {
                        android.util.Log.d("LiveKitManager", "ParticipantConnected: ${event.participant.sid}")
                    }
                    is RoomEvent.TrackSubscribed -> {
                        android.util.Log.d("LiveKitManager", "TrackSubscribed")
                    }
                    is RoomEvent.FailedToConnect -> {
                        android.util.Log.e("LiveKitManager", "FailedToConnect: ${event.error?.message}")
                    }
                    is RoomEvent.ParticipantDisconnected -> {
                        android.util.Log.d("LiveKitManager", "ParticipantDisconnected")
                    }
                    else -> {
                        android.util.Log.d("LiveKitManager", "Event: ${event.javaClass.simpleName}")
                    }
                }
            }
        }

        scope.launch {
            try {
                room!!.connect(wsUrl, token)
                android.util.Log.i("LiveKitManager", "Room connect call completed")
            } catch (e: Exception) {
                android.util.Log.e("LiveKitManager", "Failed to connect: ${e.message}")
            }
        }
    }

    fun sendInput(inputBytes: ByteArray) {
        val r = room
        if (r == null) {
            android.util.Log.w("LiveKitManager", "sendInput: room is null")
            return
        }
        if (r.state != Room.State.CONNECTED) {
            android.util.Log.w("LiveKitManager", "sendInput: not connected, state=${r.state}")
            return
        }
        scope.launch {
            try {
                r.localParticipant.publishData(
                    data = inputBytes,
                    reliability = DataPublishReliability.LOSSY,
                    topic = "in"
                )
                android.util.Log.d("LiveKitManager", "publishData ok topic=in size=${inputBytes.size}")
            } catch (e: Exception) {
                android.util.Log.w("LiveKitManager", "publishData failed: ${e.message}")
            }
        }
    }

    fun disconnect() {
        room?.disconnect()
    }
}
