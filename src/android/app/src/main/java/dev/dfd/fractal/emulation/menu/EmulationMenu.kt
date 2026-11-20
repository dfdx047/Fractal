/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.emulation.menu

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.rememberCoroutineScope
import kotlinx.coroutines.launch
import dev.dfd.fractal.nativeinterface.NativeFrameGen.LosslessDllStatus
import dev.dfd.fractal.common.framegen.installLosslessDll
import dev.dfd.fractal.common.framegen.losslessDllStatusToString
import dev.dfd.fractal.emulation.rememberSecondDisplay
import dev.dfd.fractal.emulation.EmulationActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathBuilder
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.dfd.fractal.common.framegen.FrameGenFlowScaleChoices
import dev.dfd.fractal.common.framegen.FrameGenMultiplierChoices
import dev.dfd.fractal.common.framegen.FrameGenTargetRateChoices
import dev.dfd.fractal.common.framegen.frameGenFlowScaleToString
import dev.dfd.fractal.common.framegen.frameGenMultiplierToString
import dev.dfd.fractal.common.framegen.frameGenRuntimeStatusToString
import dev.dfd.fractal.common.framegen.frameGenTargetRateToString
import dev.dfd.fractal.common.framegen.hasLosslessDll
import dev.dfd.fractal.common.ui.fractal.SierpinskiMark
import dev.dfd.fractal.common.ui.fractal.StatusChip
import dev.dfd.fractal.common.ui.fractal.focusRing
import dev.dfd.fractal.common.ui.fractal.gamepadConfirm
import dev.dfd.fractal.common.ui.icons.FractalIcons
import dev.dfd.fractal.common.ui.localization.tr
import dev.dfd.fractal.common.ui.theme.FractalTheme
import dev.dfd.fractal.common.ui.theme.SurfaceKind
import dev.dfd.fractal.common.ui.theme.fractalSurface
import dev.dfd.fractal.emulation.DisplayOptions
import dev.dfd.fractal.emulation.SideMenuState
import dev.dfd.fractal.emulation.inputoverlay.ui.OverlayChip
import dev.dfd.fractal.emulation.inputoverlay.ui.OverlaySliderRow
import dev.dfd.fractal.nativeinterface.NativeFrameGen
import dev.dfd.fractal.nativeinterface.NativeSettings
import kotlinx.coroutines.delay
import kotlin.math.roundToInt
import androidx.compose.runtime.collectAsState
import dev.dfd.fractal.common.settings.AppSettingsStore
import dev.dfd.fractal.common.settings.HudAnchor
import dev.dfd.fractal.common.settings.HudItem
import dev.dfd.fractal.common.settings.PerformanceHudSettings
import dev.dfd.fractal.common.settings.performanceHudSettings
import dev.dfd.fractal.common.settings.updatePerformanceHud
import dev.dfd.fractal.common.ui.localization.hudAnchorName
import dev.dfd.fractal.common.ui.localization.hudItemName
import dev.dfd.fractal.common.ui.localization.controllerTypeToString
import dev.dfd.fractal.nativeinterface.NativeInput
import kotlinx.coroutines.flow.map

/** What the in-game menu can ask the emulation screen to do. */
interface EmulationMenuActions {
    fun resume()
    fun setPaused(paused: Boolean)
    fun takeScreenshot()
    fun quit()
    fun updateMenuState(transform: (SideMenuState) -> SideMenuState)
    fun updateDisplay(transform: (DisplayOptions) -> DisplayOptions)
    fun editOverlay()
    fun overlayProfiles()
    fun overlayStyle()
    fun resetOverlay()
    fun controllerSettings()
    fun hotkeys()
    fun emulatedUsbDevices()
    fun gameSettings()
    fun gamePacks()
    fun calibrateMotion()
    fun resetMotionCalibration()
}

val ScalingFilterChoices = listOf(
    NativeSettings.ScalingFilter.BILINEAR_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER,
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER,
)

/** FSR and GSR only make sense when the image gets bigger. */
val UpscalingFilterChoices = ScalingFilterChoices + listOf(
    NativeSettings.ScalingFilter.FSR_FILTER,
    NativeSettings.ScalingFilter.SGSR_FILTER,
    NativeSettings.ScalingFilter.SGSR_EDGE_FILTER,
)

private val SharpenedUpscalingFilters = setOf(
    NativeSettings.ScalingFilter.FSR_FILTER,
    NativeSettings.ScalingFilter.SGSR_FILTER,
    NativeSettings.ScalingFilter.SGSR_EDGE_FILTER,
)

fun scalingFilterName(filter: Int): String = when (filter) {
    NativeSettings.ScalingFilter.BILINEAR_FILTER -> tr("Bilinear")
    NativeSettings.ScalingFilter.BICUBIC_FILTER -> tr("Bicubic")
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER -> tr("Hermite")
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER -> tr("Nearest neighbor")
    NativeSettings.ScalingFilter.FSR_FILTER -> tr("AMD FSR 1")
    NativeSettings.ScalingFilter.SGSR_FILTER -> tr("Snapdragon GSR")
    NativeSettings.ScalingFilter.SGSR_EDGE_FILTER -> tr("Snapdragon GSR EdgeDir")
    else -> filter.toString()
}

/** Background of the menu panel: opaque enough to read over any game frame. */
@Composable
fun emulationMenuColor(): Color {
    val tokens = FractalTheme.tokens
    return when (tokens.surface.kind) {
        SurfaceKind.GLASS, SurfaceKind.DARK_GLASS -> tokens.colors.background.copy(alpha = 0.95f)
        else -> tokens.surface.fill.copy(alpha = 0.97f)
    }
}

/** Content of the in-game side menu, in the look of the app theme. */
@Composable
fun EmulationMenuContent(
    isOpen: Boolean,
    state: SideMenuState,
    display: DisplayOptions,
    actions: EmulationMenuActions,
) {
    val colors = FractalTheme.colors
    val firstItem = remember { FocusRequester() }
    LaunchedEffect(isOpen) {
        // gamepad users start on the first entry
        if (isOpen) runCatching { firstItem.requestFocus() }
    }
    Column(
        Modifier
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 14.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(horizontal = 4.dp)) {
            SierpinskiMark(Modifier.size(26.dp))
            Spacer(Modifier.width(10.dp))
            Text(
                tr("Game menu"),
                style = MaterialTheme.typography.titleLarge,
                color = colors.text,
                modifier = Modifier.weight(1f),
            )
            if (state.isPaused) StatusChip(tr("Paused"), icon = MenuIcons.Pause, highlight = true)
        }

        // the tiles share their height: "Captura de tela" and "Despausar" take two lines in a narrow menu
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(top = 4.dp).height(IntrinsicSize.Min)) {
            QuickTile(FractalIcons.Play, tr("Resume"), actions::resume, primary = true, modifier = Modifier.focusRequester(firstItem))
            if (state.isPaused) {
                QuickTile(FractalIcons.Play, tr("Unpause"), { actions.setPaused(false) })
            } else {
                QuickTile(MenuIcons.Pause, tr("Pause"), { actions.setPaused(true) })
            }
            QuickTile(MenuIcons.Camera, tr("Screenshot"), actions::takeScreenshot)
            QuickTile(MenuIcons.Exit, tr("Exit"), actions::quit)
        }

        MenuActionRow(
            icon = FractalIcons.Tune,
            title = tr("Game settings"),
            description = tr("This game's profile: CPU, driver, graphic packs and controllers. The game pauses while it is open"),
            onClick = actions::gameSettings,
        )
        MenuActionRow(
            icon = FractalIcons.Layers,
            title = tr("Graphic packs and cheats"),
            description = tr("Turn this game's packs, mods and cheats on or off. Most apply right away; resolution packs need a restart"),
            onClick = actions::gamePacks,
        )

        MenuSection(tr("Display"))
        MenuToggleRow(
            icon = FractalIcons.Display,
            title = tr("Show GamePad screen"),
            description = tr("Shows the GamePad screen next to the TV"),
            checked = state.isPadVisible,
            onCheckedChange = { on -> actions.updateMenuState { it.copy(isPadVisible = on) } },
        )
        if (rememberSecondDisplay() != null) {
            MenuToggleRow(
                icon = FractalIcons.Display,
                title = tr("GamePad on the second screen"),
                description = tr("Shows the GamePad screen on the other display (dual screen handhelds, external monitor)"),
                checked = state.isPadOnSecondScreen,
                onCheckedChange = { on -> actions.updateMenuState { it.copy(isPadOnSecondScreen = on) } },
            )
        }
        if (state.isPadVisible) {
            MenuChoiceRow(
                title = tr("GamePad screen scaling"),
                choices = listOf(0 to tr("Keep aspect ratio"), 1 to tr("Stretch")),
                selected = state.padScaling,
                onSelect = { value -> actions.updateMenuState { it.copy(padScaling = value) } },
            )
            OverlaySliderRow(
                title = tr("GamePad screen size"),
                value = state.padSizePercent.toFloat(),
                valueRange = 25f..100f,
                steps = 14,
                valueText = { "${it.roundToInt()}%" },
                onValueChange = { v -> actions.updateMenuState { it.copy(padSizePercent = v.roundToInt()) } },
                onValueChangeFinished = { v -> actions.updateMenuState { it.copy(padSizePercent = v.roundToInt()) } },
            )
        }
        MenuToggleRow(
            icon = MenuIcons.Swap,
            title = tr("Swap TV and GamePad"),
            description = tr("Shows the GamePad image on the main screen"),
            checked = state.isTVReplacedWithPad,
            onCheckedChange = { on -> actions.updateMenuState { it.copy(isTVReplacedWithPad = on) } },
        )
        MenuChoiceRow(
            title = tr("Fullscreen scaling"),
            choices = listOf(
                NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO to tr("Keep aspect ratio"),
                NativeSettings.FullscreenScaling.STRETCH to tr("Stretch"),
            ),
            selected = display.fullscreenScaling,
            onSelect = { value -> actions.updateDisplay { it.copy(fullscreenScaling = value) } },
        )
        MenuToggleRow(
            icon = FractalIcons.Stats,
            title = tr("Show FPS"),
            description = tr("Frame rate in the corner of the screen. More counters are in the overlay settings"),
            checked = display.showFps,
            onCheckedChange = { on -> actions.updateDisplay { it.copy(showFps = on) } },
        )

        PerformanceHudMenuSection()

        MenuSection(tr("Graphics"))
        MenuChoiceRow(
            title = tr("Upscale filter"),
            description = tr("Used when the game image is smaller than the screen"),
            choices = UpscalingFilterChoices.map { it to scalingFilterName(it) },
            selected = display.upscaleFilter,
            onSelect = { value -> actions.updateDisplay { it.copy(upscaleFilter = value) } },
        )
        if (display.upscaleFilter in SharpenedUpscalingFilters) {
            OverlaySliderRow(
                title = tr("FSR/GSR sharpness"),
                value = display.upscaleSharpness.toFloat(),
                valueRange = 0f..100f,
                steps = 19,
                valueText = { "${it.roundToInt()}%" },
                onValueChange = { v -> actions.updateDisplay { it.copy(upscaleSharpness = v.roundToInt()) } },
                onValueChangeFinished = { v -> actions.updateDisplay { it.copy(upscaleSharpness = v.roundToInt()) } },
            )
        }
        MenuChoiceRow(
            title = tr("Downscale filter"),
            description = tr("Used when the game image is bigger than the screen"),
            choices = ScalingFilterChoices.map { it to scalingFilterName(it) },
            selected = display.downscaleFilter,
            onSelect = { value -> actions.updateDisplay { it.copy(downscaleFilter = value) } },
        )
        MenuChoiceRow(
            title = tr("VSync"),
            choices = listOf(
                NativeSettings.VSyncMode.DOUBLE_BUFFERING to tr("Double buffering"),
                NativeSettings.VSyncMode.TRIPLE_BUFFERING to tr("Triple buffering"),
            ),
            // off is the same queue as double buffering on Android
            selected = if (display.vsyncMode == NativeSettings.VSyncMode.OFF) NativeSettings.VSyncMode.DOUBLE_BUFFERING else display.vsyncMode,
            onSelect = { value -> actions.updateDisplay { it.copy(vsyncMode = value) } },
        )
        MenuHint(tr("Filters and VSync apply right away and are kept for every game. Resolution and other graphic packs are set in the game's settings in the library and apply the next time the game starts."))

        FrameGenMenuSection(isOpen)

        MenuSection(tr("Controls"))
        val touchSlot by remember { AppSettingsStore.dataStore.data.map { it.inputOverlaySettings.controllerIndex } }
            .collectAsState(initial = null)
        // which emulated controller the touch controls drive, read again each time the menu opens
        val touchSlotDescription = remember(isOpen, touchSlot) {
            touchSlot?.let { slot ->
                val type = runCatching {
                    if (NativeInput.isControllerDisabled(slot)) null else controllerTypeToString(NativeInput.getControllerType(slot))
                }.getOrNull() ?: tr("Disabled")
                tr("They drive player {0}: {1}", slot + 1, type)
            }
        }
        MenuToggleRow(
            icon = FractalIcons.TouchControls,
            title = tr("Show on-screen controls"),
            description = touchSlotDescription,
            checked = state.isInputOverlayVisible,
            onCheckedChange = { on -> actions.updateMenuState { it.copy(isInputOverlayVisible = on) } },
        )
        MenuActionRow(
            icon = MenuIcons.Move,
            title = tr("Edit on-screen controls"),
            description = tr("Move, resize, add macros and shortcuts, per button opacity and shape"),
            enabled = state.isInputOverlayVisible,
            onClick = actions::editOverlay,
        )
        MenuActionRow(
            icon = FractalIcons.Palette,
            title = tr("Look of the on-screen controls"),
            description = tr("Style, colors, opacity and size"),
            enabled = state.isInputOverlayVisible,
            onClick = actions::overlayStyle,
        )
        MenuActionRow(
            icon = FractalIcons.Layers,
            title = tr("Input overlay profiles"),
            enabled = state.isInputOverlayVisible,
            onClick = actions::overlayProfiles,
        )
        MenuActionRow(
            icon = FractalIcons.Refresh,
            title = tr("Reset input overlay"),
            enabled = state.isInputOverlayVisible,
            onClick = actions::resetOverlay,
        )
        MenuActionRow(
            icon = FractalIcons.Gamepad,
            title = tr("Controller mapping"),
            description = tr("Emulated controller type and button mapping"),
            onClick = actions::controllerSettings,
        )
        MenuActionRow(
            icon = FractalIcons.Keyboard,
            title = tr("Hotkeys"),
            description = tr("Controller button combos for emulator actions"),
            onClick = actions::hotkeys,
        )
        MenuToggleRow(
            icon = MenuIcons.Motion,
            title = tr("Enable motion"),
            description = tr("Uses the device's motion sensors for the GamePad gyro"),
            checked = state.isMotionEnabled,
            onCheckedChange = { on -> actions.updateMenuState { it.copy(isMotionEnabled = on) } },
        )
        if (state.isMotionEnabled) {
            MenuActionRow(
                icon = MenuIcons.Motion,
                title = tr("Calibrate gyro"),
                description = tr("Hold the device the way you play and tap: in that position, games that aim by tilting the GamePad (Splatoon) aim straight ahead"),
                onClick = actions::calibrateMotion,
            )
            MenuActionRow(
                icon = MenuIcons.Motion,
                title = tr("Reset gyro calibration"),
                description = tr("The device held upright is the GamePad held upright again"),
                onClick = actions::resetMotionCalibration,
            )
        }

        MenuSection(tr("Devices"))
        MenuActionRow(
            icon = FractalIcons.Usb,
            title = tr("Emulated USB devices"),
            onClick = actions::emulatedUsbDevices,
        )
        Spacer(Modifier.padding(bottom = 8.dp))
    }
}

/**
 * The performance HUD (CPU, GPU, RAM, temperatures, power, FPS). The same settings as the overlay settings screen;
 * changes show on the HUD under the menu right away.
 */
@Composable
private fun PerformanceHudMenuSection() {
    val scope = rememberCoroutineScope()
    val stored by remember { performanceHudSettings() }.collectAsState(initial = null)
    val settings = stored ?: return
    fun update(transform: (PerformanceHudSettings) -> PerformanceHudSettings) {
        scope.launch { updatePerformanceHud(transform) }
    }

    MenuSection(tr("Performance HUD"))
    MenuToggleRow(
        icon = FractalIcons.Stats,
        title = tr("Show performance HUD"),
        description = tr("FPS, frame time, CPU, GPU, RAM, temperatures, power and Fractal's own statistics over the game. Drag it with a finger to move it"),
        checked = settings.enabled,
        onCheckedChange = { on -> update { it.copy(enabled = on) } },
    )
    if (!settings.enabled) return
    MenuChipGroup(title = tr("Shown items")) {
        for (item in HudItem.entries) {
            OverlayChip(hudItemName(item), settings.shows(item), { update { it.withItem(item, !it.shows(item)) } })
        }
    }
    MenuChoiceRow(
        title = tr("Layout"),
        choices = listOf(0 to tr("Row"), 1 to tr("Column")),
        selected = if (settings.vertical) 1 else 0,
        onSelect = { value -> update { it.copy(vertical = value == 1) } },
    )
    OverlaySliderRow(
        title = tr("Size"),
        value = settings.sizePercent.toFloat(),
        valueRange = PerformanceHudSettings.SIZE_MIN.toFloat()..PerformanceHudSettings.SIZE_MAX.toFloat(),
        steps = HUD_SIZE_STEPS,
        valueText = { "${it.roundToInt()}%" },
        onValueChangeFinished = { v -> update { it.copy(sizePercent = v.roundToInt()) } },
    )
    OverlaySliderRow(
        title = tr("Background opacity"),
        value = settings.opacityPercent.toFloat(),
        valueRange = PerformanceHudSettings.OPACITY_MIN.toFloat()..PerformanceHudSettings.OPACITY_MAX.toFloat(),
        steps = HUD_OPACITY_STEPS,
        valueText = { "${it.roundToInt()}%" },
        onValueChangeFinished = { v -> update { it.copy(opacityPercent = v.roundToInt()) } },
    )
    MenuChipGroup(title = tr("Position"), description = tr("Or drag the HUD over the game")) {
        for (anchor in HudAnchor.entries) {
            OverlayChip(hudAnchorName(anchor), settings.anchor == anchor, { update { it.at(anchor) } })
        }
    }
    MenuActionRow(
        icon = FractalIcons.Refresh,
        title = tr("Reset position"),
        onClick = { update { it.at(PerformanceHudSettings.DEFAULT_ANCHOR) } },
    )
}

/** 50% to 200% in steps of 10. */
private const val HUD_SIZE_STEPS = 14

/** 0% to 100% in steps of 5. */
private const val HUD_OPACITY_STEPS = 19

/** Lossless Scaling frame generation (ported from Cemu Plus). Changes apply to the running game and are saved. */
@Composable
private fun FrameGenMenuSection(isOpen: Boolean) {
    MenuSection(tr("Frame generation"))
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var isDllInstalled by remember(isOpen) { mutableStateOf(hasLosslessDll()) }
    var importMessage by remember { mutableStateOf<String?>(null) }
    // the DLL can be imported from here too; it is kept for every game, like from the settings
    val dllPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        importMessage = tr("Installing…")
        scope.launch {
            val status = installLosslessDll(context, uri)
            isDllInstalled = status == LosslessDllStatus.OK
            importMessage = if (isDllInstalled) tr("Lossless.dll installed")
            else tr("Could not install Lossless.dll: {0}", losslessDllStatusToString(status))
        }
    }
    if (!isDllInstalled) {
        MenuActionRow(
            icon = FractalIcons.Layers,
            title = tr("Import Lossless.dll"),
            description = importMessage ?: tr("From your own copy of Lossless Scaling. Used by every game"),
            onClick = { dllPicker.launch(arrayOf("*/*")) },
        )
        return
    }
    importMessage?.let { MenuHint(it) }

    val frameGenContext = LocalContext.current
    var enabled by remember { mutableStateOf(NativeFrameGen.isEnabled()) }
    var multiplier by remember { mutableIntStateOf(NativeFrameGen.getMultiplier()) }
    var targetRate by remember { mutableIntStateOf(NativeFrameGen.getTargetRate()) }
    var flowScale by remember { mutableIntStateOf(NativeFrameGen.getFlowScale()) }
    var runtimeStatus by remember { mutableIntStateOf(NativeFrameGen.getRuntimeStatus()) }

    LaunchedEffect(isOpen, enabled) {
        // only polled while the menu is open
        while (isOpen && enabled) {
            runtimeStatus = NativeFrameGen.getRuntimeStatus()
            delay(1000)
        }
    }

    MenuToggleRow(
        icon = FractalIcons.Layers,
        title = tr("Enable frame generation"),
        description = if (enabled) tr("Status: {0}", frameGenRuntimeStatusToString(runtimeStatus))
        else tr("TV screen only, forces VSync while it runs"),
        checked = enabled,
        onCheckedChange = { on ->
            enabled = on
            NativeFrameGen.setEnabled(on)
            NativeSettings.saveSettings()
            (frameGenContext as? EmulationActivity)?.updatePreferredRefreshRate()
        },
    )
    if (!enabled) return
    MenuChoiceRow(
        title = tr("Mode"),
        description = tr("Fixed multiplies every frame; adaptive generates what is missing to reach the target"),
        choices = FrameGenTargetRateChoices.map { it to frameGenTargetRateToString(it) },
        selected = targetRate,
        onSelect = { value ->
            targetRate = value
            NativeFrameGen.setTargetRate(value)
            NativeSettings.saveSettings()
        },
    )
    if (targetRate == 0) {
        MenuChoiceRow(
            title = tr("Frame multiplier"),
            choices = FrameGenMultiplierChoices.map { it to frameGenMultiplierToString(it) },
            selected = multiplier,
            onSelect = { value ->
                multiplier = value
                NativeFrameGen.setMultiplier(value)
                NativeSettings.saveSettings()
            },
        )
    }
    MenuChoiceRow(
        title = tr("Motion estimation resolution"),
        description = tr("Lower is cheaper on the GPU"),
        choices = FrameGenFlowScaleChoices.map { it to frameGenFlowScaleToString(it) },
        selected = flowScale,
        onSelect = { value ->
            flowScale = value
            NativeFrameGen.setFlowScale(value)
            NativeSettings.saveSettings()
        },
    )
}

@Composable
private fun MenuSection(text: String) {
    Text(
        text.uppercase(),
        style = MaterialTheme.typography.labelLarge,
        color = FractalTheme.colors.accent,
        modifier = Modifier.padding(start = 4.dp, top = 12.dp, bottom = 2.dp),
    )
}

@Composable
private fun MenuHint(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.bodySmall,
        color = FractalTheme.colors.textSecondary,
        modifier = Modifier.padding(horizontal = 4.dp),
    )
}

/** Card row with the theme's surface, focus ring and gamepad A. */
@Composable
private fun MenuCard(
    onClick: (() -> Unit)?,
    enabled: Boolean = true,
    role: Role? = null,
    content: @Composable RowScope.() -> Unit,
) {
    val colors = FractalTheme.colors
    val shape = FractalTheme.shapes.card
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val clickModifier = if (onClick != null) {
        Modifier
            .gamepadConfirm { if (enabled) onClick() }
            .clickable(interaction, ripple(), enabled = enabled, role = role, onClick = onClick)
    } else Modifier
    Row(
        Modifier
            .fillMaxWidth()
            .focusRing(focused, shape, gap = 2.dp)
            .clip(shape)
            .fractalSurface(shape, FractalTheme.surface, if (focused) colors.surfaceHigh else null)
            .then(clickModifier)
            .heightIn(min = 52.dp)
            .padding(horizontal = 12.dp, vertical = 8.dp)
            .alpha(if (enabled) 1f else 0.45f),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        content = content,
    )
}

@Composable
private fun RowScope.MenuRowText(title: String, description: String?) {
    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
        Text(title, style = MaterialTheme.typography.titleSmall, color = FractalTheme.colors.text)
        if (!description.isNullOrEmpty()) {
            Text(description, style = MaterialTheme.typography.bodySmall, color = FractalTheme.colors.textSecondary)
        }
    }
}

@Composable
private fun MenuIcon(icon: ImageVector) {
    Icon(icon, contentDescription = null, tint = FractalTheme.colors.accent, modifier = Modifier.size(22.dp))
}

@Composable
private fun MenuToggleRow(
    icon: ImageVector,
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    description: String? = null,
) {
    MenuCard(onClick = { onCheckedChange(!checked) }, role = Role.Switch) {
        MenuIcon(icon)
        MenuRowText(title, description)
        Switch(checked = checked, onCheckedChange = null)
    }
}

@Composable
private fun MenuActionRow(
    icon: ImageVector,
    title: String,
    onClick: () -> Unit,
    description: String? = null,
    enabled: Boolean = true,
) {
    MenuCard(onClick = onClick, enabled = enabled, role = Role.Button) {
        MenuIcon(icon)
        MenuRowText(title, description)
        Icon(FractalIcons.Next, contentDescription = null, tint = FractalTheme.colors.textSecondary, modifier = Modifier.size(16.dp))
    }
}

@Composable
private fun MenuChoiceRow(
    title: String,
    choices: List<Pair<Int, String>>,
    selected: Int,
    onSelect: (Int) -> Unit,
    description: String? = null,
) {
    MenuChipGroup(title, description) {
        for ((value, label) in choices) {
            OverlayChip(label, value == selected, { onSelect(value) })
        }
    }
}

/** Card with a title and a wrapping row of chips. */
@Composable
private fun MenuChipGroup(
    title: String,
    description: String? = null,
    chips: @Composable () -> Unit,
) {
    val shape = FractalTheme.shapes.card
    Column(
        Modifier
            .fillMaxWidth()
            .fractalSurface(shape, FractalTheme.surface)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(title, style = MaterialTheme.typography.titleSmall, color = FractalTheme.colors.text)
        if (description != null) {
            Text(description, style = MaterialTheme.typography.bodySmall, color = FractalTheme.colors.textSecondary)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            chips()
        }
    }
}

@Composable
private fun RowScope.QuickTile(
    icon: ImageVector,
    label: String,
    onClick: () -> Unit,
    primary: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val colors = FractalTheme.colors
    val shape = FractalTheme.shapes.button
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val background = if (primary) colors.accent else colors.surfaceHigh
    val content = if (primary) colors.onAccent else colors.text
    Column(
        modifier
            .weight(1f)
            .fillMaxHeight()
            .focusRing(focused, shape, gap = 2.dp)
            .clip(shape)
            .then(
                if (primary && colors.accentBrush != null) Modifier.background(colors.accentBrush)
                else Modifier.background(background)
            )
            .gamepadConfirm(onClick)
            .clickable(interaction, ripple(), role = Role.Button, onClick = onClick)
            .padding(horizontal = 4.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        Icon(icon, contentDescription = null, tint = content, modifier = Modifier.size(22.dp))
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = content,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
}

/** Menu icons missing from [FractalIcons], in the same stroke style. */
object MenuIcons {
    private fun icon(name: String, block: PathBuilder.() -> Unit): ImageVector =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f).apply {
            path(
                fill = null,
                stroke = SolidColor(Color.White),
                strokeLineWidth = 1.8f,
                strokeLineCap = StrokeCap.Square,
                strokeLineJoin = StrokeJoin.Miter,
                pathBuilder = block,
            )
        }.build()

    val Pause = icon("pause") {
        moveTo(8f, 5f); lineTo(8f, 19f)
        moveTo(16f, 5f); lineTo(16f, 19f)
    }

    val Camera = icon("camera") {
        moveTo(3f, 7.5f); lineTo(7.5f, 7.5f); lineTo(9f, 5f); lineTo(15f, 5f); lineTo(16.5f, 7.5f); lineTo(21f, 7.5f)
        lineTo(21f, 19f); lineTo(3f, 19f); close()
        moveTo(12f, 9.5f); lineTo(15f, 12.5f); lineTo(12f, 15.5f); lineTo(9f, 12.5f); close()
    }

    val Exit = icon("exit") {
        moveTo(13f, 4f); lineTo(4f, 4f); lineTo(4f, 20f); lineTo(13f, 20f)
        moveTo(9f, 12f); lineTo(20f, 12f)
        moveTo(16.5f, 8.5f); lineTo(20f, 12f); lineTo(16.5f, 15.5f)
    }

    val Swap = icon("swap") {
        moveTo(4f, 8f); lineTo(19f, 8f)
        moveTo(15.5f, 4.5f); lineTo(19f, 8f); lineTo(15.5f, 11.5f)
        moveTo(20f, 16f); lineTo(5f, 16f)
        moveTo(8.5f, 12.5f); lineTo(5f, 16f); lineTo(8.5f, 19.5f)
    }

    val Move = icon("move") {
        moveTo(12f, 3f); lineTo(12f, 21f)
        moveTo(3f, 12f); lineTo(21f, 12f)
        moveTo(9.5f, 5.5f); lineTo(12f, 3f); lineTo(14.5f, 5.5f)
        moveTo(9.5f, 18.5f); lineTo(12f, 21f); lineTo(14.5f, 18.5f)
        moveTo(5.5f, 9.5f); lineTo(3f, 12f); lineTo(5.5f, 14.5f)
        moveTo(18.5f, 9.5f); lineTo(21f, 12f); lineTo(18.5f, 14.5f)
    }

    val Motion = icon("motion") {
        moveTo(8f, 3.5f); lineTo(16f, 3.5f); lineTo(16f, 20.5f); lineTo(8f, 20.5f); close()
        moveTo(4f, 8f); lineTo(2.5f, 12f); lineTo(4f, 16f)
        moveTo(20f, 8f); lineTo(21.5f, 12f); lineTo(20f, 16f)
    }
}

/** Controller picker shown before the in-game controller mapping. */
@Composable
fun ColumnScope.ControllerChoiceRow(title: String, description: String, onClick: () -> Unit) {
    MenuCard(onClick = onClick, role = Role.Button) {
        MenuIcon(FractalIcons.Gamepad)
        MenuRowText(title, description)
        Icon(FractalIcons.Next, contentDescription = null, tint = FractalTheme.colors.textSecondary, modifier = Modifier.size(16.dp))
    }
}
