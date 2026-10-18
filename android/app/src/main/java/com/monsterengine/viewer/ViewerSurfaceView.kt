package com.monsterengine.viewer

import android.content.Context
import android.view.SurfaceHolder
import android.view.SurfaceView

/** Surface callbacks serialize attach/detach edges on the UI thread. Native detach
 * is the sole release authority; a timeout stays pending and blocks replacement. */
class ViewerSurfaceView(context: Context, private val controller: ViewerController) : SurfaceView(context), SurfaceHolder.Callback {
    private var generation = 0L
    private val touch = TouchCameraController({ generation }, controller::cameraInput)
    init {
        holder.addCallback(this)
        setOnTouchListener { _, event -> touch.onTouch(event) }
        contentDescription = "三维场景；单指旋转，双指平移或缩放，双击复位"
        isFocusable = true
    }
    override fun surfaceCreated(holder: SurfaceHolder) { generation=controller.newSurfaceGeneration() }
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (generation <= 0 || width <= 0 || height <= 0) return
        touch.resize(width,height);controller.attachSurface(holder.surface,generation,width,height)
    }
    override fun surfaceDestroyed(holder: SurfaceHolder) {
        touch.cancel()
        if (generation > 0) controller.detachSurface(generation) // synchronous native old-window ACK attempt
        generation=0
    }
    fun resetCamera() = touch.reset()
}
