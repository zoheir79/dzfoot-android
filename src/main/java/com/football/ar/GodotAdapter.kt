package com.football.ar

import android.app.Activity
import android.util.Log
import android.view.View
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity
import org.godotengine.godot.Godot
import org.godotengine.godot.GodotFragment
import org.godotengine.godot.plugin.GodotPlugin
import java.io.File

/**
 * Manages the Godot engine lifecycle inside the Android app.
 * Refactored to use GodotFragment for Godot 4.x compatibility.
 */
class GodotAdapter(private val activity: Activity) {

    private val TAG = "GodotAdapter_V9"

    private var godotFragment: GodotFragment? = null
    private var containerLayout: FrameLayout? = null
    private var dzPlugin: DZFootPlugin? = null
    private var dzPluginGodot: Godot? = null

    fun initialize(): View? {
        try {
            Log.i(TAG, "!!! INITIALIZING GODOT ENGINE V9 (GodotFragment) !!!")

            containerLayout = FrameLayout(activity)
            containerLayout?.id = View.generateViewId()

            // Copy project.pck to internal storage to ensure reliable access
            val pckFile = File(activity.filesDir, "project.pck")
            try {
                activity.assets.open("project.pck").use { input ->
                    pckFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                Log.i(TAG, "!!! project.pck copied successfully (size: ${pckFile.length()}) !!!")
            } catch (e: Exception) {
                Log.e(TAG, "!!! Failed to copy project.pck: ${e.message} !!!")
            }

            val fragmentActivity = activity as? AppCompatActivity
                ?: throw IllegalStateException("Activity must be an AppCompatActivity")

            godotFragment = GodotFragment()
            
            fragmentActivity.supportFragmentManager.beginTransaction()
                .replace(containerLayout!!.id, godotFragment!!)
                .commitNowAllowingStateLoss()

            return containerLayout
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize Godot: ${e.message}", e)
            return null
        }
    }

    fun getCommandLine(): List<String> {
        val pckFile = File(activity.filesDir, "project.pck")
        return listOf("--main-pack", pckFile.absolutePath)
    }

    fun getHostPlugins(godot: Godot): Set<GodotPlugin> {
        // The plugin is bound to a specific Godot instance. On the second
        // match the previous plugin still references the dead instance and
        // emitSignal() throws "not registered" — recreate it per instance.
        if (dzPlugin == null || dzPluginGodot !== godot) {
            dzPlugin = DZFootPlugin(godot)
            dzPluginGodot = godot
        }
        return setOf(dzPlugin!!)
    }

    fun getGodot(): Godot {
        return godotFragment?.getGodot() 
            ?: Godot.getInstance(activity)
    }

    fun sendGameState(data: ByteArray) { dzPlugin?.emitGameState(data) }
    fun sendMatchSetup(data: ByteArray) { dzPlugin?.emitMatchSetup(data) }
    fun sendMatchEvent(data: ByteArray) { dzPlugin?.emitMatchEvent(data) }
    fun sendTacticalState(data: ByteArray) { dzPlugin?.emitTacticalState(data) }
    fun getPlayerInput(): ByteArray? { return dzPlugin?.getInputBytes() }

    // Lifecycle is handled by GodotFragment
    fun onPause() {}
    fun onResume() {}
    fun onStart() {}
    fun onStop() {}
    fun destroy() {
        // Removing the fragment runs its onDestroy → Godot.onDestroy, which
        // shuts the engine and GL thread down cleanly while the activity is
        // still alive. Just dropping references lets the GL thread exit during
        // activity teardown → SIGSEGV in GodotRenderer.onRenderThreadExiting.
        val frag = godotFragment
        godotFragment = null
        containerLayout = null
        if (frag != null && activity is AppCompatActivity) {
            try {
                activity.supportFragmentManager.beginTransaction()
                    .remove(frag)
                    .commitNowAllowingStateLoss()
            } catch (e: Exception) {
                Log.w(TAG, "destroy: fragment removal failed: ${e.message}")
            }
        }
    }

    fun isReady(): Boolean {
        val godot = godotFragment?.getGodot()
        val initialized = godot?.isInitialized() ?: false
        val hasView = godot?.renderView != null
        if (!initialized || !hasView) {
            Log.d(TAG, "isReady check: initialized=$initialized, hasView=$hasView")
        }
        return initialized && hasView
    }
}
