package dev.dfd.fractal.common.input

import android.view.KeyEvent
import dev.dfd.fractal.common.settings.ButtonBehavior
import dev.dfd.fractal.common.settings.MacroKind
import dev.dfd.fractal.common.settings.MacroStep
import dev.dfd.fractal.common.settings.OverlayMacroInput
import dev.dfd.fractal.common.settings.TURBO_MAX_HZ
import dev.dfd.fractal.common.settings.TURBO_MIN_HZ
import dev.dfd.fractal.nativeinterface.NativeInput
import dev.dfd.fractal.nativeinterface.NativeInput.EmulatedControllerType
import kotlinx.serialization.Serializable
import java.util.UUID

/**
 * A macro or turbo run by buttons of a physical controller, the counterpart of the on-screen
 * macros: the same [MacroKind], [MacroStep] and [ButtonBehavior], with a button combination as trigger.
 *
 * [trigger] holds Android key codes that must all be held on one controller. Analog triggers count
 * as L2/R2 and the hat as the D-pad keys (see [analogVirtualKeys]). A one key trigger is taken away
 * from the game (the key only runs the macro); keys of a combination still reach the game.
 */
@Serializable
data class PhysicalMacro(
    val id: String = UUID.randomUUID().toString(),
    val label: String = "",
    val enabled: Boolean = true,
    /** Emulated controller slot (0-7) the macro presses buttons on. */
    val controllerIndex: Int = 0,
    val trigger: Set<Int> = emptySet(),
    /** Descriptor of the controller the trigger must come from; null for any controller. */
    val deviceDescriptor: String? = null,
    /** Name of [deviceDescriptor], only for display. */
    val deviceName: String? = null,
    val kind: MacroKind = MacroKind.COMBO,
    val buttons: List<OverlayMacroInput> = emptyList(),
    val steps: List<MacroStep> = emptyList(),
    val behavior: ButtonBehavior = ButtonBehavior(),
) {
    val isUsable: Boolean
        get() = enabled && trigger.isNotEmpty() && when (kind) {
            MacroKind.COMBO -> buttons.isNotEmpty()
            MacroKind.SEQUENCE -> steps.isNotEmpty()
        }

    /** One key trigger: the key belongs to the macro and is not passed to the game. */
    val takesTriggerKey: Boolean get() = trigger.size == 1

    fun triggerMatches(descriptor: String, keyCode: Int): Boolean =
        trigger.size == 1 && keyCode in trigger && (deviceDescriptor == null || deviceDescriptor == descriptor)

    fun isTriggerHeld(heldByDevice: Map<String, Set<Int>>): Boolean {
        if (trigger.isEmpty()) return false
        if (deviceDescriptor != null) return heldByDevice[deviceDescriptor]?.containsAll(trigger) == true
        return heldByDevice.values.any { it.containsAll(trigger) }
    }
}

@Serializable
data class PhysicalMacroSettings(
    val macros: List<PhysicalMacro> = emptyList(),
)

/**
 * What one macro holds over time, without any Android or native call: the engine feeds trigger
 * changes and asks for the held buttons on each tick.
 *
 * COMBO holds its buttons while active; with turbo it presses and releases them [ButtonBehavior.turboHz]
 * times a second. SEQUENCE plays its steps once per press (to the end, even if the trigger is
 * released); with turbo it repeats while active. [ButtonBehavior.toggle] latches: one press turns
 * the macro on, the next one off.
 */
class MacroPlayback(val macro: PhysicalMacro) {
    private var triggerHeld = false
    private var active = false
    private var sequenceRunning = false
    private var startMs = 0L

    private val turbo get() = macro.behavior.turbo
    private val steps = macro.steps.map { it.copy(durationMs = it.durationMs.coerceIn(MacroStep.MIN_STEP_MS, MacroStep.MAX_STEP_MS)) }
    private val sequenceLengthMs = steps.sumOf { it.durationMs.toLong() }

    fun onTrigger(held: Boolean, nowMs: Long) {
        if (held == triggerHeld) return
        triggerHeld = held
        if (macro.behavior.toggle) {
            if (held) setActive(!active, nowMs)
        } else {
            setActive(held, nowMs)
        }
    }

    private fun setActive(on: Boolean, nowMs: Long) {
        if (on == active) return
        active = on
        if (macro.kind == MacroKind.SEQUENCE) {
            if (on) {
                startMs = nowMs
                sequenceRunning = sequenceLengthMs > 0
            } else if (turbo) {
                // a repeating sequence stops with its trigger; a single one plays to the end
                sequenceRunning = false
            }
        } else if (on) {
            startMs = nowMs
        }
    }

    /** Lets go of everything, e.g. when the game pauses or the controller disconnects. */
    fun release() {
        triggerHeld = false
        active = false
        sequenceRunning = false
    }

    /** Whether the held buttons change with time alone, so the engine has to keep ticking. */
    val needsTicks: Boolean
        get() = when (macro.kind) {
            MacroKind.COMBO -> active && turbo
            MacroKind.SEQUENCE -> sequenceRunning
        }

    fun buttonsAt(nowMs: Long): List<OverlayMacroInput> {
        val elapsed = (nowMs - startMs).coerceAtLeast(0)
        return when (macro.kind) {
            MacroKind.COMBO -> when {
                !active -> emptyList()
                !turbo -> macro.buttons
                isTurboPressed(elapsed) -> macro.buttons
                else -> emptyList()
            }

            MacroKind.SEQUENCE -> sequenceButtonsAt(elapsed)
        }
    }

    private fun isTurboPressed(elapsedMs: Long): Boolean {
        val periodMs = 1000.0 / macro.behavior.turboHz.coerceIn(TURBO_MIN_HZ, TURBO_MAX_HZ)
        return (elapsedMs % periodMs) < periodMs / 2
    }

    private fun sequenceButtonsAt(elapsedMs: Long): List<OverlayMacroInput> {
        if (!sequenceRunning) return emptyList()
        var t = elapsedMs
        if (turbo) {
            t %= sequenceLengthMs
        } else if (t >= sequenceLengthMs) {
            sequenceRunning = false
            return emptyList()
        }
        for (step in steps) {
            if (t < step.durationMs) return step.buttons
            t -= step.durationMs
        }
        return emptyList()
    }
}

/**
 * Records a trigger: the buttons held together on one controller, finished when they are all released.
 */
class TriggerRecorder {
    private val held = mutableSetOf<Int>()
    private val combo = linkedSetOf<Int>()

    var deviceDescriptor: String? = null
        private set

    /** Returns the finished trigger once every key is up again, else null. */
    fun onKey(descriptor: String, keyCode: Int, down: Boolean): Set<Int>? {
        if (deviceDescriptor == null) {
            if (!down) return null
            deviceDescriptor = descriptor
        } else if (descriptor != deviceDescriptor) {
            return null
        }
        if (down) {
            held += keyCode
            combo += keyCode
            return null
        }
        held -= keyCode
        return if (held.isEmpty() && combo.isNotEmpty()) combo.toSet() else null
    }
}

private const val ANALOG_PRESS_THRESHOLD = 0.5f

/** Key codes an analog trigger or hat counts as for macro triggers. */
fun analogVirtualKeys(leftTrigger: Float, rightTrigger: Float, hatX: Float, hatY: Float): Set<Int> = buildSet {
    if (leftTrigger > ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_BUTTON_L2)
    if (rightTrigger > ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_BUTTON_R2)
    if (hatX < -ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_DPAD_LEFT)
    if (hatX > ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_DPAD_RIGHT)
    if (hatY < -ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_DPAD_UP)
    if (hatY > ANALOG_PRESS_THRESHOLD) add(KeyEvent.KEYCODE_DPAD_DOWN)
}

/** Native mapping id of [input] on an emulated controller of [controllerType], or -1 when it has none. */
fun macroButtonToNative(controllerType: Int, input: OverlayMacroInput): Int = when (controllerType) {
    EmulatedControllerType.VPAD -> when (input) {
        OverlayMacroInput.A -> NativeInput.VPADButton.A
        OverlayMacroInput.B -> NativeInput.VPADButton.B
        OverlayMacroInput.X -> NativeInput.VPADButton.X
        OverlayMacroInput.Y -> NativeInput.VPADButton.Y
        OverlayMacroInput.L -> NativeInput.VPADButton.L
        OverlayMacroInput.R -> NativeInput.VPADButton.R
        OverlayMacroInput.ZL -> NativeInput.VPADButton.ZL
        OverlayMacroInput.ZR -> NativeInput.VPADButton.ZR
        OverlayMacroInput.PLUS -> NativeInput.VPADButton.PLUS
        OverlayMacroInput.MINUS -> NativeInput.VPADButton.MINUS
        OverlayMacroInput.HOME -> NativeInput.VPADButton.HOME
        OverlayMacroInput.DPAD_UP -> NativeInput.VPADButton.UP
        OverlayMacroInput.DPAD_DOWN -> NativeInput.VPADButton.DOWN
        OverlayMacroInput.DPAD_LEFT -> NativeInput.VPADButton.LEFT
        OverlayMacroInput.DPAD_RIGHT -> NativeInput.VPADButton.RIGHT
        OverlayMacroInput.L_STICK_CLICK -> NativeInput.VPADButton.STICKL
        OverlayMacroInput.R_STICK_CLICK -> NativeInput.VPADButton.STICKR
        OverlayMacroInput.BLOW_MIC -> NativeInput.VPADButton.MIC
        else -> -1
    }

    EmulatedControllerType.PRO -> when (input) {
        OverlayMacroInput.A -> NativeInput.ProButton.A
        OverlayMacroInput.B -> NativeInput.ProButton.B
        OverlayMacroInput.X -> NativeInput.ProButton.X
        OverlayMacroInput.Y -> NativeInput.ProButton.Y
        OverlayMacroInput.L -> NativeInput.ProButton.L
        OverlayMacroInput.R -> NativeInput.ProButton.R
        OverlayMacroInput.ZL -> NativeInput.ProButton.ZL
        OverlayMacroInput.ZR -> NativeInput.ProButton.ZR
        OverlayMacroInput.PLUS -> NativeInput.ProButton.PLUS
        OverlayMacroInput.MINUS -> NativeInput.ProButton.MINUS
        OverlayMacroInput.HOME -> NativeInput.ProButton.HOME
        OverlayMacroInput.DPAD_UP -> NativeInput.ProButton.UP
        OverlayMacroInput.DPAD_DOWN -> NativeInput.ProButton.DOWN
        OverlayMacroInput.DPAD_LEFT -> NativeInput.ProButton.LEFT
        OverlayMacroInput.DPAD_RIGHT -> NativeInput.ProButton.RIGHT
        OverlayMacroInput.L_STICK_CLICK -> NativeInput.ProButton.STICKL
        OverlayMacroInput.R_STICK_CLICK -> NativeInput.ProButton.STICKR
        else -> -1
    }

    EmulatedControllerType.CLASSIC -> when (input) {
        OverlayMacroInput.A -> NativeInput.ClassicButton.A
        OverlayMacroInput.B -> NativeInput.ClassicButton.B
        OverlayMacroInput.X -> NativeInput.ClassicButton.X
        OverlayMacroInput.Y -> NativeInput.ClassicButton.Y
        OverlayMacroInput.L -> NativeInput.ClassicButton.L
        OverlayMacroInput.R -> NativeInput.ClassicButton.R
        OverlayMacroInput.ZL -> NativeInput.ClassicButton.ZL
        OverlayMacroInput.ZR -> NativeInput.ClassicButton.ZR
        OverlayMacroInput.PLUS -> NativeInput.ClassicButton.PLUS
        OverlayMacroInput.MINUS -> NativeInput.ClassicButton.MINUS
        OverlayMacroInput.HOME -> NativeInput.ClassicButton.HOME
        OverlayMacroInput.DPAD_UP -> NativeInput.ClassicButton.UP
        OverlayMacroInput.DPAD_DOWN -> NativeInput.ClassicButton.DOWN
        OverlayMacroInput.DPAD_LEFT -> NativeInput.ClassicButton.LEFT
        OverlayMacroInput.DPAD_RIGHT -> NativeInput.ClassicButton.RIGHT
        else -> -1
    }

    EmulatedControllerType.WIIMOTE -> when (input) {
        OverlayMacroInput.A -> NativeInput.WiimoteButton.A
        OverlayMacroInput.B -> NativeInput.WiimoteButton.B
        OverlayMacroInput.ONE -> NativeInput.WiimoteButton.ONE
        OverlayMacroInput.TWO -> NativeInput.WiimoteButton.TWO
        OverlayMacroInput.C -> NativeInput.WiimoteButton.NUNCHUCK_C
        OverlayMacroInput.Z -> NativeInput.WiimoteButton.NUNCHUCK_Z
        OverlayMacroInput.PLUS -> NativeInput.WiimoteButton.PLUS
        OverlayMacroInput.MINUS -> NativeInput.WiimoteButton.MINUS
        OverlayMacroInput.HOME -> NativeInput.WiimoteButton.HOME
        OverlayMacroInput.DPAD_UP -> NativeInput.WiimoteButton.UP
        OverlayMacroInput.DPAD_DOWN -> NativeInput.WiimoteButton.DOWN
        OverlayMacroInput.DPAD_LEFT -> NativeInput.WiimoteButton.LEFT
        OverlayMacroInput.DPAD_RIGHT -> NativeInput.WiimoteButton.RIGHT
        else -> -1
    }

    else -> -1
}

/** Buttons a macro can press on an emulated controller of [controllerType]. */
fun macroButtonsFor(controllerType: Int): List<OverlayMacroInput> =
    OverlayMacroInput.entries.filter { macroButtonToNative(controllerType, it) != -1 }

/** Short name of a macro button for chips and lists. */
fun OverlayMacroInput.shortName(): String = when (this) {
    OverlayMacroInput.ONE -> "1"
    OverlayMacroInput.TWO -> "2"
    OverlayMacroInput.PLUS -> "+"
    OverlayMacroInput.MINUS -> "−"
    OverlayMacroInput.L_STICK_CLICK -> "L3"
    OverlayMacroInput.R_STICK_CLICK -> "R3"
    OverlayMacroInput.DPAD_UP -> "↑"
    OverlayMacroInput.DPAD_DOWN -> "↓"
    OverlayMacroInput.DPAD_LEFT -> "←"
    OverlayMacroInput.DPAD_RIGHT -> "→"
    OverlayMacroInput.BLOW_MIC -> "MIC"
    else -> name
}
