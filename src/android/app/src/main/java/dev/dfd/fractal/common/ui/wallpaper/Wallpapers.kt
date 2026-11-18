/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

package dev.dfd.fractal.common.ui.wallpaper

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.layout.ContentScale
import androidx.compose.foundation.layout.Spacer
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import dev.dfd.fractal.common.ui.theme.FractalPalette
import dev.dfd.fractal.common.ui.theme.WallpaperId
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import androidx.compose.runtime.withFrameNanos
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.random.Random

/** Animated wallpapers are capped at this frame rate. */
private const val ANIMATION_FPS = 30
private const val FRAME_NANOS = 1_000_000_000L / ANIMATION_FPS
private const val FRAME_SLACK_NANOS = 2_000_000L

private data class Palette(val base: Color, val stops: List<Color>)

private val InkPalette = Palette(FractalPalette.Ink, listOf(Color(0xFF15131D), Color(0xFF1E1A2A), Color(0xFF2A1630)))
private val MagentaPalette = Palette(Color(0xFF16060F), listOf(Color(0xFF2A0A1C), Color(0xFF6E0F45), Color(0xFFFF2D95)))
private val CyanPalette = Palette(Color(0xFF041019), listOf(Color(0xFF06202E), Color(0xFF0A4A63), Color(0xFF00C8FF)))
private val PurplePalette = Palette(Color(0xFF0C0718), listOf(Color(0xFF170D33), Color(0xFF34207A), Color(0xFF6A3CFF)))
private val SynthwavePalette = Palette(
    FractalPalette.Night,
    listOf(FractalPalette.Night, FractalPalette.Purple, FractalPalette.Magenta, FractalPalette.Cyan),
)

/**
 * Draws the wallpaper [id] full size.
 * Animated wallpapers run only while the screen is resumed and are frozen when [economy] is on.
 */
@Composable
fun Wallpaper(
    id: WallpaperId,
    customImagePath: String?,
    economy: Boolean,
    fallback: Color,
    modifier: Modifier = Modifier,
) {
    when (id) {
        WallpaperId.NONE -> Box(modifier.fillMaxSize().background(fallback))
        WallpaperId.STATIC_INK -> StaticFacets(InkPalette, seed = 11, modifier)
        WallpaperId.STATIC_MAGENTA -> StaticFacets(MagentaPalette, seed = 23, modifier)
        WallpaperId.STATIC_CYAN -> StaticFacets(CyanPalette, seed = 37, modifier)
        WallpaperId.STATIC_PURPLE -> StaticFacets(PurplePalette, seed = 41, modifier)
        WallpaperId.STATIC_SYNTHWAVE -> StaticFacets(SynthwavePalette, seed = 53, modifier)
        WallpaperId.CUSTOM -> CustomImage(customImagePath, fallback, modifier)
        else -> {
            val time = animationClock(enabled = !economy)
            AnimatedWallpaper(id, time = { time.floatValue }, modifier)
        }
    }
}

/** Seconds since start, advanced at most [ANIMATION_FPS] times per second while resumed. */
@Composable
private fun animationClock(enabled: Boolean): androidx.compose.runtime.MutableFloatState {
    val time = remember { mutableFloatStateOf(0f) }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val state by lifecycle.currentStateFlow.collectAsState()
    val running = enabled && state.isAtLeast(Lifecycle.State.RESUMED)
    LaunchedEffect(running) {
        if (!running) return@LaunchedEffect
        var last = 0L
        while (true) {
            withFrameNanos { now ->
                if (last == 0L) last = now
                val elapsed = now - last
                // 2 ms of slack: vsync intervals jitter, and a strict check would wait one more vsync (~20 fps at 60 Hz)
                if (elapsed >= FRAME_NANOS - FRAME_SLACK_NANOS) {
                    // clamp so a long pause doesn't jump the animation
                    time.floatValue += min(elapsed, FRAME_NANOS * 4) / 1e9f
                    last = now
                }
            }
        }
    }
    return time
}

// region low-poly mesh

private class Mesh(val points: Array<Offset>, val cols: Int, val rows: Int, val shade: FloatArray) {
    /** Triangles as indices into [points], two per grid cell. */
    val triangles: IntArray = IntArray((cols - 1) * (rows - 1) * 6).also { t ->
        var i = 0
        for (r in 0 until rows - 1) for (c in 0 until cols - 1) {
            val a = r * cols + c
            val b = a + 1
            val d = a + cols
            val e = d + 1
            // alternate the diagonal for a less regular look
            if ((r + c) % 2 == 0) {
                t[i++] = a; t[i++] = b; t[i++] = e
                t[i++] = a; t[i++] = e; t[i++] = d
            } else {
                t[i++] = a; t[i++] = b; t[i++] = d
                t[i++] = b; t[i++] = e; t[i++] = d
            }
        }
    }
}

private fun buildMesh(size: Size, cell: Float, seed: Int): Mesh {
    val rnd = Random(seed)
    val cols = (size.width / cell).toInt() + 3
    val rows = (size.height / cell).toInt() + 3
    val points = Array(cols * rows) { i ->
        val c = i % cols
        val r = i / cols
        val jitter = cell * 0.38f
        Offset(
            (c - 1) * cell + (rnd.nextFloat() - 0.5f) * 2 * jitter,
            (r - 1) * cell + (rnd.nextFloat() - 0.5f) * 2 * jitter,
        )
    }
    val shade = FloatArray((cols - 1) * (rows - 1) * 2) { rnd.nextFloat() }
    return Mesh(points, cols, rows, shade)
}

private fun Palette.colorAt(t: Float): Color {
    val x = t.coerceIn(0f, 1f) * (stops.size - 1)
    val i = min(x.toInt(), stops.size - 2)
    return lerp(stops[i], stops[i + 1], x - i)
}

private fun DrawScope.drawFacet(path: Path, a: Offset, b: Offset, c: Offset, color: Color) {
    path.reset()
    path.moveTo(a.x, a.y)
    path.lineTo(b.x, b.y)
    path.lineTo(c.x, c.y)
    path.close()
    drawPath(path, color)
}

// endregion

@Composable
private fun StaticFacets(palette: Palette, seed: Int, modifier: Modifier) {
    Spacer(
        modifier
            .fillMaxSize()
            .drawWithCache {
                val mesh = buildMesh(size, cell = max(size.width, size.height) / 9f, seed = seed)
                val path = Path()
                onDrawBehind {
                    drawRect(palette.base)
                    val tri = mesh.triangles
                    for (k in 0 until tri.size / 3) {
                        val a = mesh.points[tri[k * 3]]
                        val b = mesh.points[tri[k * 3 + 1]]
                        val c = mesh.points[tri[k * 3 + 2]]
                        val cx = (a.x + b.x + c.x) / 3f
                        val cy = (a.y + b.y + c.y) / 3f
                        // diagonal gradient, lighter towards the bottom right, per facet shade
                        val t = (cx / size.width) * 0.55f + (cy / size.height) * 0.45f
                        val base = palette.colorAt(t * 0.85f + mesh.shade[k] * 0.15f)
                        val color = lerp(palette.base, base, 0.35f + mesh.shade[k] * 0.4f)
                        drawFacet(path, a, b, c, color)
                    }
                    // darken the left side so text stays readable
                    drawRect(
                        Brush.horizontalGradient(
                            0f to Color.Black.copy(alpha = 0.35f),
                            0.6f to Color.Transparent,
                        )
                    )
                }
            }
    )
}

@Composable
private fun AnimatedWallpaper(id: WallpaperId, time: () -> Float, modifier: Modifier) {
    val palette = when (id) {
        WallpaperId.ANIMATED_DRIFT -> PurplePalette
        WallpaperId.ANIMATED_BREATHE -> MagentaPalette
        WallpaperId.ANIMATED_SIERPINSKI -> InkPalette
        WallpaperId.ANIMATED_WAVE -> CyanPalette
        else -> SynthwavePalette
    }
    Spacer(
        modifier
            .fillMaxSize()
            .drawWithCache {
                val path = Path()
                val mesh = buildMesh(size, cell = max(size.width, size.height) / 10f, seed = id.ordinal * 7 + 3)
                val drifters = List(28) { DriftTriangle(Random(id.ordinal * 101 + it)) }
                onDrawBehind {
                    val t = time()
                    drawRect(palette.base)
                    when (id) {
                        WallpaperId.ANIMATED_DRIFT -> drawDrift(drifters, palette, t, path)
                        WallpaperId.ANIMATED_BREATHE -> drawBreathe(mesh, palette, t, path)
                        WallpaperId.ANIMATED_SIERPINSKI -> drawSierpinskiZoom(palette, t, path)
                        WallpaperId.ANIMATED_WAVE -> drawWave(mesh, palette, t, path)
                        else -> drawParticles(drifters, palette, t, path)
                    }
                }
            }
    )
}

private class DriftTriangle(rnd: Random) {
    val x = rnd.nextFloat()
    val y = rnd.nextFloat()
    val radius = 0.03f + rnd.nextFloat() * 0.09f
    val speedX = (rnd.nextFloat() - 0.5f) * 0.012f
    val speedY = 0.004f + rnd.nextFloat() * 0.01f
    val spin = (rnd.nextFloat() - 0.5f) * 14f
    val phase = rnd.nextFloat() * 360f
    val tone = rnd.nextFloat()
    val filled = rnd.nextFloat() < 0.45f
}

private fun DrawScope.triangle(path: Path, center: Offset, radius: Float, angleDeg: Float) {
    path.reset()
    for (i in 0 until 3) {
        val a = (angleDeg + i * 120f - 90f) * (PI / 180f).toFloat()
        val p = Offset(center.x + cos(a) * radius, center.y + sin(a) * radius)
        if (i == 0) path.moveTo(p.x, p.y) else path.lineTo(p.x, p.y)
    }
    path.close()
}

private fun wrap(v: Float) = ((v % 1f) + 1f) % 1f

private fun DrawScope.drawDrift(items: List<DriftTriangle>, palette: Palette, t: Float, path: Path) {
    val s = max(size.width, size.height)
    for (d in items) {
        val center = Offset(wrap(d.x + d.speedX * t) * size.width, wrap(d.y - d.speedY * t) * size.height)
        triangle(path, center, d.radius * s, d.phase + d.spin * t)
        val color = palette.colorAt(0.4f + d.tone * 0.6f)
        if (d.filled) drawPath(path, color.copy(alpha = 0.18f))
        drawPath(path, color.copy(alpha = 0.55f), style = Stroke(width = 2f))
    }
}

private fun DrawScope.drawParticles(items: List<DriftTriangle>, palette: Palette, t: Float, path: Path) {
    val s = max(size.width, size.height)
    for (k in 0 until 3) for (d in items) {
        val rise = wrap(d.y - (d.speedY * 3f + k * 0.01f) * t + k * 0.33f)
        val center = Offset(wrap(d.x + k * 0.29f + sin(t * 0.5f + d.phase) * 0.01f) * size.width, rise * size.height)
        // fade in at the bottom and out at the top
        val alpha = (sin(rise * PI.toFloat())).coerceIn(0f, 1f)
        triangle(path, center, d.radius * s * 0.18f, d.phase + d.spin * 3f * t)
        drawPath(path, palette.colorAt(0.35f + d.tone * 0.65f).copy(alpha = 0.75f * alpha))
    }
}

private fun DrawScope.drawBreathe(mesh: Mesh, palette: Palette, t: Float, path: Path) {
    val tri = mesh.triangles
    for (k in 0 until tri.size / 3) {
        val a = mesh.points[tri[k * 3]]
        val b = mesh.points[tri[k * 3 + 1]]
        val c = mesh.points[tri[k * 3 + 2]]
        val breath = 0.5f + 0.5f * sin(t * 0.6f + mesh.shade[k] * 6.283f)
        val cy = (a.y + b.y + c.y) / 3f / size.height
        val color = lerp(palette.base, palette.colorAt(0.2f + cy * 0.5f + breath * 0.3f), 0.25f + breath * 0.35f)
        drawFacet(path, a, b, c, color)
    }
}

private fun DrawScope.drawWave(mesh: Mesh, palette: Palette, t: Float, path: Path) {
    val tri = mesh.triangles
    val amp = max(size.width, size.height) / 60f
    fun moved(i: Int): Offset {
        val p = mesh.points[i]
        val w = sin(p.x / size.width * 6f + t * 0.9f) + cos(p.y / size.height * 5f + t * 0.7f)
        return Offset(p.x, p.y + w * amp)
    }
    for (k in 0 until tri.size / 3) {
        val a = moved(tri[k * 3])
        val b = moved(tri[k * 3 + 1])
        val c = moved(tri[k * 3 + 2])
        // shade facets by their slope, like light from above
        val nz = ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x))
        val slope = ((b.y + c.y - 2 * a.y) / (amp * 6f)).coerceIn(-1f, 1f)
        val light = (0.5f + slope * 0.5f + if (nz > 0) 0.05f else 0f).coerceIn(0f, 1f)
        val color = lerp(palette.base, palette.colorAt(0.25f + light * 0.6f), 0.3f + light * 0.45f)
        drawFacet(path, a, b, c, color)
    }
}

/** Sierpinski triangle zooming into its top vertex; the picture repeats every doubling. */
private fun DrawScope.drawSierpinskiZoom(palette: Palette, t: Float, path: Path) {
    val period = 24f
    val phase = (t % period) / period
    val zoom = Math.pow(2.0, phase.toDouble()).toFloat()
    val side = min(size.width, size.height) * 1.15f
    val h = side * 0.866f
    val top = Offset(size.width / 2f, (size.height - h) / 2f)
    scale(zoom, pivot = top) {
        // the ink palette tops out at the same dark plum the facets start from, so blend toward the brand magenta
        val accent = FractalPalette.Magenta
        fun draw(a: Offset, b: Offset, c: Offset, depth: Int) {
            if (depth == 0) {
                path.reset()
                path.moveTo(a.x, a.y); path.lineTo(b.x, b.y); path.lineTo(c.x, c.y); path.close()
                val tone = ((a.y - top.y) / h).coerceIn(0f, 1f)
                drawPath(path, lerp(Color(0xFF2A1630), accent, 0.08f + tone * 0.22f))
                return
            }
            val ab = (a + b) / 2f
            val bc = (b + c) / 2f
            val ca = (c + a) / 2f
            draw(a, ab, ca, depth - 1)
            draw(ab, b, bc, depth - 1)
            draw(ca, bc, c, depth - 1)
        }
        draw(top, Offset(top.x + side / 2f, top.y + h), Offset(top.x - side / 2f, top.y + h), 6)
    }
}

@Composable
private fun CustomImage(path: String?, fallback: Color, modifier: Modifier) {
    val image by produceState<ImageBitmap?>(null, path) {
        value = path?.let { decodeScaled(it) }
    }
    Box(modifier.fillMaxSize().background(fallback)) {
        image?.let {
            Image(it, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
        }
        Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.35f)))
    }
}

private suspend fun decodeScaled(path: String, maxSide: Int = 1920): ImageBitmap? = withContext(Dispatchers.IO) {
    try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(path, bounds)
        var sample = 1
        while (max(bounds.outWidth, bounds.outHeight) / (sample * 2) >= maxSide) sample *= 2
        BitmapFactory.decodeFile(path, BitmapFactory.Options().apply { inSampleSize = sample })?.asImageBitmap()
    } catch (_: Exception) {
        null
    }
}

