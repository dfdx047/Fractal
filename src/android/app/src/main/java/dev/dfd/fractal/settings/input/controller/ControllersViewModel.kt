package dev.dfd.fractal.settings.input.controller

import android.app.Application
import android.content.Context
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelProvider.AndroidViewModelFactory.Companion.APPLICATION_KEY
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import dev.dfd.fractal.common.android.inputdevice.hasMotion
import dev.dfd.fractal.common.android.inputdevice.hasRumble
import dev.dfd.fractal.common.android.inputdevice.listGameControllers
import dev.dfd.fractal.common.android.inputdevice.toControllerInfo
import dev.dfd.fractal.nativeinterface.NativeInput
import dev.dfd.fractal.settings.input.controller.GameControllerType
import dev.dfd.fractal.nativeinterface.NativeInput.EmulatedControllerType
import dev.dfd.fractal.nativeinterface.NativeInput.WiimoteExtension
import dev.dfd.fractal.settings.input.controller.emulatedcontroller.inputFullName
import dev.dfd.fractal.settings.input.controller.emulatedcontroller.inputGroupsFor
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

data class ButtonInfo(
    val name: String,
    val id: Int,
)

data class InputController(
    val id: Int,
    val name: String,
    val descriptor: String,
    val hasMotion: Boolean,
    val hasRumble: Boolean,
)

data class ActiveController(
    val controller: InputController,
    val settings: NativeInput.ControllerSettings
)

/** What the user picks for a slot; GAMECUBE is a Classic Controller with the GameCube layout. */
enum class ControllerChoice(val nativeType: Int) {
    DISABLED(EmulatedControllerType.DISABLED),
    GAMEPAD(EmulatedControllerType.VPAD),
    PRO(EmulatedControllerType.PRO),
    CLASSIC(EmulatedControllerType.CLASSIC),
    GAMECUBE(EmulatedControllerType.CLASSIC),
    WIIMOTE(EmulatedControllerType.WIIMOTE);

    val style: ControllerStyle?
        get() = when (this) {
            DISABLED -> null
            GAMEPAD -> ControllerStyle.GAMEPAD
            PRO -> ControllerStyle.PRO
            CLASSIC -> ControllerStyle.CLASSIC
            GAMECUBE -> ControllerStyle.GAMECUBE
            WIIMOTE -> ControllerStyle.WIIMOTE
        }

    companion object {
        fun of(nativeType: Int, gameCubeLayout: Boolean): ControllerChoice = when (nativeType) {
            EmulatedControllerType.VPAD -> GAMEPAD
            EmulatedControllerType.PRO -> PRO
            EmulatedControllerType.CLASSIC -> if (gameCubeLayout) GAMECUBE else CLASSIC
            EmulatedControllerType.WIIMOTE -> WIIMOTE
            else -> DISABLED
        }
    }
}

class ControllersViewModel(private val context: Context, val controllerIndex: Int) : ViewModel() {
    private val _choice = MutableStateFlow(readChoice())
    val choice = _choice.asStateFlow()

    private val _wiimoteExtension = MutableStateFlow(NativeInput.getWiimoteExtension(controllerIndex))
    val wiimoteExtension = _wiimoteExtension.asStateFlow()

    private val _controls = MutableStateFlow<Map<Int, String>>(emptyMap())
    val controls = _controls.asStateFlow()

    private val _controllers = MutableStateFlow<List<InputController>>(emptyList())
    val controllers = _controllers.asStateFlow()

    private val _buttonToBind = MutableStateFlow<ButtonInfo?>(null)
    val buttonToBind = _buttonToBind.asStateFlow()

    /** Inputs still to bind after [buttonToBind] in "Map all, one by one". */
    private var bindQueue: List<ButtonInfo> = emptyList()

    private val _bindQueueSize = MutableStateFlow(0)
    val bindQueueSize = _bindQueueSize.asStateFlow()

    private val _profiles = MutableStateFlow<List<String>>(emptyList())
    val profiles = _profiles.asStateFlow()

    private val _profileName = MutableStateFlow<String?>(null)
    val profileName = _profileName.asStateFlow()

    /** Axis just bound and still held: it must go back to rest before it can bind the next input. */
    private var heldAxis: Int? = null

    private var controllersCount = NativeInput.ControllerCount(0, 0)

    private fun readChoice() = ControllerChoice.of(
        NativeInput.getControllerType(controllerIndex),
        AutoControllerMapping.isGameCubeLayout(context, controllerIndex),
    )

    val controllerType: Int get() = _choice.value.nativeType

    private fun getControllerMapping(buttonId: Int) =
        buttonId to NativeInput.getControllerMapping(controllerIndex, buttonId)

    /** Returns whether the choice changed. */
    fun setChoice(choice: ControllerChoice): Boolean {
        if (!isChoiceAllowed(choice) || _choice.value == choice) {
            return false
        }
        val typeChanged = choice.nativeType != _choice.value.nativeType
        AutoControllerMapping.setGameCubeLayout(context, controllerIndex, choice == ControllerChoice.GAMECUBE)
        _choice.value = choice
        if (typeChanged) {
            NativeInput.setControllerType(controllerIndex, choice.nativeType)
            AutoControllerMapping.onControllerTypeChosen(context, controllerIndex, choice.nativeType)
        }
        refreshControllerData()
        if (choice != ControllerChoice.DISABLED) {
            // a new type starts mapped on the connected pad; the GameCube layout remaps the Classic Controller
            val shouldMap = _controls.value.isEmpty() || choice == ControllerChoice.GAMECUBE ||
                    (choice == ControllerChoice.CLASSIC && !typeChanged)
            if (shouldMap) resetToAutomaticMapping()
        }
        return true
    }

    fun setWiimoteExtension(extension: Int) {
        NativeInput.setWiimoteExtension(controllerIndex, extension)
        _wiimoteExtension.value = NativeInput.getWiimoteExtension(controllerIndex)
    }

    /** Binds [ids] one after the other, each with its full name ("Left stick: up"). */
    fun startBinding(ids: List<Int>) {
        val style = _choice.value.style ?: return
        val nunchuk = WiimoteExtension.hasNunchuk(_wiimoteExtension.value)
        val infos = ids.mapNotNull { id -> inputFullName(style, nunchuk, id)?.let { ButtonInfo(it, id) } }
        if (infos.isEmpty()) return
        _buttonToBind.value = infos.first()
        bindQueue = infos.drop(1)
        _bindQueueSize.value = bindQueue.size
    }

    /** Every input of the current controller, one by one. */
    fun startBindingAll() {
        val style = _choice.value.style ?: return
        val nunchuk = WiimoteExtension.hasNunchuk(_wiimoteExtension.value)
        startBinding(inputGroupsFor(style, nunchuk).flatMap { group -> group.inputs.map { it.first } })
    }

    /** Next input of the queue, or done. */
    fun skipButtonToBind() {
        _buttonToBind.value = bindQueue.firstOrNull()
        bindQueue = bindQueue.drop(1)
        _bindQueueSize.value = bindQueue.size
    }

    fun clearButtonToBind() {
        bindQueue = emptyList()
        _bindQueueSize.value = 0
        _buttonToBind.value = null
    }

    fun mapKeyEvent(keyEvent: KeyEvent): Boolean {
        val buttonId = _buttonToBind.value?.id ?: return false
        if (keyEvent.action != KeyEvent.ACTION_DOWN || keyEvent.repeatCount > 0) {
            return false
        }
        InputMapper.mapKeyEventToMappingId(controllerIndex, buttonId, keyEvent)
        _controls.value += getControllerMapping(buttonId)
        skipButtonToBind()
        return true
    }

    fun tryMapMotionEvent(motionEvent: MotionEvent): Boolean {
        val buttonId = _buttonToBind.value?.id ?: return false
        val axis = InputMapper.dominantAxis(motionEvent)
        if (heldAxis != null) {
            if (axis == heldAxis) return false
            heldAxis = null
        }
        if (axis == null || !InputMapper.tryMapMotionEventToMappingId(controllerIndex, buttonId, motionEvent)) {
            return false
        }
        heldAxis = axis
        _controls.value += getControllerMapping(buttonId)
        skipButtonToBind()
        return true
    }

    /** Physical controllers connected now; also sends them to the native side. */
    fun refreshAvailableControllers(): Boolean {
        val gameControllers = listGameControllers()

        val inputControllers = gameControllers.map {
            InputController(
                id = it.id,
                name = it.name,
                descriptor = it.descriptor,
                hasMotion = it.hasMotion(),
                hasRumble = it.hasRumble(),
            )
        }
        val active = _activeController.value?.controller
        setActiveController(inputControllers.firstOrNull { it.descriptor == active?.descriptor } ?: inputControllers.firstOrNull())
        _controllers.value = inputControllers

        NativeInput.setControllers(gameControllers.map { it.toControllerInfo() }.toTypedArray())

        return gameControllers.isNotEmpty()
    }

    fun mapAllInputs(deviceId: Int) {
        val oldControls = _controls.value
        _controls.value = emptyMap()
        oldControls.keys.forEach { NativeInput.clearControllerMapping(controllerIndex, it) }

        val layout = if (_choice.value == ControllerChoice.GAMECUBE) MappingLayout.GAMECUBE
        else AutoControllerMapping.defaultLayout(context)
        InputMapper.mapAllInputs(deviceId, controllerIndex, layout)

        refreshControllerData()
    }

    /** Redoes the automatic mapping with the connected gamepad; returns its name, null when none is connected. */
    fun resetToAutomaticMapping(): String? {
        val device = AutoControllerMapping.pickDeviceForReset(controllerIndex) ?: return null
        mapAllInputs(device.id)
        return device.name
    }

    fun clearButtonMapping(buttonId: Int) {
        _controls.value -= buttonId
        NativeInput.clearControllerMapping(controllerIndex, buttonId)
    }

    private fun refreshControllerData() {
        controllersCount = NativeInput.getControllersCount()
        _controls.value = NativeInput.getControllerMappings(controllerIndex)
        _wiimoteExtension.value = NativeInput.getWiimoteExtension(controllerIndex)
        _profileName.value = NativeInput.getControllerProfileName(controllerIndex)
    }

    fun isChoiceAllowed(choice: ControllerChoice): Boolean {
        val controllerType = choice.nativeType
        if (controllerType == EmulatedControllerType.DISABLED) {
            return true
        }

        val currentControllerType = this.controllerType

        if (controllerType == EmulatedControllerType.VPAD) {
            return currentControllerType == EmulatedControllerType.VPAD || controllersCount.vpadCount < NativeInput.MAX_VPAD_CONTROLLERS
        }

        val isWPAD = currentControllerType != EmulatedControllerType.VPAD
                && currentControllerType != EmulatedControllerType.DISABLED

        return isWPAD || controllersCount.wpadCount < NativeInput.MAX_WPAD_CONTROLLERS
    }

    private val _activeController = MutableStateFlow<ActiveController?>(null)
    val activeController = _activeController.asStateFlow()

    fun setActiveController(controller: InputController?) {
        if (controller == null) {
            _activeController.value = null
            return
        }

        val settings = NativeInput.getControllerSettings(
            controllerIndex,
            controller.descriptor,
            controller.name
        ) ?: NativeInput.ControllerSettings()
        _activeController.value = ActiveController(controller, settings)
    }

    fun setControllerSettings(
        controller: InputController,
        settings: NativeInput.ControllerSettings
    ) {
        NativeInput.setControllerSettings(
            controllerIndex,
            controller.descriptor,
            controller.name,
            settings
        )
        _activeController.value = _activeController.value?.copy(settings = settings)
    }

    // region profiles

    fun refreshProfiles() {
        _profiles.value = NativeInput.getControllerProfiles().filter { it !in GameControllerType.profileNames }
            .sortedWith(String.CASE_INSENSITIVE_ORDER)
    }

    fun isValidProfileName(name: String) = name.isNotBlank() && NativeInput.isValidControllerProfileName(name.trim())

    fun saveProfile(name: String): Boolean {
        val ok = NativeInput.saveControllerProfile(controllerIndex, name.trim())
        refreshProfiles()
        refreshControllerData()
        return ok
    }

    fun loadProfile(name: String): Boolean {
        if (!NativeInput.loadControllerProfile(controllerIndex, name)) return false
        // the GameCube look only belongs to the slot it was set on
        AutoControllerMapping.setGameCubeLayout(context, controllerIndex, false)
        _choice.value = readChoice()
        refreshControllerData()
        refreshAvailableControllers()
        save()
        return true
    }

    fun deleteProfile(name: String) {
        NativeInput.deleteControllerProfile(name)
        refreshProfiles()
    }

    // endregion

    fun save() {
        NativeInput.saveInput(controllerIndex)
    }

    init {
        refreshControllerData()
        refreshProfiles()
    }

    companion object {
        val CONTROLLER_INDEX_KEY = object : CreationExtras.Key<Int> {}
        val Factory: ViewModelProvider.Factory = viewModelFactory {
            initializer {
                ControllersViewModel(
                    (this[APPLICATION_KEY] as Application).applicationContext,
                    this[CONTROLLER_INDEX_KEY] as Int,
                )
            }
        }
    }
}
