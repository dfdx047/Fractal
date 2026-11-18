package dev.dfd.fractal.common.settings

import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/*
 * Default layout of the on-screen controls, computed from the screen instead of fixed offsets.
 *
 * Everything here is in dp and has no Android dependency, so it is unit tested on the JVM.
 * The controls get a comfortable physical size (scaled a little with the screen's short side),
 * are anchored to the corners and the bottom edge, and the whole layout shrinks step by step
 * until nothing overlaps and everything stays inside the safe area (cutouts, bars).
 */

/** Rectangle in dp. */
data class LayoutBox(val left: Float, val top: Float, val right: Float, val bottom: Float) {
    val width: Float get() = right - left
    val height: Float get() = bottom - top
    val centerX: Float get() = (left + right) * 0.5f
    val centerY: Float get() = (top + bottom) * 0.5f

    fun isInside(outer: LayoutBox, tolerance: Float = 0.5f): Boolean =
        left >= outer.left - tolerance && top >= outer.top - tolerance &&
            right <= outer.right + tolerance && bottom <= outer.bottom + tolerance

    companion object {
        fun sized(left: Float, top: Float, width: Float, height: Float) =
            LayoutBox(left, top, left + width, top + height)

        fun centered(centerX: Float, centerY: Float, width: Float, height: Float) =
            LayoutBox(centerX - width / 2f, centerY - height / 2f, centerX + width / 2f, centerY + height / 2f)
    }
}

/** A placed control: its box and whether it is round (sticks, d-pad, round buttons) or a rectangle. */
data class LayoutShape(val box: LayoutBox, val round: Boolean) {
    /** Shortest distance between the outlines; negative when they overlap. */
    fun distanceTo(other: LayoutShape): Float = when {
        round && other.round -> {
            val d = hypot(box.centerX - other.box.centerX, box.centerY - other.box.centerY)
            d - box.width / 2f - other.box.width / 2f
        }

        round -> circleToBox(box, other.box)
        other.round -> circleToBox(other.box, box)
        else -> {
            val dx = max(other.box.left - box.right, box.left - other.box.right)
            val dy = max(other.box.top - box.bottom, box.top - other.box.bottom)
            if (dx < 0f && dy < 0f) max(dx, dy) else hypot(max(dx, 0f), max(dy, 0f))
        }
    }

    private fun circleToBox(circle: LayoutBox, rect: LayoutBox): Float {
        val cx = circle.centerX
        val cy = circle.centerY
        val nearestX = cx.coerceIn(rect.left, rect.right)
        val nearestY = cy.coerceIn(rect.top, rect.bottom)
        val radius = circle.width / 2f
        if (nearestX == cx && nearestY == cy) {
            // center inside the rectangle
            return -radius
        }
        return hypot(cx - nearestX, cy - nearestY) - radius
    }
}

private fun hypot(x: Float, y: Float): Float = sqrt(x * x + y * y)

/** Space taken by cutouts and bars on each side, in dp. */
data class LayoutInsets(val left: Float = 0f, val top: Float = 0f, val right: Float = 0f, val bottom: Float = 0f) {
    companion object {
        val NONE = LayoutInsets()
    }
}

/** What the adaptive layout is computed for. */
data class OverlayLayoutSpec(
    val widthDp: Float,
    val heightDp: Float,
    val controllerType: OverlayControllerType,
    /** Pixels per dp, to convert the result to pixels. */
    val density: Float = 1f,
    val insets: LayoutInsets = LayoutInsets.NONE,
    /** Aspect ratio of the game image; in portrait the controls go below it. */
    val contentAspect: Float = 16f / 9f,
    /** The overlay size setting; the layout is fitted with it, so bigger controls never overlap. */
    val sizeScale: Float = 1f,
    /** Height from the top, in dp, of the stats box in the top left corner; the left shoulders go below it. */
    val reservedTopLeftDp: Float = 0f,
)

/** How the two clusters (stick + d-pad on the left, buttons + stick on the right) are arranged. */
enum class ClusterArrangement {
    /** Primary control at the outer edge, secondary lower and further in. Landscape. */
    SIDE_BY_SIDE,

    /** Primary control above the secondary one. Portrait and square screens. */
    STACKED,
}

/** How L/ZL and R/ZR sit in the top corners. */
enum class ShoulderArrangement {
    /** ZL above L, along the side edge. */
    COLUMN,

    /** ZL in the corner, L next to it. */
    ROW,
}

/** Where -, + (and Home, mic) go. */
enum class CenterPlacement { BOTTOM, TOP, COLUMN }

/** Inputs shown on the overlay for a controller of [type], in drawing order. */
fun overlayInputsFor(type: OverlayControllerType): List<OverlayInputConfig> = buildList {
    add(OverlayInputConfig.BUTTON_MINUS)
    add(OverlayInputConfig.BUTTON_PLUS)
    add(OverlayInputConfig.DPAD)
    add(OverlayInputConfig.BUTTON_A)
    add(OverlayInputConfig.BUTTON_B)
    add(OverlayInputConfig.JOYSTICK_RIGHT)
    if (type != OverlayControllerType.WIIMOTE) {
        add(OverlayInputConfig.BUTTON_X)
        add(OverlayInputConfig.BUTTON_Y)
        add(OverlayInputConfig.BUTTON_ZL)
        add(OverlayInputConfig.BUTTON_ZR)
        add(OverlayInputConfig.BUTTON_L)
        add(OverlayInputConfig.BUTTON_R)
        add(OverlayInputConfig.JOYSTICK_LEFT)
    } else {
        add(OverlayInputConfig.BUTTON_ONE)
        add(OverlayInputConfig.BUTTON_TWO)
        add(OverlayInputConfig.BUTTON_C)
        add(OverlayInputConfig.BUTTON_Z)
        add(OverlayInputConfig.BUTTON_HOME)
    }
    if (type == OverlayControllerType.VPAD || type == OverlayControllerType.PRO) {
        add(OverlayInputConfig.BUTTON_L_STICK_CLICK)
        add(OverlayInputConfig.BUTTON_R_STICK_CLICK)
    }
    if (type == OverlayControllerType.VPAD) {
        add(OverlayInputConfig.BUTTON_BLOW_MIC)
    }
}

/** Inputs drawn as rectangles by default: shoulder buttons and triggers. */
fun OverlayInputConfig.isRectangular(): Boolean = when (this) {
    OverlayInputConfig.BUTTON_L, OverlayInputConfig.BUTTON_R,
    OverlayInputConfig.BUTTON_ZL, OverlayInputConfig.BUTTON_ZR,
    OverlayInputConfig.BUTTON_Z, OverlayInputConfig.BUTTON_C -> true

    else -> false
}

/** Result of [computeAdaptiveLayout]. */
class AdaptiveOverlayLayout internal constructor(
    val spec: OverlayLayoutSpec,
    /** Size factor actually used, 1 = the reference phone size. */
    val unit: Float,
    val arrangement: ClusterArrangement,
    val shoulders: ShoulderArrangement,
    val center: CenterPlacement,
    val isPortrait: Boolean,
    val safeArea: LayoutBox,
    /** Where the controls live: the safe area, or the band below the game in portrait. */
    val controlArea: LayoutBox,
    val boxes: Map<OverlayInputConfig, LayoutBox>,
    private val extraSlots: List<LayoutBox>,
    /** False when even the smallest size could not avoid overlaps (tiny screens). */
    val fits: Boolean,
) {
    val shapes: Map<OverlayInputConfig, LayoutShape>
        get() = boxes.mapValues { (config, box) -> LayoutShape(box, !config.isRectangular()) }

    /** Free spot for the [index]th user made button (macro, shortcut) that was never moved. */
    fun extraSlot(index: Int): LayoutBox {
        if (index < extraSlots.size) return extraSlots[index]
        // more buttons than free spots: pile them up in the middle, slightly apart
        val cell = extraCellSize(unit)
        val k = index - extraSlots.size
        return LayoutBox.centered(
            controlArea.centerX + (k % 4 - 1.5f) * (cell.first + 4f),
            controlArea.centerY + (k / 4) * (cell.second + 4f),
            cell.first,
            cell.second,
        )
    }

    val extraSlotCount: Int get() = extraSlots.size

    /** [box] in the resolution independent form saved in profiles, before the size setting. */
    fun normalized(box: LayoutBox): NormalizedRect {
        val shortSide = max(min(spec.widthDp, spec.heightDp), 1f)
        val scale = max(spec.sizeScale, 0.01f)
        return NormalizedRect(
            centerX = box.centerX / max(spec.widthDp, 1f),
            centerY = box.centerY / max(spec.heightDp, 1f),
            width = box.width / shortSide / scale,
            height = box.height / shortSide / scale,
        )
    }

    fun normalized(config: OverlayInputConfig): NormalizedRect? = boxes[config]?.let { normalized(it) }
}

// Sizes at unit 1, in dp. The reference is a phone with a 400 dp short side (about 6.5").
private const val REFERENCE_SHORT_SIDE_DP = 400f
private const val UNIT_MIN = 0.85f
private const val UNIT_MAX = 1.3f
private const val UNIT_FLOOR = 0.55f
private const val SHRINK_STEP = 0.97f

private const val STICK = 136f
private const val DPAD = 128f
private const val FACE = 56f
private const val FACE_SPREAD = 52f
private const val SHOULDER_W = 88f
private const val SHOULDER_H = 42f
private const val SMALL = 44f
private const val MARGIN = 16f
private const val GAP = 12f
private const val EXTRA_W = 80f
private const val EXTRA_H = 40f

/** Secondary control of a SIDE_BY_SIDE cluster sits this fraction of its size lower than the primary. */
private const val SIDE_LIFT = 0.42f

/** Secondary control of a STACKED cluster is pushed in by this fraction of the primary size (thumb arc). */
private const val STACK_INWARD = 0.1f

/** Controls closer than this fraction of the gap count as overlapping. */
private const val MIN_SEPARATION = 0.5f

private fun extraCellSize(unit: Float) = Pair(EXTRA_W * unit, EXTRA_H * unit)

/** Unit the layout starts from: the reference size, a little bigger on tablets, times the size setting. */
fun targetLayoutUnit(spec: OverlayLayoutSpec): Float {
    val safeW = spec.widthDp - spec.insets.left - spec.insets.right
    val safeH = spec.heightDp - spec.insets.top - spec.insets.bottom
    val shortSide = min(safeW, safeH)
    return (shortSide / REFERENCE_SHORT_SIDE_DP).coerceIn(UNIT_MIN, UNIT_MAX) * spec.sizeScale.coerceIn(0.3f, 3f)
}

private data class Plan(val arrangement: ClusterArrangement, val minFraction: Float)

/** Computes the default layout for [spec]. Pure and fast enough to run on every size change. */
fun computeAdaptiveLayout(spec: OverlayLayoutSpec): AdaptiveOverlayLayout {
    val w = max(spec.widthDp, 1f)
    val h = max(spec.heightDp, 1f)
    val safe = LayoutBox(
        spec.insets.left.coerceIn(0f, w / 3f),
        spec.insets.top.coerceIn(0f, h / 3f),
        w - spec.insets.right.coerceIn(0f, w / 3f),
        h - spec.insets.bottom.coerceIn(0f, h / 3f),
    )
    val portrait = safe.height > safe.width
    val target = targetLayoutUnit(spec)
    val plans = if (portrait) {
        listOf(Plan(ClusterArrangement.STACKED, 0f), Plan(ClusterArrangement.SIDE_BY_SIDE, 0f))
    } else {
        // side by side keeps the image clear; stacked only when that would make the controls too small
        listOf(
            Plan(ClusterArrangement.SIDE_BY_SIDE, 0.8f),
            Plan(ClusterArrangement.STACKED, 0f),
            Plan(ClusterArrangement.SIDE_BY_SIDE, 0f),
        )
    }
    var last: AdaptiveOverlayLayout? = null
    for (plan in plans) {
        val floor = max(UNIT_FLOOR, target * plan.minFraction)
        var unit = target
        while (true) {
            for (shoulders in ShoulderArrangement.entries) {
                val layout = place(spec, safe, portrait, unit, plan.arrangement, shoulders)
                if (layout != null) {
                    if (layout.fits) return layout
                    last = layout
                }
            }
            if (unit <= floor) break
            unit = max(unit * SHRINK_STEP, floor)
        }
    }
    return last ?: place(spec, safe, portrait, UNIT_FLOOR, ClusterArrangement.STACKED, ShoulderArrangement.COLUMN)!!
}

private class Placer(val unit: Float) {
    val boxes = LinkedHashMap<OverlayInputConfig, LayoutBox>()
    val stick = STICK * unit
    val dpad = DPAD * unit
    val face = FACE * unit
    val faceSpread = FACE_SPREAD * unit
    val faceCluster = 2 * faceSpread + face
    val shoulderW = SHOULDER_W * unit
    val shoulderH = SHOULDER_H * unit
    val small = SMALL * unit
    val margin = MARGIN * unit
    val gap = GAP * unit

    /** True when no two controls are closer than half a gap. */
    fun noOverlaps(): Boolean {
        val shapes = boxes.map { (config, box) -> LayoutShape(box, !config.isRectangular()) }
        val minDistance = gap * MIN_SEPARATION
        for (i in shapes.indices) {
            for (j in i + 1 until shapes.size) {
                if (shapes[i].distanceTo(shapes[j]) < minDistance) return false
            }
        }
        return true
    }

    fun collides(candidate: LayoutShape, others: Iterable<LayoutShape>): Boolean {
        val minDistance = gap * MIN_SEPARATION
        return others.any { candidate.distanceTo(it) < minDistance }
    }
}

/** Places every control at [unit]; returns null when the arrangement can't be built at all. */
private fun place(
    spec: OverlayLayoutSpec,
    safe: LayoutBox,
    portrait: Boolean,
    unit: Float,
    arrangement: ClusterArrangement,
    shoulders: ShoulderArrangement,
): AdaptiveOverlayLayout? {
    val type = spec.controllerType
    val p = Placer(unit)
    val isWiimote = type == OverlayControllerType.WIIMOTE

    // height the controls need, to keep the portrait band compact (thumbs don't reach far up)
    val shoulderBlock = p.margin + p.shoulderH + (if (shoulders == ShoulderArrangement.COLUMN) p.gap + p.shoulderH else 0f)
    val leftBlock = if (arrangement == ClusterArrangement.STACKED) p.stick + p.gap + p.dpad else p.stick + SIDE_LIFT * p.dpad
    val rightBlock = when {
        isWiimote -> p.faceCluster + p.gap
        arrangement == ClusterArrangement.STACKED -> p.faceCluster + p.gap + p.stick
        else -> p.faceCluster + SIDE_LIFT * p.stick
    }
    val needed = shoulderBlock + p.gap * 2 + max(leftBlock, rightBlock) + p.margin

    val area = if (portrait) {
        val gameBottom = safe.top + safe.width / spec.contentAspect
        val top = max(gameBottom + p.gap, safe.bottom - needed * 1.2f).coerceAtMost(safe.bottom - needed).coerceAtLeast(safe.top)
        LayoutBox(safe.left, top, safe.right, safe.bottom)
    } else {
        safe
    }

    // ---- top corners: shoulders and triggers ----
    val leftOuter = if (isWiimote) OverlayInputConfig.BUTTON_Z else OverlayInputConfig.BUTTON_ZL
    val leftInner = if (isWiimote) OverlayInputConfig.BUTTON_C else OverlayInputConfig.BUTTON_L
    // the left pair goes below the stats box when it is in that corner
    val leftTop = max(area.top + p.margin, spec.reservedTopLeftDp + p.gap)
    placeShoulders(p, area, shoulders, leftOuter, leftInner, mirrored = false, top = leftTop)
    if (!isWiimote) {
        placeShoulders(p, area, shoulders, OverlayInputConfig.BUTTON_ZR, OverlayInputConfig.BUTTON_R, mirrored = true, top = area.top + p.margin)
    }

    // ---- left cluster: movement stick and d-pad (the nunchuk stick on a Wiimote) ----
    val leftStick = if (isWiimote) OverlayInputConfig.JOYSTICK_RIGHT else OverlayInputConfig.JOYSTICK_LEFT
    val stickBox: LayoutBox
    val dpadBox: LayoutBox
    if (arrangement == ClusterArrangement.SIDE_BY_SIDE) {
        stickBox = LayoutBox.sized(area.left + p.margin, area.bottom - p.margin - SIDE_LIFT * p.dpad - p.stick, p.stick, p.stick)
        dpadBox = LayoutBox.sized(stickBox.right + p.gap, area.bottom - p.margin - p.dpad, p.dpad, p.dpad)
    } else {
        dpadBox = LayoutBox.sized(
            area.left + p.margin + max(0f, (p.stick - p.dpad) / 2f) + STACK_INWARD * p.stick,
            area.bottom - p.margin - p.dpad,
            p.dpad,
            p.dpad,
        )
        stickBox = LayoutBox.sized(area.left + p.margin, dpadBox.top - p.gap - p.stick, p.stick, p.stick)
    }
    p.boxes[leftStick] = stickBox
    p.boxes[OverlayInputConfig.DPAD] = dpadBox

    // ---- right cluster: face buttons and camera stick ----
    val faceBox: LayoutBox
    var rightStickBox: LayoutBox? = null
    if (isWiimote) {
        val lift = if (arrangement == ClusterArrangement.SIDE_BY_SIDE) SIDE_LIFT * p.dpad * 0.5f else p.gap
        faceBox = LayoutBox(area.right - p.margin - p.faceCluster, area.bottom - p.margin - lift - p.faceCluster, area.right - p.margin, area.bottom - p.margin - lift)
    } else if (arrangement == ClusterArrangement.SIDE_BY_SIDE) {
        faceBox = LayoutBox.sized(
            area.right - p.margin - p.faceCluster,
            area.bottom - p.margin - SIDE_LIFT * p.stick - p.faceCluster,
            p.faceCluster,
            p.faceCluster,
        )
        rightStickBox = LayoutBox.sized(faceBox.left - p.gap - p.stick, area.bottom - p.margin - p.stick, p.stick, p.stick)
    } else {
        val rightStickRight = area.right - p.margin - max(0f, (p.faceCluster - p.stick) / 2f) - STACK_INWARD * p.faceCluster
        rightStickBox = LayoutBox(rightStickRight - p.stick, area.bottom - p.margin - p.stick, rightStickRight, area.bottom - p.margin)
        faceBox = LayoutBox.sized(area.right - p.margin - p.faceCluster, rightStickBox.top - p.gap - p.faceCluster, p.faceCluster, p.faceCluster)
    }
    rightStickBox?.let { p.boxes[OverlayInputConfig.JOYSTICK_RIGHT] = it }
    val north: OverlayInputConfig
    val south: OverlayInputConfig
    val west: OverlayInputConfig
    val east: OverlayInputConfig
    if (isWiimote) {
        north = OverlayInputConfig.BUTTON_TWO
        south = OverlayInputConfig.BUTTON_B
        west = OverlayInputConfig.BUTTON_ONE
        east = OverlayInputConfig.BUTTON_A
    } else {
        // Nintendo diamond
        north = OverlayInputConfig.BUTTON_X
        south = OverlayInputConfig.BUTTON_B
        west = OverlayInputConfig.BUTTON_Y
        east = OverlayInputConfig.BUTTON_A
    }
    val fcx = faceBox.centerX
    val fcy = faceBox.centerY
    p.boxes[north] = LayoutBox.centered(fcx, fcy - p.faceSpread, p.face, p.face)
    p.boxes[south] = LayoutBox.centered(fcx, fcy + p.faceSpread, p.face, p.face)
    p.boxes[west] = LayoutBox.centered(fcx - p.faceSpread, fcy, p.face, p.face)
    p.boxes[east] = LayoutBox.centered(fcx + p.faceSpread, fcy, p.face, p.face)

    // ---- stick clicks, next to their sticks ----
    if (type == OverlayControllerType.VPAD || type == OverlayControllerType.PRO) {
        val rs = rightStickBox!!
        if (arrangement == ClusterArrangement.SIDE_BY_SIDE) {
            // the bottom corners, under the lifted stick and face buttons: away from the image
            val top = area.bottom - p.margin - p.small
            p.boxes[OverlayInputConfig.BUTTON_L_STICK_CLICK] = LayoutBox.sized(area.left + p.margin, top, p.small, p.small)
            p.boxes[OverlayInputConfig.BUTTON_R_STICK_CLICK] =
                LayoutBox.sized(area.right - p.margin - p.small, top, p.small, p.small)
        } else {
            p.boxes[OverlayInputConfig.BUTTON_L_STICK_CLICK] =
                LayoutBox.sized(stickBox.right + p.gap, stickBox.top, p.small, p.small)
            p.boxes[OverlayInputConfig.BUTTON_R_STICK_CLICK] =
                LayoutBox.sized(rs.left - p.gap - p.small, rs.top, p.small, p.small)
        }
    }

    // ---- -, Home, + and the mic in the middle ----
    val centerItems = buildList {
        add(OverlayInputConfig.BUTTON_MINUS)
        if (isWiimote) add(OverlayInputConfig.BUTTON_HOME)
        add(OverlayInputConfig.BUTTON_PLUS)
    }
    val mic = if (type == OverlayControllerType.VPAD) OverlayInputConfig.BUTTON_BLOW_MIC else null
    val placedShapes = p.boxes.map { (config, box) -> LayoutShape(box, !config.isRectangular()) }
    var center: CenterPlacement? = null
    for (placement in CenterPlacement.entries) {
        val candidate = centerGroup(p, area, placement, centerItems, mic) ?: continue
        val inside = candidate.values.all { it.isInside(safe) }
        if (inside && candidate.values.none { p.collides(LayoutShape(it, true), placedShapes) } && noOverlap(p, candidate.values)) {
            p.boxes.putAll(candidate)
            center = placement
            break
        }
    }
    val fits = center != null && p.boxes.values.all { it.isInside(safe) } && p.noOverlaps()
    if (center == null) {
        // keep the result usable: a bottom row, overlapping
        p.boxes.putAll(centerGroup(p, area, CenterPlacement.BOTTOM, centerItems, mic)!!)
    }

    val extras = findExtraSlots(p, area, safe)
    return AdaptiveOverlayLayout(
        spec = spec,
        unit = unit,
        arrangement = arrangement,
        shoulders = shoulders,
        center = center ?: CenterPlacement.BOTTOM,
        isPortrait = portrait,
        safeArea = safe,
        controlArea = area,
        boxes = p.boxes,
        extraSlots = extras,
        fits = fits,
    )
}

private fun noOverlap(p: Placer, boxes: Collection<LayoutBox>): Boolean {
    val shapes = boxes.map { LayoutShape(it, true) }
    for (i in shapes.indices) {
        for (j in i + 1 until shapes.size) {
            if (shapes[i].distanceTo(shapes[j]) < p.gap * MIN_SEPARATION) return false
        }
    }
    return true
}

private fun placeShoulders(
    p: Placer,
    area: LayoutBox,
    arrangement: ShoulderArrangement,
    outer: OverlayInputConfig,
    inner: OverlayInputConfig,
    mirrored: Boolean,
    top: Float,
) {
    fun box(leftFromEdge: Float, top: Float): LayoutBox {
        val left = if (mirrored) area.right - leftFromEdge - p.shoulderW else area.left + leftFromEdge
        return LayoutBox.sized(left, top, p.shoulderW, p.shoulderH)
    }
    p.boxes[outer] = box(p.margin, top)
    p.boxes[inner] = when (arrangement) {
        ShoulderArrangement.COLUMN -> box(p.margin, top + p.shoulderH + p.gap)
        ShoulderArrangement.ROW -> box(p.margin + p.shoulderW + p.gap, top)
    }
}

/** Boxes of the middle group for [placement], or null when it has no room at all. */
private fun centerGroup(
    p: Placer,
    area: LayoutBox,
    placement: CenterPlacement,
    items: List<OverlayInputConfig>,
    mic: OverlayInputConfig?,
): Map<OverlayInputConfig, LayoutBox>? {
    val result = LinkedHashMap<OverlayInputConfig, LayoutBox>()
    val spacing = p.gap * 1.5f
    val cx = area.centerX
    when (placement) {
        CenterPlacement.BOTTOM -> {
            val rowWidth = items.size * p.small + (items.size - 1) * spacing
            if (rowWidth > area.width) return null
            val top = area.bottom - p.margin - p.small
            var x = cx - rowWidth / 2f
            for (item in items) {
                result[item] = LayoutBox.sized(x, top, p.small, p.small)
                x += p.small + spacing
            }
            if (mic != null) result[mic] = LayoutBox.sized(cx - p.small / 2f, top - p.gap - p.small, p.small, p.small)
        }

        CenterPlacement.TOP -> {
            // one row along the top edge, the mic at its end, so nothing hangs over the image
            val all = if (mic != null) items + mic else items
            val rowWidth = all.size * p.small + (all.size - 1) * spacing
            if (rowWidth > area.width) return null
            var x = cx - rowWidth / 2f
            for (item in all) {
                result[item] = LayoutBox.sized(x, area.top + p.margin, p.small, p.small)
                x += p.small + spacing
            }
        }

        CenterPlacement.COLUMN -> {
            val all = if (mic != null) items + mic else items
            val columnHeight = all.size * p.small + (all.size - 1) * spacing
            var y = area.bottom - p.margin - columnHeight
            for (item in all) {
                result[item] = LayoutBox.sized(cx - p.small / 2f, y, p.small, p.small)
                y += p.small + spacing
            }
        }
    }
    return result
}

/** Free spots for user made buttons: rows from the top middle outwards, then further down. */
private fun findExtraSlots(p: Placer, area: LayoutBox, safe: LayoutBox): List<LayoutBox> {
    val (cellW, cellH) = extraCellSize(p.unit)
    val taken = p.boxes.map { (config, box) -> LayoutShape(box, !config.isRectangular()) }.toMutableList()
    val slots = ArrayList<LayoutBox>()
    val pitchX = cellW + p.gap
    val maxColumns = max(1, ((area.width - 2 * p.margin + p.gap) / pitchX).toInt())
    var top = area.top + p.margin
    while (top + cellH <= area.bottom - p.margin && slots.size < MAX_EXTRA_SLOTS) {
        // columns ordered from the middle outwards: 0, -1, +1, -2, +2...
        for (i in 0 until maxColumns) {
            val k = if (i % 2 == 0) i / 2 else -(i + 1) / 2
            val box = LayoutBox.centered(area.centerX + k * pitchX, top + cellH / 2f, cellW, cellH)
            if (!box.isInside(safe)) continue
            val shape = LayoutShape(box, false)
            if (p.collides(shape, taken)) continue
            slots.add(box)
            taken.add(shape)
            if (slots.size >= MAX_EXTRA_SLOTS) break
        }
        top += cellH + p.gap
    }
    return slots
}

private const val MAX_EXTRA_SLOTS = 16
