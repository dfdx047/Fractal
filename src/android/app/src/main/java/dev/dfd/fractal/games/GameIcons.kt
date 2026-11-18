/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.games

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathBuilder
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

/** Icons only the game screens use, drawn like FractalIcons (24x24, 1.8 stroke, mitered). */
object GameIcons {
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

    val Sort = icon("sort") {
        moveTo(4f, 6.5f); lineTo(20f, 6.5f)
        moveTo(4f, 12f); lineTo(15f, 12f)
        moveTo(4f, 17.5f); lineTo(10f, 17.5f)
    }

    val Clock = icon("clock") {
        // octagon dial, hands at ten past
        moveTo(8.5f, 3.5f); lineTo(15.5f, 3.5f); lineTo(20.5f, 8.5f); lineTo(20.5f, 15.5f)
        lineTo(15.5f, 20.5f); lineTo(8.5f, 20.5f); lineTo(3.5f, 15.5f); lineTo(3.5f, 8.5f); close()
        moveTo(12f, 7.5f); lineTo(12f, 12f); lineTo(15.5f, 14f)
    }

    /** Arrow up out of a tray: export. */
    val Export = icon("export") {
        moveTo(12f, 15.5f); lineTo(12f, 4f)
        moveTo(7f, 9f); lineTo(12f, 4f); lineTo(17f, 9f)
        moveTo(4.5f, 14f); lineTo(4.5f, 20f); lineTo(19.5f, 20f); lineTo(19.5f, 14f)
    }

    /** Arrow down into a tray: import. */
    val Import = icon("import") {
        moveTo(12f, 3.5f); lineTo(12f, 15f)
        moveTo(7f, 10f); lineTo(12f, 15f); lineTo(17f, 10f)
        moveTo(4.5f, 14f); lineTo(4.5f, 20f); lineTo(19.5f, 20f); lineTo(19.5f, 14f)
    }

    /** Lightning bolt: performance. */
    val Bolt = icon("bolt") {
        moveTo(13.5f, 3f); lineTo(5.5f, 13.5f); lineTo(11.5f, 13.5f); lineTo(10.5f, 21f); lineTo(18.5f, 10.5f)
        lineTo(12.5f, 10.5f); close()
    }
}
