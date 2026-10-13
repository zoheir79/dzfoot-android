package com.football.ar

import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin
import org.godotengine.godot.plugin.SignalInfo
import org.godotengine.godot.plugin.UsedByGodot
import java.util.concurrent.atomic.AtomicReference

/**
 * GodotPlugin bridge between Kotlin (LiveKit) and GDScript (DZFootScene).
 *
 * Signals (emitted from Kotlin → received by GDScript):
 *   - game_state_received(byte[])
 *   - match_setup_received(byte[])
 *   - match_event_received(byte[])
 *
 * Methods (called from GDScript → read by Kotlin):
 *   - set_input_packet(byte[])  — GDScript stores serialized PlayerInputPacket
 *
 * Kotlin reads input via getInputBytes() on the 100Hz input loop.
 */
class DZFootPlugin(godot: Godot) : GodotPlugin(godot) {

    companion object {
        const val SIGNAL_GAME_STATE = "game_state_received"
        const val SIGNAL_MATCH_SETUP = "match_setup_received"
        const val SIGNAL_MATCH_EVENT = "match_event_received"
        const val SIGNAL_TACTICAL = "tactical_state_received"
    }

    private val inputBytesRef = AtomicReference<ByteArray?>(null)

    override fun getPluginName(): String = "DZFootPlugin"

    override fun getPluginSignals(): Set<SignalInfo> {
        return setOf(
            SignalInfo(SIGNAL_GAME_STATE, ByteArray::class.java),
            SignalInfo(SIGNAL_MATCH_SETUP, ByteArray::class.java),
            SignalInfo(SIGNAL_MATCH_EVENT, ByteArray::class.java),
            SignalInfo(SIGNAL_TACTICAL, ByteArray::class.java)
        )
    }

    @UsedByGodot
    fun set_input_packet(data: ByteArray) {
        inputBytesRef.set(data)
    }

    @UsedByGodot
    fun is_godot_ready(): Boolean = true

    fun emitGameState(data: ByteArray) {
        emitSignal(SIGNAL_GAME_STATE, data)
    }

    fun emitMatchSetup(data: ByteArray) {
        emitSignal(SIGNAL_MATCH_SETUP, data)
    }

    fun emitMatchEvent(data: ByteArray) {
        emitSignal(SIGNAL_MATCH_EVENT, data)
    }

    fun emitTacticalState(data: ByteArray) {
        emitSignal(SIGNAL_TACTICAL, data)
    }

    fun getInputBytes(): ByteArray? {
        return inputBytesRef.getAndSet(null)
    }
}
