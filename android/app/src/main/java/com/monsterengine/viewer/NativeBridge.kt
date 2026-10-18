package com.monsterengine.viewer

import android.content.res.AssetManager
import android.os.Build
import android.view.Surface
import org.json.JSONArray
import org.json.JSONObject

/** Exact JNI declarations and guarded Kotlin entry points. No Activity/Surface is retained.
 * Use guarded methods in application code: loading or resolving a native symbol can fail
 * independently. Native implementations must catch C++ exceptions at every JNI boundary.
 */
object NativeBridge {
    /** Captured loader diagnostic; redact private paths before exporting this raw detail. */
    @Volatile var loadError: String? = null
        private set
    /** Bounded operation category only; never stores private paths or native detail. */
    @Volatile var lastCallError: String? = null
        private set
    /** Library loaded and no linkage failure observed; symbols are resolved lazily.
     * Says nothing about Vulkan render eligibility or uncalled JNI symbols.
     */
    @Volatile var isAvailable: Boolean = false
        private set

    init {
        try {
            System.loadLibrary("monster_android")
            isAvailable = true
        } catch (error: LinkageError) {
            loadError = error.toString()
        } catch (error: SecurityException) {
            loadError = error.toString()
        }
    }

    /** Independent staged probe; null Surface must preserve partial diagnostics. */
    external fun nativeProbe(surface: Surface?): String
    /** Create render owner; zero on failure, never returns a raw native pointer. */
    external fun nativeCreate(assets: AssetManager, filesRoot: String, configJson: String): Long
    /** Queue a new positive Surface generation and nonzero drawable extent. */
    external fun nativeAttach(handle: Long, surface: Surface, generation: Long, width: Int, height: Int): String
    /** Synchronous release acknowledgement: after success the old window is no longer in use
     * by recording, submission, GPU or presentation. Never acknowledge CPU queueing alone.
     */
    external fun nativeDetach(handle: Long, generation: Long): String
    /** Set lifecycle state without doing heavy work on the caller thread. */
    external fun nativeResume(handle: Long, resumed: Boolean)
    /** Coalesce into at most one pending owner frame token. */
    external fun nativeFrame(handle: Long, frameNanos: Long)
    /** Enqueue bounded gesture JSON; preserve Begin/End/Cancel order. */
    external fun nativeInput(handle: Long, eventJson: String)
    /** Open a lease-protected published descriptor, never parse/hash large source here. */
    external fun nativeOpenScene(handle: Long, descriptorJson: String): String
    /** Queue requested settings; native returns effective configuration separately. */
    external fun nativeSettings(handle: Long, configJson: String): String
    /** Forward Android memory pressure level to the rendering owner. */
    external fun nativeTrim(handle: Long, level: Int)
    /** Explicit manual retry; automatic device-lost retry remains bounded natively. */
    external fun nativeRetry(handle: Long): String
    /** Return immutable diagnostics or enqueue asynchronous offscreen screenshot export. */
    external fun nativeSnapshot(handle: Long, requestJson: String): String
    /** Idempotent destruction; invalidate registry before retiring ownership. */
    external fun nativeDestroy(handle: Long)
    /** CPU validation; caller must use a worker for filesystem work. */
    external fun nativeValidateScene(path: String): String
    /** Create independent CPU build job; stable private source and bounded config only. */
    external fun nativeCreateBuild(source: String, jobRoot: String, outputRoot: String, configJson: String): Long
    /** Consume bounded creation detail on the same worker as nativeCreateBuild. */
    external fun nativeTakeBuildCreationError(): String
    /** Blocking worker-only Run; control and snapshots remain concurrently callable. */
    external fun nativeRunBuild(job: Long): String
    /** Nonblocking cooperative action: pause, resume or cancel; no join/checkpoint I/O. */
    external fun nativeBuildControl(job: Long, action: String): String
    /** Copy immutable progress while Run is active. */
    external fun nativeBuildSnapshot(job: Long): String
    /** Idempotent job destruction; never invoke from service timeout callback if it joins. */
    external fun nativeDestroyBuild(job: Long)

    /** Kotlin-only report remains exportable even if no native symbol can run. */
    fun partialDiagnostics(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("state", "diagnosticsOnly").put("nativeLibraryAvailable", isAvailable)
        .put("nativeLoadErrorAvailable", loadError != null).put("sdkInt", Build.VERSION.SDK_INT)
        .put("nativeCallErrorAvailable", lastCallError != null)
        .put("supportedAbis", JSONArray(Build.SUPPORTED_ABIS.toList()))
        .put("surfaceProbe", "pending").put("capabilities", JSONArray())
        .put("timing", JSONObject().put("available", false).put("reason", "not_measured"))
        .put("memoryBudget", JSONObject().put("available", false).put("reason", "not_queried"))

    /** Safe probe fallback never invents GPU support/enablement/execution evidence. */
    fun probe(surface: Surface? = null): NativeResult {
        val result = result { nativeProbe(surface) }
        return if (result.code == "native_unavailable") result.copy(payload = partialDiagnostics()) else result
    }

    /** Create a structurally valid registry token or return the invalid zero token. */
    fun create(assets: AssetManager, filesRoot: String, config: ViewerConfig): NativeHandle {
        require(filesRoot.isNotBlank())
        return createHandle { nativeCreate(assets, filesRoot, config.toJson().toString()) }
    }

    /** Attach only valid extents; registry and Surface lifetime checks remain native. */
    fun attach(handle: NativeHandle, surface: Surface, generation: Long, width: Int, height: Int): NativeResult {
        require(generation > 0 && width > 0 && height > 0)
        return withHandle(handle) { nativeAttach(handle.value, surface, generation, width, height) }
    }

    /** Return native synchronous acknowledgement unchanged; failure cannot imply safe detach. */
    fun detach(handle: NativeHandle, generation: Long): NativeResult {
        require(generation > 0)
        return withHandle(handle) { nativeDetach(handle.value, generation) }
    }

    /** Request lifecycle transition, reporting unavailable JNI explicitly. */
    fun resume(handle: NativeHandle, resumed: Boolean): NativeResult = command(handle) { nativeResume(handle.value, resumed) }
    /** Submit a monotonic frame timestamp, without a second scheduling loop. */
    fun frame(handle: NativeHandle, frameNanos: Long): NativeResult {
        require(frameNanos >= 0)
        return command(handle) { nativeFrame(handle.value, frameNanos) }
    }
    /** Send a serialized validated gesture. */
    fun input(handle: NativeHandle, event: CameraInputState): NativeResult = command(handle) {
        nativeInput(handle.value, event.toJson().toString())
    }
    /** Request a published scene switch. */
    fun openScene(handle: NativeHandle, descriptor: SceneDescriptor): NativeResult = withHandle(handle) {
        nativeOpenScene(handle.value, descriptor.toJson().toString())
    }
    /** Apply requested settings with separate effective-state observation. */
    fun settings(handle: NativeHandle, config: ViewerConfig): NativeResult = withHandle(handle) {
        nativeSettings(handle.value, config.toJson().toString())
    }
    /** Forward memory pressure without blocking on CPU build work. */
    fun trim(handle: NativeHandle, level: Int): NativeResult = command(handle) { nativeTrim(handle.value, level) }
    /** Request an explicit recovery attempt. */
    fun retry(handle: NativeHandle): NativeResult = withHandle(handle) { nativeRetry(handle.value) }
    /** Snapshot request defaults to diagnostics without any private-path inclusion. */
    fun snapshot(handle: NativeHandle, request: JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("kind", "diagnostics").put("includePrivatePaths", false)): NativeResult = withHandle(handle) {
        nativeSnapshot(handle.value, request.toString())
    }
    /** Releasing the invalid token is a safe no-op; stale nonzero tokens are checked natively. */
    fun destroy(handle: NativeHandle): NativeResult = if (handle.value == 0L) NativeResult(true, "already_destroyed")
        else command(handle) { nativeDestroy(handle.value) }
    /** Validate only on a worker; never perform this in a Surface handshake. */
    fun validateScene(path: String): NativeResult {
        require(path.isNotBlank())
        return result { nativeValidateScene(path) }
    }
    private val buildCreationFailure = ThreadLocal<NativeResult>()

    /** Allocate a worker-owned build token; creation does not run the job.
     * Failure detail is copied immediately on this worker, before any other JNI call. */
    fun createBuild(source: String, jobRoot: String, outputRoot: String, config: BoundedBuildConfig): NativeHandle {
        buildCreationFailure.remove()
        require(source.isNotBlank() && jobRoot.isNotBlank() && outputRoot.isNotBlank())
        if (!isAvailable) { buildCreationFailure.set(unavailable()); return NativeHandle() }
        return try {
            val handle = NativeHandle(nativeCreateBuild(source, jobRoot, outputRoot, config.toJson().toString()))
            if (!handle.isValid) {
                lastCallError = "native_creation_rejected"
                buildCreationFailure.set(result { nativeTakeBuildCreationError() })
            }
            handle
        } catch (error: LinkageError) {
            recordLinkage(error); buildCreationFailure.set(unavailable()); NativeHandle()
        } catch (error: RuntimeException) {
            buildCreationFailure.set(runtimeFailure(error)); NativeHandle()
        }
    }
    /** Private job-store diagnostic; consume immediately after failed createBuild
     * on its worker. Never export this detail without private-path redaction. */
    fun takeBuildCreationFailure(): NativeResult {
        val failure = buildCreationFailure.get()
        buildCreationFailure.remove()
        return failure ?: NativeResult(false, "native_creation_rejected", "No creation diagnostic on this worker")
    }
    /** Blocking service-worker operation. Do not hold UI/registry locks across this call. */
    fun runBuild(job: NativeHandle): NativeResult = withHandle(job) { nativeRunBuild(job.value) }
    /** Concurrent nonblocking cooperative control. */
    fun buildControl(job: NativeHandle, action: String): NativeResult {
        require(action in setOf("pause", "resume", "cancel"))
        return withHandle(job) { nativeBuildControl(job.value, action) }
    }
    /** Observe a copied progress snapshot concurrently with the worker. */
    fun buildSnapshot(job: NativeHandle): NativeResult = withHandle(job) { nativeBuildSnapshot(job.value) }
    /** Worker cleanup only; Run and concurrent calls must finish before resource reclamation. */
    fun destroyBuild(job: NativeHandle): NativeResult = if (job.value == 0L) NativeResult(true, "already_destroyed")
        else command(job) { nativeDestroyBuild(job.value) }

    private fun unavailable() = NativeResult(false, "native_unavailable", "Native library or JNI symbol unavailable")
    private fun invalidHandle() = NativeResult(false, "invalid_handle", "Invalid native registry token")
    private fun recordLinkage(error: LinkageError) {
        loadError = error.toString()
        isAvailable = false
    }
    private fun runtimeFailure(error: RuntimeException): NativeResult {
        lastCallError = error.javaClass.simpleName.take(80)
        return NativeResult(false, "native_error", lastCallError.orEmpty())
    }
    private inline fun result(block: () -> String): NativeResult {
        if (!isAvailable) return unavailable()
        return try { NativeResult.fromJson(block()) }
        catch (error: LinkageError) { recordLinkage(error); unavailable() }
        catch (error: org.json.JSONException) { NativeResult(false, "invalid_native_result", "Malformed native JSON response") }
        catch (error: IllegalArgumentException) { NativeResult(false, "invalid_native_result", "Unsupported native response schema") }
        catch (error: RuntimeException) { runtimeFailure(error) }
    }
    private inline fun withHandle(handle: NativeHandle, block: () -> String): NativeResult =
        if (!handle.isValid) invalidHandle() else result(block)
    private inline fun command(handle: NativeHandle, block: () -> Unit): NativeResult {
        if (!handle.isValid) return invalidHandle()
        if (!isAvailable) return unavailable()
        return try { block(); NativeResult(true, "dispatched") }
        catch (error: LinkageError) { recordLinkage(error); unavailable() }
        catch (error: RuntimeException) { runtimeFailure(error) }
    }
    private inline fun createHandle(block: () -> Long): NativeHandle {
        if (!isAvailable) return NativeHandle()
        return try { NativeHandle(block()).let {
            if (it.isValid) it else { lastCallError = "native_creation_rejected"; NativeHandle() }
        } }
        catch (error: LinkageError) { recordLinkage(error); NativeHandle() }
        catch (error: RuntimeException) { runtimeFailure(error); NativeHandle() }
    }
}
