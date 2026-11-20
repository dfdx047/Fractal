package dev.dfd.fractal.emulation

import android.annotation.SuppressLint
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import dev.dfd.fractal.common.ui.fractal.dialogHints
import dev.dfd.fractal.common.ui.fractal.PadButton
import dev.dfd.fractal.common.ui.fractal.ButtonHint
import dev.dfd.fractal.common.ui.components.FractalAlertDialog
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.viewmodel.MutableCreationExtras
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.dfd.fractal.common.settings.GamePadPosition
import dev.dfd.fractal.common.settings.HotkeyAction
import dev.dfd.fractal.common.ui.extensions.showMessage
import dev.dfd.fractal.common.ui.localization.tr
import dev.dfd.fractal.emulation.emulatedusbdevices.EmulatedUSBDevicesDialog
import dev.dfd.fractal.emulation.input.ControllerTypeHint
import dev.dfd.fractal.emulation.input.HotkeyManager
import dev.dfd.fractal.emulation.input.InputApiUsage
import dev.dfd.fractal.settings.input.controller.AutoControllerMapping
import dev.dfd.fractal.common.ui.localization.controllerTypeToString
import kotlinx.coroutines.delay
import dev.dfd.fractal.common.settings.InputOverlaySettings
import dev.dfd.fractal.common.settings.performanceHudSettings
import dev.dfd.fractal.common.settings.updatePerformanceHud
import dev.dfd.fractal.emulation.hud.PerformanceHudOverlay
import dev.dfd.fractal.common.settings.OverlayControllerType
import dev.dfd.fractal.common.settings.activeProfile
import dev.dfd.fractal.common.settings.createProfile
import dev.dfd.fractal.common.settings.deleteProfile
import dev.dfd.fractal.common.settings.renameProfile
import dev.dfd.fractal.common.settings.selectProfile
import dev.dfd.fractal.emulation.inputoverlay.InputOverlayController
import dev.dfd.fractal.emulation.inputoverlay.InputOverlaySurface
import dev.dfd.fractal.emulation.inputoverlay.InputOverlayView.InputMode
import dev.dfd.fractal.emulation.inputoverlay.ui.OverlayEditor
import dev.dfd.fractal.emulation.inputoverlay.ui.OverlayEditorSession
import dev.dfd.fractal.common.settings.setInputRect
import dev.dfd.fractal.emulation.inputoverlay.ui.ConfirmDialog
import dev.dfd.fractal.emulation.inputoverlay.ui.OverlayAppearanceDialog
import dev.dfd.fractal.emulation.inputoverlay.ui.ProfilesDialog
import dev.dfd.fractal.emulation.inputoverlay.ui.displayName
import dev.dfd.fractal.nativeinterface.NativeEmulation
import kotlinx.coroutines.launch
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.platform.LocalContext
import dev.dfd.fractal.common.ui.theme.FractalTheme
import dev.dfd.fractal.emulation.inputoverlay.styleKey
import dev.dfd.fractal.emulation.inputoverlay.ui.overlayPaletteFor
import dev.dfd.fractal.emulation.menu.EmulationMenuActions
import dev.dfd.fractal.emulation.menu.EmulationMenuContent
import dev.dfd.fractal.emulation.menu.EmulationPanel
import dev.dfd.fractal.emulation.menu.EmulationPanels
import dev.dfd.fractal.emulation.menu.SurfaceRef
import dev.dfd.fractal.emulation.menu.UpscalingFilterChoices
import dev.dfd.fractal.emulation.menu.emulationMenuColor
import dev.dfd.fractal.emulation.menu.scalingFilterName
import dev.dfd.fractal.nativeinterface.NativeSettings

@Composable
fun EmulationScreen(
    gamePath: String,
    setMotionSensorEnabled: (Boolean) -> Unit,
    onCalibrateMotion: () -> Boolean = { false },
    onResetMotionCalibration: () -> Unit = {},
    setInputListeningEnabled: (Boolean) -> Unit,
    onQuit: () -> Unit,
    onRestartGame: () -> Unit = {},
    viewModel: EmulationViewModel = viewModel(
        factory = EmulationViewModel.Factory, extras = MutableCreationExtras().apply {
            set(EmulationViewModel.LAUNCH_PATH_KEY, gamePath)
        }),
) {
    val snackbarHostState = remember { SnackbarHostState() }
    val drawerState = rememberDrawerState(initialValue = DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    var showQuitConfirmationDialog by remember { mutableStateOf(false) }
    var editorSession by remember { mutableStateOf<OverlayEditorSession?>(null) }
    var editorTesting by remember { mutableStateOf(false) }
    var pausedBeforeEditing by remember { mutableStateOf(false) }
    var pausedBeforeGameSettings by remember { mutableStateOf(false) }
    var showEmulatedUSBDevices by remember { mutableStateOf(false) }
    val overlayController = remember { InputOverlayController() }
    var overlayDialog by remember { mutableStateOf<OverlayDialogState?>(null) }
    var panel by remember { mutableStateOf<EmulationPanel?>(null) }
    val mainSurface = remember { SurfaceRef() }

    val emulationError by viewModel.emulationError.collectAsState()
    val isEmulationInitialized by viewModel.isEmulationInitialized.collectAsState()
    val sideMenuState by viewModel.sideMenuState.collectAsState()
    val displayOptions by viewModel.displayOptions.collectAsState()
    val gamePadPosition by viewModel.gamePadPosition.collectAsState()
    val isInputOverlayVisible by viewModel.isInputOverlayVisible.collectAsState()
    val inputOverlaySettings by viewModel.inputOverlaySettings.collectAsState()
    val performanceHud by remember { performanceHudSettings() }.collectAsState(initial = null)


    fun closeDrawer() {
        scope.launch {
            drawerState.close()
        }
    }

    suspend fun toggleMenu() {
        drawerState.apply {
            if (isClosed) {
                open()
            } else {
                close()
            }
        }
    }

    fun applyMenuState(transform: (SideMenuState) -> SideMenuState) {
        val old = viewModel.sideMenuState.value
        val new = transform(old)
        if (new == old) return
        viewModel.updateSideMenuState(new)
        if (new.isMotionEnabled != old.isMotionEnabled) setMotionSensorEnabled(new.isMotionEnabled)
        if (new.isTVReplacedWithPad != old.isTVReplacedWithPad) NativeEmulation.setReplaceTVWithPadView(new.isTVReplacedWithPad)
    }

    fun captureScreenshot() {
        scope.launch {
            val view = mainSurface.view
            val bitmap = view?.let { captureSurface(it) }
            val uri = bitmap?.let { saveScreenshot(context, it) }
            snackbarHostState.showMessage(
                scope,
                if (uri != null) tr("Screenshot saved to Pictures/Fractal") else tr("Could not take a screenshot"),
            )
        }
    }

    fun setPaused(paused: Boolean) {
        viewModel.setPaused(paused)
    }

    /** Runs a hotkey or shortcut button action. */
    suspend fun perform(action: HotkeyAction) {
        // full screen panels take the controller; their own dialogs must not be covered
        if (panel != null) return
        when (action) {
            HotkeyAction.QUIT -> showQuitConfirmationDialog = true
            HotkeyAction.TOGGLE_MENU -> toggleMenu()
            HotkeyAction.SHOW_EMULATED_USB_DEVICES_DIALOG -> showEmulatedUSBDevices = true
            HotkeyAction.TOGGLE_PAUSE -> {
                val paused = !viewModel.sideMenuState.value.isPaused
                setPaused(paused)
                snackbarHostState.showMessage(scope, if (paused) tr("Paused") else tr("Resumed"))
            }

            HotkeyAction.TAKE_SCREENSHOT -> captureScreenshot()
            HotkeyAction.SWAP_TV_AND_PAD -> applyMenuState { it.copy(isTVReplacedWithPad = !it.isTVReplacedWithPad) }
            HotkeyAction.TOGGLE_PAD_SCREEN -> applyMenuState { it.copy(isPadVisible = !it.isPadVisible) }
            HotkeyAction.TOGGLE_INPUT_OVERLAY -> applyMenuState { it.copy(isInputOverlayVisible = !it.isInputOverlayVisible) }
            HotkeyAction.TOGGLE_MOTION -> {
                applyMenuState { it.copy(isMotionEnabled = !it.isMotionEnabled) }
                snackbarHostState.showMessage(
                    scope,
                    if (viewModel.sideMenuState.value.isMotionEnabled) tr("Motion enabled") else tr("Motion disabled"),
                )
            }

            HotkeyAction.CALIBRATE_MOTION -> {
                if (!viewModel.sideMenuState.value.isMotionEnabled) applyMenuState { it.copy(isMotionEnabled = true) }
                snackbarHostState.showMessage(
                    scope,
                    if (onCalibrateMotion()) tr("Gyro calibrated") else tr("Hold the device still and try again"),
                )
            }

            HotkeyAction.TOGGLE_FPS_OVERLAY -> {
                viewModel.updateDisplayOptions { it.copy(showFps = !it.showFps) }
                viewModel.saveDisplayOptions()
            }

            HotkeyAction.TOGGLE_ASPECT_RATIO -> {
                viewModel.updateDisplayOptions {
                    it.copy(
                        fullscreenScaling = if (it.fullscreenScaling == NativeSettings.FullscreenScaling.STRETCH)
                            NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO else NativeSettings.FullscreenScaling.STRETCH
                    )
                }
                viewModel.saveDisplayOptions()
                snackbarHostState.showMessage(
                    scope,
                    if (viewModel.displayOptions.value.fullscreenScaling == NativeSettings.FullscreenScaling.STRETCH)
                        tr("Stretch") else tr("Keep aspect ratio"),
                )
            }

            HotkeyAction.CYCLE_UPSCALE_FILTER -> {
                viewModel.updateDisplayOptions {
                    val index = UpscalingFilterChoices.indexOf(it.upscaleFilter)
                    it.copy(upscaleFilter = UpscalingFilterChoices[(index + 1).mod(UpscalingFilterChoices.size)])
                }
                viewModel.saveDisplayOptions()
                snackbarHostState.showMessage(
                    scope,
                    tr("Upscale filter: {0}", scalingFilterName(viewModel.displayOptions.value.upscaleFilter)),
                )
            }
        }
    }

    /** Opens the editor of the on-screen controls over the game. */
    fun startEditing() {
        val settings = viewModel.inputOverlaySettings.value
        editorSession = OverlayEditorSession(settings)
        editorTesting = false
        pausedBeforeEditing = viewModel.sideMenuState.value.isPaused
        if (settings.pauseWhileEditing) viewModel.setPaused(true)
    }

    fun finishEditing(saved: InputOverlaySettings?) {
        if (saved != null) {
            viewModel.updateInputOverlaySettings { saved }
            snackbarHostState.showMessage(scope, tr("Layout saved"))
        }
        overlayController.select(null)
        editorSession = null
        editorTesting = false
        viewModel.setPaused(pausedBeforeEditing)
    }

    BackHandler {
        if (drawerState.isAnimationRunning) {
            return@BackHandler
        }

        scope.launch {
            toggleMenu()
        }
    }

    // while editing, back leaves the editor (the editor asks before dropping changes)
    BackHandler(enabled = editorSession != null && drawerState.isClosed) {
        val session = editorSession ?: return@BackHandler
        if (session.isDirty) {
            overlayDialog = OverlayDialogState.ConfirmDiscardEdits
        } else {
            finishEditing(null)
        }
    }

    LaunchedEffect(drawerState.isClosed) {
        setInputListeningEnabled(drawerState.isClosed)
        if (drawerState.isClosed) {
            viewModel.saveDisplayOptions()
        } else {
            viewModel.refreshDisplayOptions()
        }
    }

    LaunchedEffect(Unit) {
        HotkeyManager.actions.collect { action -> perform(action) }
    }

    // a game well below full speed that has a lower resolution in its graphic packs: say so once
    LaunchedEffect(isEmulationInitialized) {
        if (!isEmulationInitialized) return@LaunchedEffect
        val titleId = runCatching { NativeEmulation.getForegroundTitleId() }.getOrDefault(0L)
        if (titleId == 0L || ResolutionHint.wasShown(context, titleId)) return@LaunchedEffect
        val suggestion = ResolutionHint.watch(titleId) {
            viewModel.sideMenuState.value.isPaused || editorSession != null
        } ?: return@LaunchedEffect
        ResolutionHint.markShown(context, titleId)
        snackbarHostState.currentSnackbarData?.dismiss()
        val result = snackbarHostState.showSnackbar(
            message = ResolutionHint.message(suggestion),
            actionLabel = ResolutionHint.action(suggestion),
            withDismissAction = true,
            duration = SnackbarDuration.Indefinite,
        )
        if (result != SnackbarResult.ActionPerformed) return@LaunchedEffect
        if (!ResolutionHint.apply(suggestion)) {
            snackbarHostState.showMessage(scope, tr("Couldn't change the resolution"))
            return@LaunchedEffect
        }
        val restart = snackbarHostState.showSnackbar(
            message = tr("{0} is set for this game: it takes effect when the game starts again. You can change it in the game's settings.", suggestion.resolution),
            actionLabel = tr("Restart now"),
            withDismissAction = true,
            duration = SnackbarDuration.Long,
        )
        if (restart == SnackbarResult.ActionPerformed) onRestartGame()
    }

    // the game reads the GamePad only (or never), and the player's input goes to the other kind: say so once
    LaunchedEffect(isEmulationInitialized) {
        if (!isEmulationInitialized) return@LaunchedEffect
        // the game profile slots are loaded now: one that only sets player 1's type gets the gamepad
        runCatching {
            AutoControllerMapping.showMappedToast(context, AutoControllerMapping.mapConnectedControllers(context))
        }
        overlayController.rebuild()
        var waited = 0L
        for (at in ControllerTypeHint.CHECK_DELAYS_MS) {
            delay(at - waited)
            waited = at
            val titleId = runCatching { NativeEmulation.getForegroundTitleId() }.getOrDefault(0L)
            if (titleId == 0L || ControllerTypeHint.wasShown(context, titleId)) return@LaunchedEffect
            if (viewModel.sideMenuState.value.isPaused || editorSession != null) continue
            val usage = InputApiUsage.read() ?: return@LaunchedEffect
            val overlaySlot = viewModel.inputOverlaySettings.value
                .takeIf { viewModel.sideMenuState.value.isInputOverlayVisible }?.controllerIndex
            val suggestion = ControllerTypeHint.check(usage, overlaySlot) ?: continue
            ControllerTypeHint.markShown(context, titleId)
            snackbarHostState.currentSnackbarData?.dismiss()
            val result = snackbarHostState.showSnackbar(
                message = suggestion.message,
                actionLabel = suggestion.action,
                withDismissAction = true,
                duration = SnackbarDuration.Indefinite,
            )
            if (result == SnackbarResult.ActionPerformed) {
                val ok = ControllerTypeHint.apply(context, suggestion, titleId)
                // the touch controls drive the controller that changed
                if (ok && overlaySlot != null && overlaySlot != 0) {
                    viewModel.updateInputOverlaySettings { it.copy(controllerIndex = 0) }
                }
                overlayController.rebuild()
                snackbarHostState.showMessage(
                    scope,
                    if (ok) tr(
                        "Controller 1 is now {0} in this game. You can change it in the game's settings.",
                        controllerTypeToString(suggestion.fix.controllerType),
                    )
                    else tr("Couldn't change the controller"),
                )
            }
            return@LaunchedEffect
        }
    }

    val menuActions = remember {
        object : EmulationMenuActions {
            override fun resume() {
                viewModel.setPaused(false)
                closeDrawer()
            }
            override fun setPaused(paused: Boolean) = viewModel.setPaused(paused)
            override fun takeScreenshot() = captureScreenshot()
            override fun quit() {
                showQuitConfirmationDialog = true
                closeDrawer()
            }

            override fun updateMenuState(transform: (SideMenuState) -> SideMenuState) = applyMenuState(transform)
            override fun updateDisplay(transform: (DisplayOptions) -> DisplayOptions) = viewModel.updateDisplayOptions(transform)
            override fun editOverlay() {
                startEditing()
                closeDrawer()
            }

            override fun overlayProfiles() {
                overlayDialog = OverlayDialogState.Profiles
                closeDrawer()
            }

            override fun overlayStyle() {
                overlayDialog = OverlayDialogState.Options
                closeDrawer()
            }

            override fun resetOverlay() {
                overlayDialog = OverlayDialogState.ConfirmReset
                closeDrawer()
            }

            override fun controllerSettings() {
                panel = EmulationPanel.ControllerPicker
            }

            override fun calibrateMotion() {
                snackbarHostState.showMessage(
                    scope,
                    if (onCalibrateMotion()) tr("Gyro calibrated") else tr("Hold the device still and try again"),
                )
            }

            override fun resetMotionCalibration() {
                onResetMotionCalibration()
                snackbarHostState.showMessage(scope, tr("Gyro calibration reset"))
            }

            override fun hotkeys() {
                panel = EmulationPanel.Hotkeys
                closeDrawer()
            }

            override fun emulatedUsbDevices() {
                showEmulatedUSBDevices = true
                closeDrawer()
            }

            override fun gameSettings() {
                val game = RunningGame.current(gamePath) ?: return
                // the game waits while its settings are open
                pausedBeforeGameSettings = viewModel.sideMenuState.value.isPaused
                viewModel.setPaused(true)
                panel = EmulationPanel.GameSettings(game)
                closeDrawer()
            }

            override fun gamePacks() {
                val game = RunningGame.current(gamePath) ?: return
                pausedBeforeGameSettings = viewModel.sideMenuState.value.isPaused
                viewModel.setPaused(true)
                panel = EmulationPanel.GamePacks(game)
                closeDrawer()
            }
        }
    }

    val menuShape = if (FractalTheme.shapes.card is CutCornerShape) {
        CutCornerShape(bottomEnd = 18.dp)
    } else {
        RoundedCornerShape(topEnd = 24.dp, bottomEnd = 24.dp)
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        gesturesEnabled = drawerState.isOpen,
        drawerContent = {
            ModalDrawerSheet(
                drawerShape = menuShape,
                drawerContainerColor = emulationMenuColor(),
                drawerContentColor = FractalTheme.colors.text,
            ) {
                EmulationMenuContent(
                    isOpen = drawerState.isOpen,
                    state = sideMenuState,
                    display = displayOptions,
                    actions = menuActions,
                )
            }
        },
    ) {
        EmulationSurfaces(
            sideMenuState = sideMenuState,
            gamePadPosition = gamePadPosition,
            mainHolderCallback = viewModel.mainHolderCallback,
            padHolderCallback = viewModel.padHolderCallback,
            onInitializeEmulation = viewModel::initializeEmulation,
            onMainSurfaceView = { mainSurface.view = it },
            canShowPad = isEmulationInitialized && emulationError == null,
        )

        val session = editorSession
        val shownOverlaySettings = session?.working ?: inputOverlaySettings
        InputOverlaySurface(
            isVisible = isInputOverlayVisible || session != null,
            inputOverlaySettings = shownOverlaySettings,
            inputMode = if (session != null && !editorTesting) InputMode.EDIT else InputMode.DEFAULT,
            controller = overlayController,
            palette = overlayPaletteFor(shownOverlaySettings),
            onLegacyLayout = viewModel::migrateLegacyInputOverlayLayout,
            onShortcut = HotkeyManager::trigger,
            onInputMoved = { target, rect, portrait ->
                val type = overlayController.controllerType
                if (type != null) session?.update { it.setInputRect(type, target.styleKey, rect, portrait) }
            },
        )

        // above the on-screen controls, taking only the touches on itself; out of the way while they are edited
        val hud = performanceHud
        if (hud != null && hud.enabled && session == null && isEmulationInitialized && emulationError == null) {
            PerformanceHudOverlay(
                settings = hud,
                onMoved = { x, y -> scope.launch { updatePerformanceHud { it.copy(x = x, y = y) } } },
            )
        }

        if (session != null) {
            OverlayEditor(
                session = session,
                controller = overlayController,
                onSave = { finishEditing(it) },
                onCancel = { finishEditing(null) },
                testing = editorTesting,
                onTestingChange = { editorTesting = it },
                paused = sideMenuState.isPaused,
                onPausedChange = viewModel::setPaused,
            )
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(16.dp)
        )
    }

    emulationError?.let {
        EmulationErrorDialog(it, onQuit)
    }

    if (!isEmulationInitialized) {
        EmulationLoadingDialog()
    }

    if (showQuitConfirmationDialog) {
        EmulationQuitConfirmationDialog(
            onQuit = onQuit,
            onDismiss = { showQuitConfirmationDialog = false },
        )
    }

    if (showEmulatedUSBDevices) {
        EmulatedUSBDevicesDialog(
            onDismiss = { showEmulatedUSBDevices = false },
        )
    }

    EmulationPanels(
        panel = panel,
        onPanelChange = { next ->
            if (next == null || next is EmulationPanel.Controller || next == EmulationPanel.Hotkeys) closeDrawer()
            if (next == null && (panel is EmulationPanel.GameSettings || panel is EmulationPanel.GamePacks) && !pausedBeforeGameSettings) viewModel.setPaused(false)
            panel = next
        },
        onRestartGame = onRestartGame,
        onClosed = {
            // the panel's window took the key events: nothing pressed before stays held
            HotkeyManager.clearPressedKeys()
            // the emulated controller type may have changed
            overlayController.rebuild()
        },
    )

    overlayController.controllerType?.let { controllerType ->
        InputOverlayDialogs(
            state = overlayDialog,
            settings = inputOverlaySettings,
            controllerType = controllerType,
            controller = overlayController,
            onUpdate = viewModel::updateInputOverlaySettings,
            onReset = { viewModel.resetInputOverlayLayout(controllerType) },
            onDiscardEdits = { finishEditing(null) },
            onStateChange = { overlayDialog = it },
        )
    }

    EmulationTextInputDialog()
}

/** Overlay dialogs opened from the side menu. */
private sealed interface OverlayDialogState {
    data object Profiles : OverlayDialogState
    data object Options : OverlayDialogState
    data object ConfirmReset : OverlayDialogState
    data object ConfirmDiscardEdits : OverlayDialogState
}

@Composable
private fun InputOverlayDialogs(
    state: OverlayDialogState?,
    settings: InputOverlaySettings,
    controllerType: OverlayControllerType,
    controller: InputOverlayController,
    onUpdate: ((InputOverlaySettings) -> InputOverlaySettings) -> Unit,
    onReset: () -> Unit,
    onDiscardEdits: () -> Unit,
    onStateChange: (OverlayDialogState?) -> Unit,
) {
    val dismiss = { onStateChange(null) }
    when (state) {
        null -> {}
        OverlayDialogState.Profiles -> ProfilesDialog(
            settings = settings,
            controllerType = controllerType,
            onSelect = { profile -> onUpdate { it.selectProfile(controllerType, profile.id) } },
            onCreate = { name, copyFrom -> onUpdate { it.createProfile(controllerType, name, copyFrom) } },
            onRename = { profile, name -> onUpdate { it.renameProfile(profile.id, name) } },
            onDelete = { profile -> onUpdate { it.deleteProfile(controllerType, profile.id) } },
            onDismiss = dismiss,
        )

        OverlayDialogState.Options -> OverlayAppearanceDialog(
            settings = settings,
            onUpdate = onUpdate,
            onPreview = controller::previewAppearance,
            onDismiss = dismiss,
        )

        OverlayDialogState.ConfirmReset -> ConfirmDialog(
            title = tr("Back to the automatic layout"),
            message = tr(
                "Put every input of the profile \"{0}\" back on the layout made for this screen? Button options, macros and shortcuts are kept.",
                settings.activeProfile(controllerType).displayName(),
            ),
            confirmText = tr("Reset"),
            onConfirm = {
                onReset()
                dismiss()
            },
            onDismiss = dismiss,
        )

        OverlayDialogState.ConfirmDiscardEdits -> ConfirmDialog(
            title = tr("Discard changes"),
            message = tr("Leave the editor without saving the changes?"),
            confirmText = tr("Discard"),
            onConfirm = {
                dismiss()
                onDiscardEdits()
            },
            onDismiss = dismiss,
        )
    }
}

@Composable
private fun EmulationSurfaces(
    sideMenuState: SideMenuState,
    gamePadPosition: GamePadPosition?,
    mainHolderCallback: SurfaceHolder.Callback,
    padHolderCallback: SurfaceHolder.Callback,
    onInitializeEmulation: () -> Unit,
    onMainSurfaceView: (SurfaceView?) -> Unit = {},
    canShowPad: Boolean = true,
) {
    if (gamePadPosition == null) {
        return
    }

    val isVertical = gamePadPosition.isVertical()
    val appearsAfterTV = gamePadPosition.appearsAfterTV()
    val context = LocalContext.current
    val secondDisplay = rememberSecondDisplay()
    // the presentation goes away while the game is in the background and comes back with it: its surface is
    // released with the main one and has to be created again to be drawn to
    val lifecycleState by androidx.lifecycle.compose.LocalLifecycleOwner.current.lifecycle.currentStateFlow.collectAsState()
    val isStarted = lifecycleState.isAtLeast(androidx.lifecycle.Lifecycle.State.STARTED)
    // the GamePad surface comes once the game runs: it needs the renderer (the choice is remembered between games)
    val wantsPad = sideMenuState.isPadVisible && canShowPad
    val padOnSecondScreen = wantsPad && sideMenuState.isPadOnSecondScreen && secondDisplay != null
    val showPresentation = padOnSecondScreen && isStarted
    val isPadVisible = wantsPad && !padOnSecondScreen

    DisposableEffect(showPresentation, secondDisplay) {
        val display = secondDisplay
        if (!showPresentation || display == null) return@DisposableEffect onDispose {}
        val presentation = PadPresentation(context, display, padHolderCallback)
        val shown = runCatching { presentation.show() }.isSuccess
        onDispose { if (shown) presentation.dismiss() }
    }

    @Composable
    fun MainSurface(modifier: Modifier) {
        EmulationSurface(
            modifier = modifier,
            isTV = true,
            holderCallback = mainHolderCallback,
            afterInit = { onInitializeEmulation() },
            onView = onMainSurfaceView,
        )
    }

    @Composable
    fun PadSurface(modifier: Modifier) {
        if (isPadVisible) {
            EmulationSurface(
                modifier = modifier,
                isTV = false,
                holderCallback = padHolderCallback,
            )
        }
    }

    @Composable
    fun SurfacesInOrder(itemModifier: Modifier) {
        if (appearsAfterTV) {
            MainSurface(itemModifier)
            PadSurface(itemModifier)
        } else {
            PadSurface(itemModifier)
            MainSurface(itemModifier)
        }
    }

    LinearLayout(isVertical) { itemModifier ->
        SurfacesInOrder(itemModifier)
    }
}

@Composable
private fun LinearLayout(
    isVertical: Boolean,
    content: @Composable (Modifier) -> Unit,
) {
    if (isVertical) {
        Column(modifier = Modifier.fillMaxSize()) {
            content(Modifier.weight(1f))
        }
    } else {
        CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
            Row(modifier = Modifier.fillMaxSize()) {
                content(Modifier.weight(1f))
            }
        }
    }
}

@Composable
@SuppressLint("ClickableViewAccessibility")
private fun EmulationSurface(
    modifier: Modifier,
    isTV: Boolean,
    holderCallback: SurfaceHolder.Callback,
    afterInit: () -> Unit = {},
    onView: (SurfaceView?) -> Unit = {},
) {
    DisposableEffect(Unit) { onDispose { onView(null) } }
    AndroidView(
        modifier = modifier,
        factory = { context ->
            SurfaceView(context).apply {
                var firstChange = true

                setOnTouchListener(CanvasOnTouchListener(isTV))

                holder.addCallback(holderCallback)

                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceChanged(
                        holder: SurfaceHolder, format: Int, width: Int, height: Int
                    ) {
                        if (firstChange) {
                            afterInit()
                            firstChange = false
                        }
                    }

                    override fun surfaceCreated(holder: SurfaceHolder) {}

                    override fun surfaceDestroyed(holder: SurfaceHolder) {}
                })
                onView(this)
            }
        })
}

@Composable
private fun EmulationQuitConfirmationDialog(onQuit: () -> Unit, onDismiss: () -> Unit) {
    FractalAlertDialog(
        hints = dialogHints(confirm = tr("Yes"), cancel = tr("No")),
        title = { Text(tr("Exit confirmation")) },
        text = { Text(tr("Are you sure you want to exit?")) },
        confirmButton = { TextButton(onClick = onQuit) { Text(tr("Yes")) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(tr("No")) } },
        onDismissRequest = onDismiss,
    )
}

@Composable
private fun EmulationLoadingDialog() {
    FractalAlertDialog(
        title = { Text(tr("Initializing emulation")) },
        text = { LinearProgressIndicator(modifier = Modifier.fillMaxWidth()) },
        confirmButton = {},
        onDismissRequest = {},
        hints = emptyList(),
    )
}

@Composable
private fun EmulationErrorDialog(error: NativeError, onQuit: () -> Unit) {
    val errorMessage = remember(error) {
        when (error) {
            is NativeError.SystemInitializationError -> tr("Failed to initialize")

            is NativeError.RendererInitializationError ->
                tr("Failed creating renderer: {0}", error.message)

            is NativeError.SurfaceCreationError -> tr("Failed creating surface: {0}", error.message)

            NativeError.GameFilesNotFoundError -> tr("Unable to launch game because the base files were not found.")
            NativeError.NoDiscKeysError -> tr("Could not decrypt title. Make sure that keys.txt contains the correct disc key for this title.")
            NativeError.NoTitleTikError -> tr("Could not decrypt title because title.tik is missing.")
            is NativeError.UnknownTilePrepareError ->
                tr("Unable to launch game\nPath: {0}", error.launchPath)

            NativeError.LaunchingTitleError -> tr("Failed to launch title")
        }
    }
    FractalAlertDialog(
        hints = listOf(ButtonHint(PadButton.A, tr("Quit"))),
        title = { Text(tr("Error")) },
        text = { Text(errorMessage) },
        confirmButton = { TextButton(onClick = onQuit) { Text(tr("Quit")) } },
        onDismissRequest = {},
    )
}
