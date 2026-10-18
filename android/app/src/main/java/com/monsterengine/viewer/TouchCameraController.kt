package com.monsterengine.viewer

import android.view.MotionEvent
import kotlin.math.hypot

/** Pointer IDs, rather than array indices, own gesture history. Coordinates are
 * local to the current Surface, so a newly laid-out rotation cannot reuse deltas. */
class TouchCameraController(private val generation: () -> Long, private val send: (CameraInputState) -> Unit) {
    private data class Point(val x: Float, val y: Float)
    private val points = linkedMapOf<Int, Point>()
    private var lastTapTime = 0L
    private var lastTap = Point(-10000f, -10000f)
    private var tapStart = Point(0f, 0f)
    private var moved = false
    private var downTime = 0L
    private var width = 1
    private var height = 1

    fun resize(width: Int, height: Int) { cancel(); this.width = width.coerceAtLeast(1); this.height = height.coerceAtLeast(1) }
    fun reset() { cancel(); send(CameraInputState(reset = true, surfaceGeneration = generation())) }
    fun cancel() {
        if (points.isNotEmpty()) send(CameraInputState(ViewerPointerPhase.Cancel, -1, surfaceGeneration = generation()))
        points.clear(); moved = false; lastTapTime = 0
    }
    fun onTouch(event: MotionEvent): Boolean {
        if (generation() <= 0) return false
        val index = event.actionIndex
        val id = event.getPointerId(index)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if(points.isNotEmpty())cancel(); downTime = event.eventTime; tapStart = Point(event.x, event.y); moved = false
                // A clean down preserves the previous tap; cancellation discards it.
                points[id] = tapStart; send(CameraInputState(ViewerPointerPhase.Begin, id, surfaceGeneration = generation()))
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                moved = true; points[id] = Point(event.getX(index), event.getY(index))
                send(CameraInputState(ViewerPointerPhase.Begin, id, surfaceGeneration = generation()))
            }
            MotionEvent.ACTION_MOVE -> {
                if (points.isEmpty()) return true
                val current = linkedMapOf<Int, Point>()
                for (i in 0 until event.pointerCount) { val pointer = event.getPointerId(i)
                    if (points.containsKey(pointer)) current[pointer] = Point(event.getX(i), event.getY(i)) }
                if (current.keys != points.keys) { cancel(); return true }
                val ids = points.keys.take(2)
                if (ids.size == 1) {
                    val old = points.getValue(ids[0]); val next = current.getValue(ids[0])
                    if (hypot(next.x - tapStart.x, next.y - tapStart.y) > 12) moved = true
                    send(CameraInputState(pointerId = ids[0], orbitX = (next.x-old.x)/width,
                        orbitY = (next.y-old.y)/height, surfaceGeneration = generation()))
                } else {
                    val a = points.getValue(ids[0]); val b = points.getValue(ids[1]); val c = current.getValue(ids[0]); val d = current.getValue(ids[1])
                    val oldDistance = hypot(a.x-b.x, a.y-b.y); val distance = hypot(c.x-d.x, c.y-d.y)
                    val scale = if (oldDistance > 1 && distance > 1) (distance/oldDistance).coerceIn(0.5f,2f) else 1f
                    send(CameraInputState(pointerId = ids[0], panX = (c.x+d.x-a.x-b.x)/(2*width),
                        panY = (c.y+d.y-a.y-b.y)/(2*height), pinchScale = scale, surfaceGeneration = generation()))
                }
                points.clear(); points.putAll(current)
            }
            MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_UP -> {
                if (points.remove(id) != null) send(CameraInputState(ViewerPointerPhase.End, id, surfaceGeneration = generation()))
                // Rebase survivors by ID; adding/removing a finger never creates a movement delta.
                for (i in 0 until event.pointerCount) if (i != index) {
                    val survivor = event.getPointerId(i); if (points.containsKey(survivor)) points[survivor] = Point(event.getX(i), event.getY(i)) }
                if (event.actionMasked == MotionEvent.ACTION_UP && !moved && event.eventTime-downTime < 250) {
                    val tap = Point(event.getX(index), event.getY(index))
                    if (lastTapTime > 0 && event.eventTime-lastTapTime < 350 && hypot(tap.x-lastTap.x,tap.y-lastTap.y)<40) {
                        reset(); lastTapTime = 0
                    } else { lastTapTime=event.eventTime;lastTap=tap }
                }
            }
            MotionEvent.ACTION_CANCEL, MotionEvent.ACTION_OUTSIDE -> cancel()
        }
        return true
    }
}
