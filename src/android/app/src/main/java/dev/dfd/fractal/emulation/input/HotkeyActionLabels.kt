package dev.dfd.fractal.emulation.input

import dev.dfd.fractal.common.settings.HotkeyAction
import dev.dfd.fractal.common.ui.localization.tr

/** Name of [HotkeyAction] in the hotkey mapper and the shortcut button editor. */
fun HotkeyAction.displayName(): String = when (this) {
    HotkeyAction.QUIT -> tr("Quit")
    HotkeyAction.TOGGLE_MENU -> tr("Toggle menu")
    HotkeyAction.SHOW_EMULATED_USB_DEVICES_DIALOG -> tr("Show Emulated USB Devices dialog")
    HotkeyAction.TOGGLE_PAUSE -> tr("Pause / resume")
    HotkeyAction.TAKE_SCREENSHOT -> tr("Take screenshot")
    HotkeyAction.SWAP_TV_AND_PAD -> tr("Swap TV and GamePad")
    HotkeyAction.TOGGLE_PAD_SCREEN -> tr("Show / hide GamePad screen")
    HotkeyAction.TOGGLE_INPUT_OVERLAY -> tr("Show / hide on-screen controls")
    HotkeyAction.TOGGLE_FPS_OVERLAY -> tr("Show / hide FPS")
    HotkeyAction.TOGGLE_ASPECT_RATIO -> tr("Keep aspect ratio / stretch")
    HotkeyAction.CYCLE_UPSCALE_FILTER -> tr("Next upscale filter")
    HotkeyAction.TOGGLE_MOTION -> tr("Enable / disable motion")
    HotkeyAction.CALIBRATE_MOTION -> tr("Calibrate gyro")
}

/** Short text drawn on an on-screen shortcut button that has no label of its own. */
fun HotkeyAction.shortLabel(): String = when (this) {
    HotkeyAction.QUIT -> tr("Exit")
    HotkeyAction.TOGGLE_MENU -> tr("Menu")
    HotkeyAction.SHOW_EMULATED_USB_DEVICES_DIALOG -> tr("USB")
    HotkeyAction.TOGGLE_PAUSE -> tr("Pause")
    HotkeyAction.TAKE_SCREENSHOT -> tr("Shot")
    HotkeyAction.SWAP_TV_AND_PAD -> tr("TV/Pad")
    HotkeyAction.TOGGLE_PAD_SCREEN -> tr("Pad")
    HotkeyAction.TOGGLE_INPUT_OVERLAY -> tr("Hide")
    HotkeyAction.TOGGLE_FPS_OVERLAY -> tr("FPS")
    HotkeyAction.TOGGLE_ASPECT_RATIO -> tr("Aspect")
    HotkeyAction.CYCLE_UPSCALE_FILTER -> tr("Filter")
    HotkeyAction.TOGGLE_MOTION -> tr("Motion")
    HotkeyAction.CALIBRATE_MOTION -> tr("Gyro")
}
