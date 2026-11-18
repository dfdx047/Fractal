/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.settings.input.controller

import android.util.Log
import dev.dfd.fractal.nativeinterface.NativeGameTitles
import dev.dfd.fractal.nativeinterface.NativeInput
import dev.dfd.fractal.nativeinterface.NativeInput.EmulatedControllerType

/**
 * The emulated controller type player 1 has in one game (the Wii U GamePad for a game that only reads it, a Pro
 * Controller for one that never does), without touching the input settings of the other games.
 *
 * It is a game profile controller profile that holds only the type: when the game starts, the automatic mapping puts
 * the connected gamepad on it, so it works with whichever gamepad the player has that day.
 */
object GameControllerType {
    private const val TAG = "GameControllerType"

    /** Types a game can pick for player 1. */
    val CHOICES = listOf(
        EmulatedControllerType.VPAD,
        EmulatedControllerType.PRO,
        EmulatedControllerType.CLASSIC,
        EmulatedControllerType.WIIMOTE,
    )

    private val PROFILE_NAMES = mapOf(
        EmulatedControllerType.VPAD to "Fractal GamePad",
        EmulatedControllerType.PRO to "Fractal Pro Controller",
        EmulatedControllerType.CLASSIC to "Fractal Classic Controller",
        EmulatedControllerType.WIIMOTE to "Fractal Wiimote",
    )

    fun profileName(type: Int): String? = PROFILE_NAMES[type]

    /** The type a profile name stands for, null for a profile the user saved. */
    fun typeOfProfile(name: String?): Int? = PROFILE_NAMES.entries.firstOrNull { it.value == name }?.key

    fun isTypeProfile(name: String?) = typeOfProfile(name) != null

    /** Sets player 1's type in game [titleId]; null goes back to the input settings. Returns the profile name used. */
    fun set(titleId: Long, type: Int?): Result<String?> = runCatching {
        if (type == null) {
            NativeGameTitles.setControllerProfileForTitle(titleId, 0, null)
            return@runCatching null
        }
        val name = profileName(type) ?: error("no profile for type $type")
        check(NativeInput.saveControllerTypeProfile(name, type)) { "can't write profile $name" }
        NativeGameTitles.setControllerProfileForTitle(titleId, 0, name)
        name
    }.onFailure { Log.w(TAG, "can't set the controller type of $titleId", it) }

    /** Profile names the type choices write, kept out of the user's own profile lists. */
    val profileNames: Collection<String> get() = PROFILE_NAMES.values
}
