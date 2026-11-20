package dev.dfd.fractal.emulation

import android.widget.Toast
import kotlinx.coroutines.delay
import dev.dfd.fractal.common.customdrivers.DriverGuard
import dev.dfd.fractal.common.ui.localization.tr
import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import dev.dfd.fractal.BuildConfig
import dev.dfd.fractal.common.android.inputevent.isFromPhysicalController
import dev.dfd.fractal.common.settings.AppSettingsStore
import dev.dfd.fractal.nativeinterface.NativeFrameGen
import dev.dfd.fractal.common.ui.components.ActivityContent
import dev.dfd.fractal.common.ui.localization.TranslatableContent
import dev.dfd.fractal.emulation.input.ControllerCallbacks
import dev.dfd.fractal.emulation.input.ControllerMotionHandler
import dev.dfd.fractal.emulation.input.DeviceControllerCallbacks
import dev.dfd.fractal.emulation.input.DeviceMotionHandler
import dev.dfd.fractal.emulation.input.HotkeyManager
import dev.dfd.fractal.emulation.input.InputHandler
import dev.dfd.fractal.emulation.input.NativeInputDeviceListener
import dev.dfd.fractal.emulation.input.PhysicalMacroEngine
import dev.dfd.fractal.common.input.PhysicalMacroStore
import dev.dfd.fractal.settings.input.controller.AutoControllerMappingListener
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlin.system.exitProcess

private class InputDelegateManager(private val context: Context) {
    private val nativeInputDeviceListener = NativeInputDeviceListener(context)
    private val controllerCallbacks = ControllerCallbacks(context)
    private val controllerMotionHandler = ControllerMotionHandler(context)
    private val deviceControllerCallbacks = DeviceControllerCallbacks(context)
    private val deviceMotionHandler = DeviceMotionHandler(context)
    private val autoControllerMapping = AutoControllerMappingListener(context)

    fun setDeviceMotionEnabled(isListening: Boolean) =
        deviceMotionHandler.setIsListening(isListening)

    fun setDeviceRotation(rotation: Int) = deviceMotionHandler.setDeviceRotation(rotation)

    fun calibrateMotion(): Boolean = deviceMotionHandler.calibrate()

    fun resetMotionCalibration() = deviceMotionHandler.resetCalibration()

    fun registerAll() {
        nativeInputDeviceListener.register()
        controllerCallbacks.register()
        controllerMotionHandler.register()
        deviceControllerCallbacks.register()
        autoControllerMapping.register()
        PhysicalMacroEngine.register(context)
    }

    fun unregisterAll() {
        nativeInputDeviceListener.unregister()
        controllerCallbacks.unregister()
        controllerMotionHandler.unregister()
        deviceControllerCallbacks.unregister()
        autoControllerMapping.unregister()
        PhysicalMacroEngine.unregister(context)
    }

    fun onResume(rotation: Int) {
        registerAll()
        deviceMotionHandler.setDeviceRotation(rotation)
        deviceMotionHandler.resumeListening()
    }

    fun onPause() {
        unregisterAll()
        deviceMotionHandler.pauseListening()
    }
}

class EmulationActivity : AppCompatActivity() {
    private lateinit var inputManager: InputDelegateManager
    private var processInputEvents = true

    // the gyro axes follow the screen: the game may open while the phone is still upright, and a 180° turn between the
    // two landscapes resumes nothing, so onResume alone left the axes swapped or inverted
    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {}
        override fun onDisplayRemoved(displayId: Int) {}
        override fun onDisplayChanged(displayId: Int) {
            val display = display ?: return
            if (displayId == display.displayId) inputManager.setDeviceRotation(display.rotation)
        }
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (processInputEvents) {
            PhysicalMacroEngine.onMotionEvent(event)
        }
        if (processInputEvents && InputHandler.onMotionEvent(event)) {
            return true
        }

        return super.onGenericMotionEvent(event)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        HotkeyManager.onKeyEvent(event)

        // handhelds whose built-in controller reports the back button as part of the gamepad (AYN Odin 3: KEYCODE_BACK
        // from Vendor_2020_Product_0112) would never open the side menu if the emulated controller took it
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            return super.dispatchKeyEvent(event)
        }

        if (processInputEvents && PhysicalMacroEngine.onKeyEvent(event)) {
            return true
        }

        if (processInputEvents && InputHandler.onKeyEvent(event)) {
            return true
        }

        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE && event.isFromPhysicalController()) {
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    private fun getGamePath(): String {
        val extras = intent.extras
        val data = intent.data
        var launchPath: String? = null

        if (extras != null) {
            launchPath = extras.getString(EXTRA_LAUNCH_PATH)
        }

        if (launchPath == null && data != null) {
            launchPath = data.toString()
        }

        if (launchPath == null) {
            throw RuntimeException("launchPath is null")
        }

        return launchPath
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        inputManager = InputDelegateManager(this)

        setupHotkeys()
        setupPhysicalMacros()

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setFullscreen()

        val gamePath = getGamePath()

        setContent {
            TranslatableContent {
                ActivityContent(showBackground = false) {
                    EmulationScreen(
                        gamePath = gamePath,
                        setMotionSensorEnabled = inputManager::setDeviceMotionEnabled,
                        onCalibrateMotion = inputManager::calibrateMotion,
                        onResetMotionCalibration = inputManager::resetMotionCalibration,
                        onQuit = ::onQuit,
                        onRestartGame = ::restartGame,
                        setInputListeningEnabled = {
                            processInputEvents = it
                            if (!it) PhysicalMacroEngine.releaseAll()
                        },
                    )
                }
            }
        }
        warnIfCustomDriverFailed()
    }

    override fun onPause() {
        super.onPause()

        inputManager.onPause()
        getSystemService(DisplayManager::class.java).unregisterDisplayListener(displayListener)
    }

    override fun onResume() {
        super.onResume()

        inputManager.onResume(display.rotation)
        getSystemService(DisplayManager::class.java).registerDisplayListener(displayListener, null)
        updatePreferredRefreshRate()
    }

    /**
     * Frame generation presents 2 to 4 frames per game frame, and presenting waits for the display (FIFO). Without a
     * request, Android keeps many phones at 60 Hz for apps, so 2x on a 60 FPS game halved the game itself to 30 FPS
     * (Moto Edge 30 Ultra, 144 Hz panel, issue #9). While frame generation is on, ask for the display's highest refresh
     * rate at the current resolution; off, leave the choice to the system again.
     */
    fun updatePreferredRefreshRate() {
        val display = display ?: return
        val wanted = if (NativeFrameGen.isEnabled()) {
            val current = display.mode
            display.supportedModes
                .filter { it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight }
                .maxByOrNull { it.refreshRate }?.modeId ?: 0
        } else 0
        val attributes = window.attributes
        if (attributes.preferredDisplayModeId != wanted) {
            attributes.preferredDisplayModeId = wanted
            window.attributes = attributes
        }
    }

    /** The custom driver could not be loaded and the system driver is used: say so while playing. */
    private fun warnIfCustomDriverFailed() {
        lifecycleScope.launch {
            repeat(60) {
                delay(500)
                if (DriverGuard.loadFailed(this@EmulationActivity)) {
                    Toast.makeText(
                        this@EmulationActivity,
                        tr("The custom driver could not be loaded. Using the system driver."),
                        Toast.LENGTH_LONG,
                    ).show()
                    return@launch
                }
            }
        }
    }

    private fun setupHotkeys() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                AppSettingsStore.dataStore.data.map { it.hotkeySettings }
                    .distinctUntilChanged()
                    .collect { HotkeyManager.setHotkeyMappings(it) }
            }
        }
    }

    private fun setupPhysicalMacros() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                PhysicalMacroStore.get(this@EmulationActivity).data.map { it.macros }
                    .distinctUntilChanged()
                    .collect { PhysicalMacroEngine.setMacros(it) }
            }
        }
    }

    private fun setFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    private fun onQuit() {
        finish()
        exitProcess(0)
    }

    /** Boots the game again in a new process, so settings that only apply at boot take effect. */
    private fun restartGame() {
        GameRestartActivity.restart(this, intent)
        finish()
        exitProcess(0)
    }

    companion object {
        const val EXTRA_LAUNCH_PATH: String = BuildConfig.APPLICATION_ID + ".LaunchPath"
    }
}
