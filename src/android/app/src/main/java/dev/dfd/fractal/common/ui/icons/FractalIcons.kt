/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.common.ui.icons

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathBuilder
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

/** Stroke icons in the Fractal style: 24x24 grid, 1.8 stroke, mitered joins. Tint with Icon(tint). */
object FractalIcons {
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

    private fun filled(name: String, block: PathBuilder.() -> Unit): ImageVector =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f).apply {
            path(fill = SolidColor(Color.White), pathBuilder = block)
        }.build()

    val Play = icon("play") { moveTo(7f, 4.5f); lineTo(19f, 12f); lineTo(7f, 19.5f); close() }

    val Settings = icon("settings") {
        // hexagon nut with a hole
        moveTo(12f, 2.8f); lineTo(20f, 7.4f); lineTo(20f, 16.6f); lineTo(12f, 21.2f); lineTo(4f, 16.6f); lineTo(4f, 7.4f); close()
        moveTo(12f, 9f); lineTo(14.6f, 10.5f); lineTo(14.6f, 13.5f); lineTo(12f, 15f); lineTo(9.4f, 13.5f); lineTo(9.4f, 10.5f); close()
    }

    val Tune = icon("tune") {
        moveTo(4f, 7f); lineTo(20f, 7f)
        moveTo(4f, 17f); lineTo(20f, 17f)
        moveTo(9f, 4.5f); lineTo(9f, 9.5f)
        moveTo(15f, 14.5f); lineTo(15f, 19.5f)
    }

    val Grid = icon("grid") {
        moveTo(4f, 4f); lineTo(10.5f, 4f); lineTo(10.5f, 10.5f); lineTo(4f, 10.5f); close()
        moveTo(13.5f, 4f); lineTo(20f, 4f); lineTo(20f, 10.5f); lineTo(13.5f, 10.5f); close()
        moveTo(4f, 13.5f); lineTo(10.5f, 13.5f); lineTo(10.5f, 20f); lineTo(4f, 20f); close()
        moveTo(13.5f, 13.5f); lineTo(20f, 13.5f); lineTo(20f, 20f); lineTo(13.5f, 20f); close()
    }

    val Carousel = icon("carousel") {
        moveTo(7.5f, 5f); lineTo(16.5f, 5f); lineTo(16.5f, 15f); lineTo(7.5f, 15f); close()
        moveTo(3f, 7f); lineTo(3f, 13f)
        moveTo(21f, 7f); lineTo(21f, 13f)
        moveTo(7.5f, 19f); lineTo(16.5f, 19f)
    }

    val List = icon("list") {
        moveTo(4f, 5f); lineTo(8f, 5f); lineTo(8f, 9f); lineTo(4f, 9f); close()
        moveTo(11f, 7f); lineTo(20f, 7f)
        moveTo(4f, 15f); lineTo(8f, 15f); lineTo(8f, 19f); lineTo(4f, 19f); close()
        moveTo(11f, 17f); lineTo(20f, 17f)
    }

    val Search = icon("search") {
        moveTo(10.5f, 4f); lineTo(16f, 7.2f); lineTo(16f, 13.6f); lineTo(10.5f, 16.8f); lineTo(5f, 13.6f); lineTo(5f, 7.2f); close()
        moveTo(15f, 15.5f); lineTo(20f, 20.5f)
    }

    val More = filled("more") {
        moveTo(4f, 10.5f); lineTo(7f, 10.5f); lineTo(7f, 13.5f); lineTo(4f, 13.5f); close()
        moveTo(10.5f, 10.5f); lineTo(13.5f, 10.5f); lineTo(13.5f, 13.5f); lineTo(10.5f, 13.5f); close()
        moveTo(17f, 10.5f); lineTo(20f, 10.5f); lineTo(20f, 13.5f); lineTo(17f, 13.5f); close()
    }

    val FolderAdd = icon("folder_add") {
        moveTo(3f, 6f); lineTo(9f, 6f); lineTo(11f, 8f); lineTo(21f, 8f); lineTo(21f, 19f); lineTo(3f, 19f); close()
        moveTo(12f, 11f); lineTo(12f, 16.5f)
        moveTo(9.25f, 13.75f); lineTo(14.75f, 13.75f)
    }

    val Layers = icon("layers") {
        moveTo(12f, 3.5f); lineTo(21f, 8.5f); lineTo(12f, 13.5f); lineTo(3f, 8.5f); close()
        moveTo(3f, 12.5f); lineTo(12f, 17.5f); lineTo(21f, 12.5f)
        moveTo(3f, 16.5f); lineTo(12f, 21.5f); lineTo(21f, 16.5f)
    }

    val Box = icon("box") {
        moveTo(12f, 3f); lineTo(20f, 7.5f); lineTo(20f, 16.5f); lineTo(12f, 21f); lineTo(4f, 16.5f); lineTo(4f, 7.5f); close()
        moveTo(4f, 7.5f); lineTo(12f, 12f); lineTo(20f, 7.5f)
        moveTo(12f, 12f); lineTo(12f, 21f)
    }

    val Refresh = icon("refresh") {
        // circular arrow: 300° clockwise arc around (12, 12), arrowhead at the top
        moveTo(18.06f, 8.5f)
        arcTo(7f, 7f, 0f, isMoreThanHalf = true, isPositiveArc = true, x1 = 12f, y1 = 5f)
        moveTo(9.5f, 2.5f); lineTo(12f, 5f); lineTo(9.5f, 7.5f)
    }

    val Star = icon("star") {
        moveTo(12f, 3f); lineTo(14.6f, 9.2f); lineTo(21f, 9.6f); lineTo(16.1f, 13.8f); lineTo(17.6f, 20.4f)
        lineTo(12f, 16.9f); lineTo(6.4f, 20.4f); lineTo(7.9f, 13.8f); lineTo(3f, 9.6f); lineTo(9.4f, 9.2f); close()
    }

    val StarFilled = filled("star_filled") {
        moveTo(12f, 3f); lineTo(14.6f, 9.2f); lineTo(21f, 9.6f); lineTo(16.1f, 13.8f); lineTo(17.6f, 20.4f)
        lineTo(12f, 16.9f); lineTo(6.4f, 20.4f); lineTo(7.9f, 13.8f); lineTo(3f, 9.6f); lineTo(9.4f, 9.2f); close()
    }

    val Info = icon("info") {
        moveTo(12f, 3f); lineTo(21f, 12f); lineTo(12f, 21f); lineTo(3f, 12f); close()
        moveTo(12f, 11f); lineTo(12f, 16f)
        moveTo(12f, 8f); lineTo(12f, 8.2f)
    }

    val Close = icon("close") { moveTo(5f, 5f); lineTo(19f, 19f); moveTo(19f, 5f); lineTo(5f, 19f) }

    val Back = icon("back") { moveTo(14.5f, 5f); lineTo(7.5f, 12f); lineTo(14.5f, 19f) }

    val Next = icon("next") { moveTo(9.5f, 5f); lineTo(16.5f, 12f); lineTo(9.5f, 19f) }
    val Download = icon("download") {
        moveTo(12f, 4f); lineTo(12f, 15f)
        moveTo(7f, 10.5f); lineTo(12f, 15.5f); lineTo(17f, 10.5f)
        moveTo(5f, 19.5f); lineTo(19f, 19.5f)
    }

    val Check = icon("check") { moveTo(4.5f, 12.5f); lineTo(9.5f, 17.5f); lineTo(19.5f, 6.5f) }

    val Folder = icon("folder") {
        moveTo(3f, 6f); lineTo(9f, 6f); lineTo(11f, 8f); lineTo(21f, 8f); lineTo(21f, 19f); lineTo(3f, 19f); close()
    }

    val Share = icon("share") {
        moveTo(12f, 15f); lineTo(12f, 3.5f)
        moveTo(8f, 7.5f); lineTo(12f, 3.5f); lineTo(16f, 7.5f)
        moveTo(5f, 11f); lineTo(5f, 20f); lineTo(19f, 20f); lineTo(19f, 11f)
    }

    val Trash = icon("trash") {
        moveTo(4f, 6.5f); lineTo(20f, 6.5f)
        moveTo(9f, 6.5f); lineTo(9f, 3.5f); lineTo(15f, 3.5f); lineTo(15f, 6.5f)
        moveTo(6f, 6.5f); lineTo(7f, 20.5f); lineTo(17f, 20.5f); lineTo(18f, 6.5f)
    }

    val Shortcut = icon("shortcut") {
        moveTo(13f, 4f); lineTo(20f, 4f); lineTo(20f, 11f)
        moveTo(20f, 4f); lineTo(11f, 13f)
        moveTo(17f, 14f); lineTo(17f, 20f); lineTo(4f, 20f); lineTo(4f, 7f); lineTo(10f, 7f)
    }

    val Chip = icon("chip") {
        moveTo(7f, 7f); lineTo(17f, 7f); lineTo(17f, 17f); lineTo(7f, 17f); close()
        moveTo(10f, 3f); lineTo(10f, 7f); moveTo(14f, 3f); lineTo(14f, 7f)
        moveTo(10f, 17f); lineTo(10f, 21f); moveTo(14f, 17f); lineTo(14f, 21f)
        moveTo(3f, 10f); lineTo(7f, 10f); moveTo(3f, 14f); lineTo(7f, 14f)
        moveTo(17f, 10f); lineTo(21f, 10f); moveTo(17f, 14f); lineTo(21f, 14f)
    }

    val Palette = icon("palette") {
        moveTo(12f, 3f); lineTo(21f, 19f); lineTo(3f, 19f); close()
        moveTo(12f, 11f); lineTo(16.5f, 19f)
        moveTo(12f, 11f); lineTo(7.5f, 19f)
    }

    val Globe = icon("globe") {
        moveTo(12f, 3f); lineTo(20f, 7.5f); lineTo(20f, 16.5f); lineTo(12f, 21f); lineTo(4f, 16.5f); lineTo(4f, 7.5f); close()
        moveTo(4f, 12f); lineTo(20f, 12f)
        moveTo(12f, 3f); lineTo(9f, 12f); lineTo(12f, 21f)
        moveTo(12f, 3f); lineTo(15f, 12f); lineTo(12f, 21f)
    }

    val Display = icon("display") {
        moveTo(3f, 4.5f); lineTo(21f, 4.5f); lineTo(21f, 16f); lineTo(3f, 16f); close()
        moveTo(12f, 16f); lineTo(12f, 20f)
        moveTo(7.5f, 20f); lineTo(16.5f, 20f)
    }

    val Speaker = icon("speaker") {
        moveTo(3.5f, 9f); lineTo(7.5f, 9f); lineTo(12.5f, 4.5f); lineTo(12.5f, 19.5f); lineTo(7.5f, 15f); lineTo(3.5f, 15f); close()
        moveTo(15.5f, 9f); lineTo(17f, 12f); lineTo(15.5f, 15f)
        moveTo(18.5f, 6f); lineTo(21f, 12f); lineTo(18.5f, 18f)
    }

    val Gamepad = icon("gamepad") {
        moveTo(7f, 6.5f); lineTo(17f, 6.5f); lineTo(21.5f, 17f); lineTo(17.5f, 18.5f); lineTo(14.5f, 14.5f)
        lineTo(9.5f, 14.5f); lineTo(6.5f, 18.5f); lineTo(2.5f, 17f); close()
        moveTo(8f, 9f); lineTo(8f, 12.5f)
        moveTo(6.25f, 10.75f); lineTo(9.75f, 10.75f)
        moveTo(15.5f, 9.5f); lineTo(15.5f, 9.7f)
        moveTo(17.2f, 11.5f); lineTo(17.2f, 11.7f)
    }

    /** Phone with touch controls on it. */
    val TouchControls = icon("touch_controls") {
        moveTo(2.5f, 6f); lineTo(21.5f, 6f); lineTo(21.5f, 18f); lineTo(2.5f, 18f); close()
        moveTo(7f, 10f); lineTo(7f, 14f)
        moveTo(5f, 12f); lineTo(9f, 12f)
        moveTo(17f, 9.8f); lineTo(19.2f, 12f); lineTo(17f, 14.2f); lineTo(14.8f, 12f); close()
    }

    val Keyboard = icon("keyboard") {
        moveTo(2.5f, 6.5f); lineTo(21.5f, 6.5f); lineTo(21.5f, 17.5f); lineTo(2.5f, 17.5f); close()
        moveTo(6f, 10f); lineTo(6.2f, 10f)
        moveTo(9.5f, 10f); lineTo(9.7f, 10f)
        moveTo(13f, 10f); lineTo(13.2f, 10f)
        moveTo(16.5f, 10f); lineTo(16.7f, 10f)
        moveTo(7.5f, 14f); lineTo(16.5f, 14f)
    }

    val User = icon("user") {
        moveTo(12f, 3.5f); lineTo(15.5f, 7.25f); lineTo(12f, 11f); lineTo(8.5f, 7.25f); close()
        moveTo(4.5f, 20.5f); lineTo(7f, 14f); lineTo(17f, 14f); lineTo(19.5f, 20.5f); close()
    }

    val Usb = icon("usb") {
        moveTo(12f, 5.5f); lineTo(12f, 18f)
        moveTo(10f, 6f); lineTo(12f, 3f); lineTo(14f, 6f); close()
        moveTo(12f, 18f); lineTo(13.75f, 19.75f); lineTo(12f, 21.5f); lineTo(10.25f, 19.75f); close()
        moveTo(12f, 13.5f); lineTo(7f, 11f); lineTo(7f, 8.5f)
        moveTo(5.75f, 6f); lineTo(8.25f, 6f); lineTo(8.25f, 8.5f); lineTo(5.75f, 8.5f); close()
        moveTo(12f, 15.5f); lineTo(17f, 13f); lineTo(17f, 9.5f)
        moveTo(17f, 7f); lineTo(18.25f, 8.25f); lineTo(17f, 9.5f); lineTo(15.75f, 8.25f); close()
    }

    /** Line chart: performance statistics. */
    val Stats = icon("stats") {
        moveTo(3.5f, 20f); lineTo(20.5f, 20f)
        moveTo(4f, 15.5f); lineTo(9f, 10f); lineTo(13f, 13.5f); lineTo(20f, 5.5f)
        moveTo(16f, 5.5f); lineTo(20f, 5.5f); lineTo(20f, 9.5f)
    }

    /** Four point star: first-use setup. */
    val Sparkle = icon("sparkle") {
        moveTo(12f, 3f); lineTo(14f, 10f); lineTo(21f, 12f); lineTo(14f, 14f)
        lineTo(12f, 21f); lineTo(10f, 14f); lineTo(3f, 12f); lineTo(10f, 10f); close()
    }
}
