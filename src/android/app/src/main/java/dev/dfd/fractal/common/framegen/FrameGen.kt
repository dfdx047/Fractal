package dev.dfd.fractal.common.framegen

// Lossless Scaling frame generation, ported from Cemu Plus (danielcamilo1)

import android.content.Context
import android.net.Uri
import dev.dfd.fractal.common.ui.localization.tr
import dev.dfd.fractal.nativeinterface.NativeFrameGen
import dev.dfd.fractal.nativeinterface.NativeFrameGen.LosslessDllStatus
import dev.dfd.fractal.nativeinterface.NativeSettings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

val FrameGenMultiplierChoices = listOf(2, 3, 4)
val FrameGenTargetRateChoices = listOf(0, 60, 90, 120)
val FrameGenFlowScaleChoices = listOf(NativeFrameGen.FLOW_SCALE_AUTO, 50, 75, 100)

fun frameGenMultiplierToString(multiplier: Int) = "${multiplier}x"

fun frameGenTargetRateToString(targetRate: Int) =
    if (targetRate == 0) tr("Fixed multiplier") else tr("Adaptive {0} FPS", targetRate)

fun frameGenFlowScaleToString(flowScale: Int) =
    if (flowScale == NativeFrameGen.FLOW_SCALE_AUTO) tr("Auto") else "$flowScale%"

fun losslessDllStatusToString(status: Int) = when (status) {
    LosslessDllStatus.OK -> tr("Installed")
    LosslessDllStatus.NOT_INSTALLED -> tr("Not installed")
    LosslessDllStatus.UNREADABLE_FILE -> tr("The file could not be read")
    LosslessDllStatus.NOT_PORTABLE_EXECUTABLE -> tr("The file is not a Windows DLL")
    LosslessDllStatus.MISSING_SHADERS -> tr("The DLL does not contain the frame generation shaders")
    LosslessDllStatus.TRANSLATION_FAILED -> tr("The frame generation shaders could not be loaded")
    else -> tr("Unknown error")
}

fun frameGenRuntimeStatusToString(status: Int) = when (status) {
    NativeFrameGen.RuntimeStatus.ACTIVE -> tr("Active")
    NativeFrameGen.RuntimeStatus.UNSUPPORTED_DEVICE -> tr("Not supported by this GPU driver (needs the Vulkan memory model, float16 and null descriptors)")
    NativeFrameGen.RuntimeStatus.DLL_UNAVAILABLE -> tr("Lossless.dll is missing or invalid")
    NativeFrameGen.RuntimeStatus.INIT_FAILED -> tr("Failed to start")
    NativeFrameGen.RuntimeStatus.RESTART_REQUIRED -> tr("Restart the game to turn it on")
    else -> tr("Waiting for the game")
}

/** Cheap check for places that run often: the DLL is only copied there after it was validated. */
fun hasLosslessDll() = File(NativeFrameGen.getLosslessDllPath()).exists()

/**
 * Copies the user selected Lossless.dll into the user data folder after checking that it
 * contains the frame generation shaders. Returns one of [LosslessDllStatus].
 * Fractal never ships or downloads the DLL: it only reads the shaders out of the user's own copy.
 */
suspend fun installLosslessDll(context: Context, uri: Uri): Int = withContext(Dispatchers.IO) {
    val destination = File(NativeFrameGen.getLosslessDllPath())
    val temporary = File(destination.parentFile, "${destination.name}.tmp")
    try {
        destination.parentFile?.mkdirs()
        val copied = context.contentResolver.openInputStream(uri)?.use { input ->
            temporary.outputStream().use { output -> input.copyTo(output) }
            true
        } ?: false
        if (!copied) {
            return@withContext LosslessDllStatus.UNREADABLE_FILE
        }

        val status = NativeFrameGen.validateLosslessDll(temporary.path)
        if (status != LosslessDllStatus.OK) {
            return@withContext status
        }

        destination.delete()
        if (!temporary.renameTo(destination)) {
            return@withContext LosslessDllStatus.UNREADABLE_FILE
        }
        NativeFrameGen.notifyLosslessDllChanged()
        LosslessDllStatus.OK
    } catch (exception: Exception) {
        LosslessDllStatus.UNREADABLE_FILE
    } finally {
        temporary.delete()
    }
}

suspend fun removeLosslessDll(): Boolean = withContext(Dispatchers.IO) {
    val removed = NativeFrameGen.removeLosslessDll()
    if (removed) {
        NativeFrameGen.setEnabled(false)
        NativeSettings.saveSettings()
    }
    removed
}
