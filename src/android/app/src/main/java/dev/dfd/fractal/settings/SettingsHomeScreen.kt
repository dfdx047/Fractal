package dev.dfd.fractal.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import dev.dfd.fractal.common.ui.fractal.FractalScreen
import dev.dfd.fractal.common.ui.fractal.SectionTitle
import dev.dfd.fractal.common.ui.fractal.SettingsNavRow
import dev.dfd.fractal.common.ui.icons.FractalIcons
import dev.dfd.fractal.common.ui.localization.tr

private class SettingsEntry(
    val icon: ImageVector,
    val title: String,
    val description: String,
    val onClick: () -> Unit,
)

/** Width from which a section lays its entries out in two columns (landscape, tablets). */
private val TwoColumnMinWidth = 720.dp

@Composable
fun SettingsHomeScreen(
    goToGeneralSettings: () -> Unit,
    goToGamePaths: () -> Unit,
    goToDataFolder: () -> Unit,
    goToAccountSettings: () -> Unit,
    goToWelcome: () -> Unit,
    goToGraphicsSettings: () -> Unit,
    goToFrameGenSettings: () -> Unit,
    goToDrivers: () -> Unit,
    goToAudioSettings: () -> Unit,
    goToInputSettings: () -> Unit,
    goToInputOverlaySettings: () -> Unit,
    goToHotkeySettings: () -> Unit,
    goToAppearance: () -> Unit,
    goToOverlaySettings: () -> Unit,
    goToEmulatedUSBDevicesSettings: () -> Unit,
    navigateBack: () -> Unit
) {
    FractalScreen(title = tr("Settings"), onBack = navigateBack) {
        SettingsSection(
            tr("System"),
            SettingsEntry(
                FractalIcons.Settings, tr("General settings"),
                tr("App and console language, GamePad screen position, battery saving"),
                dropUnlessResumed(block = goToGeneralSettings),
            ),
            SettingsEntry(
                FractalIcons.Folder, tr("Game folders"),
                tr("Folders scanned for games"),
                dropUnlessResumed(block = goToGamePaths),
            ),
            SettingsEntry(
                FractalIcons.Box, tr("Emulator data folder"),
                tr("Where saves, shader caches and graphic packs are kept: internal storage or SD card"),
                dropUnlessResumed(block = goToDataFolder),
            ),
            SettingsEntry(
                FractalIcons.User, tr("Account settings"),
                tr("Wii U accounts and online service"),
                dropUnlessResumed(block = goToAccountSettings),
            ),
            SettingsEntry(
                FractalIcons.Sparkle, tr("Welcome setup"),
                tr("Run the first-use setup again"),
                dropUnlessResumed(block = goToWelcome),
            ),
        )
        SettingsSection(
            tr("Graphics"),
            SettingsEntry(
                FractalIcons.Display, tr("Graphics settings"),
                tr("Shaders, VSync, scaling filters and gamma"),
                dropUnlessResumed(block = goToGraphicsSettings),
            ),
            SettingsEntry(
                FractalIcons.Chip, tr("Drivers"),
                tr("Install GPU drivers and switch between them"),
                dropUnlessResumed(block = goToDrivers),
            ),
            SettingsEntry(
                FractalIcons.Layers, tr("Frame generation"),
                tr("Lossless Scaling frame generation with your own Lossless.dll"),
                dropUnlessResumed(block = goToFrameGenSettings),
            ),
        )
        SettingsSection(
            tr("Audio"),
            SettingsEntry(
                FractalIcons.Speaker, tr("Audio settings"),
                tr("Latency, volume and channels of the TV and GamePad"),
                dropUnlessResumed(block = goToAudioSettings),
            ),
        )
        SettingsSection(
            tr("Controls"),
            SettingsEntry(
                FractalIcons.Gamepad, tr("Controllers"),
                tr("Emulated controllers, mappings and device motion"),
                dropUnlessResumed(block = goToInputSettings),
            ),
            SettingsEntry(
                FractalIcons.TouchControls, tr("On-screen controls"),
                tr("Touch buttons shown over the game"),
                dropUnlessResumed(block = goToInputOverlaySettings),
            ),
            SettingsEntry(
                FractalIcons.Keyboard, tr("Hotkeys"),
                tr("Button combinations for emulator actions"),
                dropUnlessResumed(block = goToHotkeySettings),
            ),
        )
        SettingsSection(
            tr("Interface"),
            SettingsEntry(
                FractalIcons.Palette, tr("Appearance"),
                tr("Theme, wallpaper, library view and sounds"),
                dropUnlessResumed(block = goToAppearance),
            ),
            SettingsEntry(
                FractalIcons.Stats, tr("Overlay settings"),
                tr("Performance statistics, notifications and log"),
                dropUnlessResumed(block = goToOverlaySettings),
            ),
        )
        SettingsSection(
            tr("Advanced"),
            SettingsEntry(
                FractalIcons.Usb, tr("Emulated USB devices"),
                tr("Skylanders portal, Infinity base and Dimensions toypad"),
                dropUnlessResumed(block = goToEmulatedUSBDevicesSettings),
            ),
        )
    }
}

/** Section title and its entries: one column on phones in portrait, two when there is room. */
@Composable
private fun SettingsSection(title: String, vararg entries: SettingsEntry) {
    SectionTitle(title)
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val columns = if (maxWidth >= TwoColumnMinWidth) 2 else 1
        Column {
            entries.toList().chunked(columns).forEach { row ->
                // the cards of a row share their height, so a description on two lines doesn't make them uneven
                Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    row.forEach { entry ->
                        SettingsNavRow(
                            title = entry.title,
                            description = entry.description,
                            icon = entry.icon,
                            onClick = entry.onClick,
                            descriptionMaxLines = 2,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                    }
                    // keep the last entry of an odd row at column width
                    repeat(columns - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
        }
    }
}
