package com.football.ar

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
                            "setup" -> activity.jni.nativeOnMatchSetup(event.data)
                            "ev" -> {
                                if (event.data.size >= 8) {
                                    val type = (event.data[6].toInt() and 0xFF) or ((event.data[7].toInt() and 0xFF) shl 8)
                                    if (type == 3) { // PACKET_MATCH_SETUP
                                        activity.jni.nativeOnMatchSetup(event.data)
                                    } else {
                                        activity.jni.nativeOnGameEvent(event.data)
                                        activity.handleMatchEvent(event.data)
                                    }
                                } else {
                                    activity.jni.nativeOnGameEvent(event.data)
                                    activity.handleMatchEvent(event.data)
                                }
                            }
                            "tac" -> activity.jni.nativeOnTacticalState(event.data)
                        }
                    }
                    is RoomEvent.ParticipantConnected -> {
                        android.util.Log.d("LiveKitManager", "ParticipantConnected: ${event.participant.identity} sid=${event.participant.sid}")
                    }
                    is RoomEvent.TrackSubscribed -> {
                        android.util.Log.d("LiveKitManager", "TrackSubscribed")
                    }
                    is RoomEvent.FailedToConnect -> {
                        android.util.Log.e("LiveKitManager", "FailedToConnect: ${event.error.message}")
                    }
                    is RoomEvent.ParticipantDisconnected -> {
                        android.util.Log.d("LiveKitManager", "ParticipantDisconnected: ${event.participant.identity}")
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
                @Suppress("CheckResult")
                r.localParticipant.publishData(
                    data = inputBytes,
                    reliability = DataPublishReliability.LOSSY,
                    topic = "in",
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
