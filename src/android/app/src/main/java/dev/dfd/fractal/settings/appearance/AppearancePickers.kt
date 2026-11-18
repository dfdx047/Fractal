/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.settings.appearance

import kotlin.math.roundToInt
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Slider
import dev.dfd.fractal.common.sound.withSfx
import dev.dfd.fractal.common.sound.MenuSounds
import dev.dfd.fractal.common.sound.MenuSfx
import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.dfd.fractal.common.settings.AppSettingsStore
import dev.dfd.fractal.common.settings.GuiSettings
import dev.dfd.fractal.common.settings.LibraryView
import dev.dfd.fractal.common.ui.fractal.focusRing
import dev.dfd.fractal.common.ui.fractal.gamepadConfirm
import dev.dfd.fractal.common.ui.fractal.screenFocusItem
import dev.dfd.fractal.common.ui.icons.FractalIcons
import dev.dfd.fractal.common.ui.localization.tr
import dev.dfd.fractal.common.ui.theme.FractalTheme
import dev.dfd.fractal.common.ui.theme.FractalThemeId
import dev.dfd.fractal.common.ui.theme.WallpaperId
import dev.dfd.fractal.common.ui.theme.fractalSurface
import dev.dfd.fractal.common.ui.theme.fractalTokens
import dev.dfd.fractal.common.ui.wallpaper.Wallpaper
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

suspend fun updateGuiSettings(transform: (GuiSettings) -> GuiSettings) {
    AppSettingsStore.dataStore.updateData { it.copy(guiSettings = transform(it.guiSettings)) }
}

fun themeName(id: FractalThemeId) = when (id) {
    FractalThemeId.SOLID -> tr("Solid")
    FractalThemeId.LIQUID_GLASS -> tr("Liquid Glass")
    FractalThemeId.BLUR -> tr("Blur")
    FractalThemeId.MATERIAL_YOU -> tr("Material You")
    FractalThemeId.SYNTHWAVE -> tr("Synthwave")
}

private fun themeDescription(id: FractalThemeId) = when (id) {
    FractalThemeId.SOLID -> tr("Solid colors, chamfered corners")
    FractalThemeId.LIQUID_GLASS -> tr("Translucent glass over the wallpaper")
    FractalThemeId.BLUR -> tr("The focused game's art as the background")
    FractalThemeId.MATERIAL_YOU -> tr("Your system colors")
    FractalThemeId.SYNTHWAVE -> tr("Magenta, purple and cyan gradients")
}

fun wallpaperName(id: WallpaperId) = when (id) {
    WallpaperId.NONE -> tr("None")
    WallpaperId.STATIC_INK -> tr("Facets: ink")
    WallpaperId.STATIC_MAGENTA -> tr("Facets: magenta")
    WallpaperId.STATIC_CYAN -> tr("Facets: cyan")
    WallpaperId.STATIC_PURPLE -> tr("Facets: purple")
    WallpaperId.STATIC_SYNTHWAVE -> tr("Facets: synthwave")
    WallpaperId.ANIMATED_DRIFT -> tr("Drifting triangles")
    WallpaperId.ANIMATED_BREATHE -> tr("Breathing facets")
    WallpaperId.ANIMATED_SIERPINSKI -> tr("Sierpinski zoom")
    WallpaperId.ANIMATED_WAVE -> tr("Low-poly wave")
    WallpaperId.ANIMATED_PARTICLES -> tr("Triangle particles")
    WallpaperId.CUSTOM -> tr("Your image")
}

/** A focusable, clickable tile with the Fractal focus ring and a selected state. */
@Composable
private fun PickerTile(
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = FractalTheme.shapes.card
    Box(
        modifier
            .focusRing(focused, shape)
            .clip(shape)
            .screenFocusItem()
            .gamepadConfirm(withSfx(MenuSfx.SWAP, onClick))
            .clickable(interaction, indication = null, role = Role.RadioButton, onClick = withSfx(MenuSfx.SWAP, onClick))
            .fractalSurface(
                shape,
                FractalTheme.surface,
                if (selected) FractalTheme.colors.selected else null,
            ),
    ) {
        content()
        if (selected) {
            Icon(
                FractalIcons.Check,
                contentDescription = null,
                tint = FractalTheme.colors.accent,
                modifier = Modifier.align(Alignment.TopEnd).padding(8.dp).size(20.dp),
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ThemePicker(current: FractalThemeId, onSelect: (FractalThemeId) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        FractalThemeId.entries.forEach { id ->
            PickerTile(selected = id == current, onClick = { onSelect(id) }, modifier = Modifier.width(150.dp)) {
                Column(Modifier.padding(8.dp)) {
                    ThemeSwatch(id, Modifier.fillMaxWidth().height(64.dp))
                    Spacer(Modifier.height(6.dp))
                    Text(themeName(id), style = MaterialTheme.typography.titleSmall, color = FractalTheme.colors.text)
                    Text(
                        themeDescription(id),
                        style = MaterialTheme.typography.labelSmall,
                        color = FractalTheme.colors.textSecondary,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

/** Miniature of a theme: its background, a panel and an accent button, in its own tokens. */
@Composable
private fun ThemeSwatch(id: FractalThemeId, modifier: Modifier) {
    val tokens = fractalTokens(id)
    val shapes = tokens.shapes
    Box(modifier.clip(shapes.card)) {
        Wallpaper(
            tokens.defaultWallpaper,
            customImagePath = null,
            economy = true,
            fallback = tokens.colors.background,
            modifier = Modifier.fillMaxSize(),
        )
        tokens.colors.backgroundBrush?.let { Box(Modifier.fillMaxSize().background(it, alpha = 0.55f)) }
        Row(Modifier.fillMaxSize().padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Box(
                Modifier
                    .size(34.dp, 46.dp)
                    .clip(shapes.cover)
                    .background(tokens.colors.surfaceHigh)
            )
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Box(Modifier.size(52.dp, 8.dp).background(tokens.colors.text.copy(alpha = 0.85f)))
                Box(Modifier.size(36.dp, 6.dp).background(tokens.colors.textSecondary.copy(alpha = 0.6f)))
                Spacer(Modifier.height(4.dp))
                Box(
                    Modifier
                        .size(48.dp, 16.dp)
                        .clip(shapes.button)
                        .then(
                            tokens.colors.accentBrush?.let { Modifier.background(it) }
                                ?: Modifier.background(tokens.colors.accent)
                        )
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun WallpaperPicker(
    current: WallpaperId,
    customImagePath: String?,
    economy: Boolean,
    onSelect: (WallpaperId) -> Unit,
    onCustomImage: (String) -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val pickImage = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch {
            copyWallpaper(context, uri)?.let(onCustomImage)
        }
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        WallpaperId.entries.forEach { id ->
            val isCustom = id == WallpaperId.CUSTOM
            PickerTile(
                selected = id == current,
                onClick = {
                    if (isCustom) pickImage.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
                    else onSelect(id)
                },
                modifier = Modifier.width(124.dp),
            ) {
                Column(Modifier.padding(6.dp)) {
                    Box(Modifier.fillMaxWidth().height(64.dp).clip(FractalTheme.shapes.chip)) {
                        if (isCustom && customImagePath == null) {
                            Box(Modifier.fillMaxSize().background(FractalTheme.colors.surfaceHigh), contentAlignment = Alignment.Center) {
                                Icon(FractalIcons.FolderAdd, null, tint = FractalTheme.colors.textSecondary)
                            }
                        } else {
                            Wallpaper(id, customImagePath, economy = economy, fallback = Color(0xFF100F16))
                        }
                    }
                    Spacer(Modifier.height(6.dp))
                    Text(
                        wallpaperName(id),
                        style = MaterialTheme.typography.labelMedium,
                        color = FractalTheme.colors.text,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        if (id.animated) tr("Animated") else tr("Static"),
                        style = MaterialTheme.typography.labelSmall,
                        color = FractalTheme.colors.textSecondary,
                    )
                }
            }
        }
    }
}

/** Copies the picked image into the app's files so it survives the picker's temporary permission. */
private suspend fun copyWallpaper(context: Context, uri: Uri): String? = withContext(Dispatchers.IO) {
    try {
        val dir = File(context.filesDir, "wallpaper").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }
        // new name each time so the wallpaper reloads
        val target = File(dir, "custom-${System.currentTimeMillis()}")
        context.contentResolver.openInputStream(uri)?.use { input ->
            target.outputStream().use { input.copyTo(it) }
        } ?: return@withContext null
        target.absolutePath
    } catch (_: Exception) {
        null
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LibraryViewPicker(current: LibraryView, onSelect: (LibraryView) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        LibraryView.entries.forEach { view ->
            PickerTile(selected = view == current, onClick = { onSelect(view) }) {
                Row(Modifier.padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        when (view) {
                            LibraryView.GRID -> FractalIcons.Grid
                            LibraryView.CAROUSEL -> FractalIcons.Carousel
                            LibraryView.LIST -> FractalIcons.List
                        },
                        contentDescription = null,
                        tint = FractalTheme.colors.text,
                    )
                    Spacer(Modifier.width(10.dp))
                    Text(
                        when (view) {
                            LibraryView.GRID -> tr("Grid")
                            LibraryView.CAROUSEL -> tr("Carousel")
                            LibraryView.LIST -> tr("List")
                        },
                        color = FractalTheme.colors.text,
                        style = MaterialTheme.typography.labelLarge,
                    )
                    Spacer(Modifier.width(18.dp))
                }
            }
        }
    }
}

@Composable
fun EconomyToggle(enabled: Boolean, onChange: (Boolean) -> Unit) =
    SettingToggle(tr("Economy mode"), tr("Freezes animated wallpapers to save battery"), enabled, onChange)

@Composable
fun SettingToggle(title: String, description: String, enabled: Boolean, onChange: (Boolean) -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = FractalTheme.shapes.card
    Row(
        Modifier
            .fillMaxWidth()
            .focusRing(focused, shape, gap = 2.dp)
            .clip(shape)
            .screenFocusItem()
            .gamepadConfirm(withSfx { onChange(!enabled) })
            .clickable(interaction, indication = null, role = Role.Switch, onClick = withSfx { onChange(!enabled) })
            .fractalSurface(shape, FractalTheme.surface)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall, color = FractalTheme.colors.text)
            Text(
                description,
                style = MaterialTheme.typography.bodySmall,
                color = FractalTheme.colors.textSecondary,
            )
        }
        Switch(checked = enabled, onCheckedChange = null)
    }
}

/** Library music and menu sound effects: on/off and volume of each. */
@Composable
fun MenuSoundSettings(gui: GuiSettings, update: ((GuiSettings) -> GuiSettings) -> Unit) {
    SettingToggle(
        tr("Background music"),
        tr("A track for each theme, in the library and settings only"),
        gui.menuMusicEnabled,
    ) { on -> update { it.copy(menuMusicEnabled = on) } }
    if (gui.menuMusicEnabled) {
        SettingSlider(tr("Music volume"), gui.menuMusicVolume) { v -> update { it.copy(menuMusicVolume = v) } }
    }
    SettingToggle(
        tr("Sound effects"),
        tr("Moving through menus and covers, switching themes, opening games and settings"),
        gui.menuSfxEnabled,
    ) { on -> update { it.copy(menuSfxEnabled = on) } }
    if (gui.menuSfxEnabled) {
        SettingSlider(tr("Effects volume"), gui.menuSfxVolume) { v ->
            update { it.copy(menuSfxVolume = v) }
            MenuSounds.preview(MenuSfx.CONFIRM, v)
        }
    }
    CustomSoundSettings(gui, update)
}

/** 0..100 % in steps of 5, saved when the thumb is let go. */
@Composable
fun SettingSlider(title: String, value: Int, onChange: (Int) -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = FractalTheme.shapes.card
    val colors = FractalTheme.colors
    var current by remember(value) { mutableFloatStateOf(value.toFloat()) }
    Column(
        Modifier
            .fillMaxWidth()
            .focusRing(focused, shape, gap = 2.dp)
            .clip(shape)
            .fractalSurface(shape, FractalTheme.surface)
            .padding(horizontal = 14.dp, vertical = 8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.titleSmall, color = colors.text, modifier = Modifier.weight(1f))
            Text("${current.roundToInt()}%", style = MaterialTheme.typography.labelLarge, color = colors.textSecondary)
        }
        Slider(
            value = current,
            onValueChange = { current = it },
            onValueChangeFinished = { onChange(current.roundToInt()) },
            valueRange = 0f..100f,
            steps = 19,
            interactionSource = interaction,
            colors = SliderDefaults.colors(thumbColor = colors.accent, activeTrackColor = colors.accent),
            modifier = Modifier.screenFocusItem(),
        )
    }
}

