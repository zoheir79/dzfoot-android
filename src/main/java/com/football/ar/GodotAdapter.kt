package com.football.ar

import android.app.Activity
import android.content.Context
import android.util.Log
import android.view.View
import android.widget.FrameLayout
import org.godotengine.godot.Godot
import org.godotengine.godot.GodotHost
import org.godotengine.godot.GodotRenderView
import org.godotengine.godot.plugin.GodotPlugin
import java.util.Collections

/**
 * Manages the Godot engine lifecycle inside the Android app.
 * Replaces the old GLSurfaceView + ARRenderer approach.
 *
 * Godot renders the 3D scene (stadium, players, ball, camera, lighting)
 * while Kotlin handles LiveKit, touch input, and UI overlay.
 */
class GodotAdapter(private val activity: Activity) : GodotHost {

    private val TAG = "GodotAdapter"

    private var godot: Godot? = null
    private var renderView: GodotRenderView? = null
    private var containerLayout: FrameLayout? = null
    private var dzPlugin: DZFootPlugin? = null

    /**
     * Initialize Godot engine with the DZFoot project.
     * Returns the View that should be added to the Activity layout.
     */
    fun initialize(): View? {
        try {
            Log.i(TAG, "Initializing Godot engine...")

            godot = Godot.getInstance(activity)

            // Create the DZFoot plugin for Kotlin ↔ GDScript bridge
            dzPlugin = DZFootPlugin(godot!!)

            // Create a container layout for Godot's render view
            containerLayout = FrameLayout(activity)

            // Initialize the Godot engine with --main-pack pointing to the .pck
            // in the APK assets directory (relative path, resolved via AAssetManager).
            val cmdLine = listOf("--main-pack", "dzfoot.pck")
            Log.i(TAG, "Godot command line: $cmdLine")
            godot?.initEngine(this, cmdLine, emptySet())

            // Get the render view — may be null immediately after initEngine,
            // will retry in onResume()
            renderView = godot?.renderView
            if (renderView != null) {
                Log.i(TAG, "renderView available immediately, adding to container")
                containerLayout?.addView(renderView?.view)
            } else {
                Log.w(TAG, "renderView is null after initEngine — will retry in onResume")
            }

            godot?.onStart(this)

            Log.i(TAG, "Godot engine initialized successfully (isInitialized=${godot?.isInitialized()})")
            return containerLayout
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize Godot: ${e.message}", e)
            return null
        }
    }

    override fun getCommandLine(): List<String> {
        return listOf("--main-pack", "dzfoot.pck")
    }

    override fun getHostPlugins(godot: Godot): Set<GodotPlugin> {
        val plugins = mutableSetOf<GodotPlugin>()
        dzPlugin?.let { plugins.add(it) }
        Log.i(TAG, "getHostPlugins: providing ${plugins.size} plugins")
        return plugins
    }

    /**
     * Send a GameStatePacket (raw bytes) to the Godot scene via DZFootPlugin signal.
     * Called from LiveKit data channel callback.
     */
    fun sendGameState(data: ByteArray) {
        dzPlugin?.emitGameState(data)
    }

    /**
     * Send a MatchSetupPacket to Godot.
     */
    fun sendMatchSetup(data: ByteArray) {
        dzPlugin?.emitMatchSetup(data)
    }

    /**
     * Send a MatchEventPacket to Godot.
     */
    fun sendMatchEvent(data: ByteArray) {
        dzPlugin?.emitMatchEvent(data)
    }

    /**
     * Send a TacticalStatePacket to Godot (currently logged, can be forwarded later).
     */
    fun sendTacticalState(data: ByteArray) {
        // Tactical state can be forwarded to Godot if needed for UI/debug
        Log.d(TAG, "Tactical state received: ${data.size} bytes")
    }

    /**
     * Get the current PlayerInputPacket from Godot (touch/keyboard input).
     * Called periodically (100Hz) to send player input to the server via LiveKit.
     */
    fun getPlayerInput(): ByteArray? {
        return dzPlugin?.getInputBytes()
    }

    /**
     * Pause Godot rendering (e.g. when activity is paused).
     */
    fun onPause() {
        Log.i(TAG, "Pausing Godot")
        godot?.onPause(this)
    }

    /**
     * Resume Godot rendering.
     */
    fun onResume() {
        Log.i(TAG, "Resuming Godot")
        // Retry getting renderView — it may not have been available right after initEngine
        if (renderView == null) {
            renderView = godot?.renderView
            if (renderView != null) {
                Log.i(TAG, "renderView now available in onResume, adding to container")
                containerLayout?.addView(renderView?.view)
                containerLayout?.requestLayout()
            } else {
                Log.w(TAG, "renderView still null in onResume")
            }
        }
        godot?.onResume(this)
    }

    /**
     * Destroy Godot engine and free resources.
     */
    fun destroy() {
        Log.i(TAG, "Destroying Godot engine")
        godot?.onDestroy(this)
        renderView = null
        containerLayout = null
    }

    /**
     * Check if Godot engine is initialized and ready.
     */
    fun isReady(): Boolean {
        val g = godot ?: return false
        val initialized = g.isInitialized()
        val hasRenderView = renderView != null
        if (initialized && !hasRenderView) {
            // Retry getting renderView
            renderView = godot?.renderView
            if (renderView != null) {
                Log.i(TAG, "renderView obtained lazily in isReady(), adding to container")
                containerLayout?.addView(renderView?.view)
                containerLayout?.requestLayout()
            }
        }
        if (initialized) {
            Log.i(TAG, "isReady: initialized=true renderView=${renderView != null}")
        }
        return initialized
    }

    // === GodotHost interface ===

    override fun getActivity(): Activity {
        return activity
    }

    override fun getGodot(): Godot {
        return godot ?: throw IllegalStateException("Godot not initialized")
    }
}
