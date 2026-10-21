package com.monsterengine.viewer

import android.content.Context
import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView

/** Texture composition does not wait for a producer's first buffer before the
 * Activity can draw its controls. Failed Vulkan admission therefore leaves the
 * library and diagnostics usable. Native retirement owns producer lifetime. */
class ViewerSurfaceView(context: Context, private val controller: ViewerController) : TextureView(context), TextureView.SurfaceTextureListener {
    private class Backing(val texture: SurfaceTexture) {
        var surfaces = 0
        var destroyed = false
        private var released = false
        fun releaseIfRetired() {
            if (destroyed && surfaces == 0 && !released) { released = true; texture.release() }
        }
    }
    private var cameraInputEnabled = true
    private var backing: Backing? = null
    private var producer: Surface? = null
    private var generation = 0L
    private var bufferWidth = 0
    private var bufferHeight = 0
    private val touch = TouchCameraController({ generation }, controller::cameraInput)
    init {
        surfaceTextureListener = this
        isOpaque = true
        setOnTouchListener { _, event -> if (cameraInputEnabled) touch.onTouch(event) else true }
        contentDescription = "三维场景；单指旋转，双指平移或缩放，双击复位"
        isFocusable = true
    }
    override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
        backing = Backing(texture)
        attach(width, height)
    }
    override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) {
        retireProducer()
        attach(width, height)
    }
    private fun attach(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        val owner = backing ?: return
        // Loaded settings are authoritative. Do not allocate the temporary
        // State default's smaller buffers before initialization publishes them.
        if (!controller.state.nativeAvailable || owner.destroyed || owner.surfaces != 0) return
        val (renderWidth, renderHeight) = bufferExtent(width, height)
        owner.texture.setDefaultBufferSize(renderWidth, renderHeight)
        val surface = Surface(owner.texture)
        owner.surfaces++
        producer = surface
        generation = controller.newSurfaceGeneration()
        touch.resize(width, height)
        bufferWidth = renderWidth
        bufferHeight = renderHeight
        controller.attachSurface(surface, generation, renderWidth, renderHeight, width, height)
    }
    /** Same edge/pixel clamp as the native viewer. Only redundant full-panel
     * WSI allocation is removed; internal scene quality and camera aspect stay
     * at the existing request. Touch normalization uses visible view pixels. */
    private fun bufferExtent(width: Int, height: Int): Pair<Int, Int> {
        val config = controller.state.requested
        val scale = minOf(1.0, config.maximumInternalEdge.toDouble() / maxOf(width, height),
            kotlin.math.sqrt(config.maximumInternalPixels.toDouble() / (width.toDouble() * height)))
        return maxOf(1, (width * scale).toInt()) to maxOf(1, (height * scale).toInt())
    }
    /** Initialization or an explicit saved-quality change may change the buffer
     * bound without changing this view's layout. No visibility toggle is used. */
    fun refreshRequestedExtent() {
        if (width <= 0 || height <= 0 || backing == null) return
        val extent = bufferExtent(width, height)
        if (producer != null && extent.first == bufferWidth && extent.second == bufferHeight) return
        if (producer != null) retireProducer()
        attach(width, height)
    }
    private fun retireProducer() {
        touch.cancel()
        val surface = producer
        val owner = backing
        val retiring = generation
        producer = null
        generation = 0
        bufferWidth = 0
        bufferHeight = 0
        if (surface != null && owner != null) controller.detachSurface(retiring) {
            surface.release()
            owner.surfaces--
            owner.releaseIfRetired()
            // Recreate only after retirement, using the latest layout/settings.
            // A destroyed consumer is retained solely for release, never reuse.
            if (backing === owner && !owner.destroyed) attach(width, height)
        }
    }
    override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean {
        val owner = backing
        owner?.destroyed = true
        retireProducer()
        owner?.releaseIfRetired()
        backing = null
        // The app releases the consumer only after every native producer ACK.
        return false
    }
    override fun onSurfaceTextureUpdated(texture: SurfaceTexture) = Unit
    /** Explicit retry uses a new monotonic producer generation, never a hidden
     * visibility toggle or an automatic surface recreation loop. */
    fun retrySurface() { retireProducer(); attach(width, height) }
    /** Cancel gestures before a covering page/panel consumes subsequent camera inputs. */
    fun setCameraInputEnabled(enabled: Boolean) {
        if (cameraInputEnabled && !enabled) touch.cancel()
        cameraInputEnabled = enabled
    }
    fun resetCamera() = touch.reset()
}
