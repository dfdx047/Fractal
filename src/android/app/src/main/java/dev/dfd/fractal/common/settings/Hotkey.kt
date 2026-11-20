package dev.dfd.fractal.common.settings

import kotlinx.serialization.Serializable

@Serializable
data class HotkeyCombo(
    val keys: Set<Int>
)

/**
 * Things a controller hotkey combo or an on-screen shortcut button can do while a game runs.
 * New values go at the end: the names are saved in the hotkey settings and overlay profiles.
 */
enum class HotkeyAction {
    QUIT,
    TOGGLE_MENU,
    SHOW_EMULATED_USB_DEVICES_DIALOG,
    TOGGLE_PAUSE,
    TAKE_SCREENSHOT,
    SWAP_TV_AND_PAD,
    TOGGLE_PAD_SCREEN,
    TOGGLE_INPUT_OVERLAY,
    TOGGLE_FPS_OVERLAY,
    TOGGLE_ASPECT_RATIO,
    CYCLE_UPSCALE_FILTER,
    TOGGLE_MOTION,
    CALIBRATE_MOTION,
}
