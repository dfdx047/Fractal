package dev.dfd.fractal.common.input

import android.view.KeyEvent
import android.view.MotionEvent
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.merge

object GamepadInputSource {
    private val _motionEvents = MutableSharedFlow<InputEvent.Motion>(extraBufferCapacity = 64)
    val hasMotionSubscribers: Boolean
        get() = _motionEvents.subscriptionCount.value > 0

    private val _keyEvents = MutableSharedFlow<InputEvent.Key>(extraBufferCapacity = 64)
    val hasKeySubscribers: Boolean
        get() = _keyEvents.subscriptionCount.value > 0

    val events = merge(_motionEvents, _keyEvents)

    sealed class InputEvent {
        data class Motion(val motionEvent: MotionEvent) : InputEvent()
        data class Key(val keyEvent: KeyEvent) : InputEvent()
    }

    fun emitMotion(event: MotionEvent) {
        _motionEvents.tryEmit(InputEvent.Motion(event))
    }

    fun emitKey(event: KeyEvent) {
        _keyEvents.tryEmit(InputEvent.Key(event))
    }
}
