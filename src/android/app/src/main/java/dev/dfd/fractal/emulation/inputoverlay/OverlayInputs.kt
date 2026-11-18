package dev.dfd.fractal.emulation.inputoverlay

sealed interface OverlayInput

enum class OverlayButton : OverlayInput {
    A,
    B,
    ONE,
    TWO,
    C,
    Z,
    HOME,
    L,
    L_STICK_CLICK,
    MINUS,
    PLUS,
    R,
    R_STICK_CLICK,
    X,
    Y,
    ZL,
    ZR,
    BLOW_MIC;
}

enum class OverlayDpad : OverlayInput {
    DPAD_DOWN,
    DPAD_LEFT,
    DPAD_RIGHT,
    DPAD_UP;
}
