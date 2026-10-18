package com.monsterengine.viewer

import org.json.JSONObject

/** JNI and persisted JSON protocol version. Unknown versions must be rejected. */
const val VIEWER_SCHEMA_VERSION = 1

/** Opaque registry token, never a pointer. Bits 0..31 are ID; bits 32..63 are generation.
 * Both components are nonzero. Native registries retire an ID before generation wraps,
 * validate token and object kind on every call, and invalidate before releasing ownership.
 * Negative Long values are valid bit patterns. Tokens must never survive process death.
 * @property value Exact jlong bit pattern; zero denotes creation failure/no ownership.
 */
data class NativeHandle(val value: Long = 0) {
    /** Unsigned registry slot, not an address. */
    val id: Long get() = value and 0xffffffffL
    /** Unsigned reuse generation, independent of scene and Surface generations. */
    val generation: Long get() = value ushr 32
    /** Structural validity only; native registry remains the lifetime authority. */
    val isValid: Boolean get() = id != 0L && generation != 0L

    companion object {
        /** Encode registry-assigned components without signed Long truncation. */
        fun fromParts(id: Long, generation: Long): NativeHandle {
            require(id in 1..0xffffffffL && generation in 1..0xffffffffL)
            return NativeHandle((generation shl 32) or id)
        }
    }
}

/** Versioned JNI result envelope. Successful execution must be explicitly observed.
 * @property ok Whether the requested operation succeeded.
 * @property code Stable machine-readable result code.
 * @property message Bounded diagnostic detail; exports must redact private paths.
 * @property payload Operation-specific data, separate from success status.
 */
data class NativeResult(val ok: Boolean = false, val code: String = "not_run",
                        val message: String = "", val payload: JSONObject = JSONObject()) {
    /** Serialize without converting unavailable observations into successful values. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("ok", ok).put("code", code).put("message", message).put("payload", payload)

    companion object {
        /** Reject malformed/unversioned responses rather than accepting implicit success. */
        fun fromJson(text: String): NativeResult {
            val json = JSONObject(text)
            require(json.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION) { "Unsupported native result schema" }
            return NativeResult(json.getBoolean("ok"), json.getString("code"),
                json.optString("message", ""), json.optJSONObject("payload") ?: JSONObject())
        }
    }
}

/** Exact EViewerSceneKind vocabulary. */
enum class ViewerSceneKind { PLY, LOD }

/** Published private scene identity; paths are internal and must not enter default reports.
 * @property sceneId Opaque stable library ID.
 * @property directory Validated lease-protected scene directory.
 * @property sourcePLY Validated source path for PLY scenes.
 * @property manifestPath Validated binary manifest path for LOD scenes.
 * @property contentHash Source/manifest SHA256 identity.
 * @property generation Positive monotonic scene identity.
 * @property kind Scene representation.
 */
data class SceneDescriptor(val sceneId: String, val directory: String,
    val sourcePLY: String = "", val manifestPath: String = "", val contentHash: String = "",
    val generation: Long = 1, val kind: ViewerSceneKind = ViewerSceneKind.PLY) {
    init { require(sceneId.isNotBlank() && directory.isNotBlank() && generation > 0) }
    /** Serialize fields matching FSceneDescriptor; validation is still required natively. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("sceneId", sceneId).put("directory", directory).put("sourcePLY", sourcePLY)
        .put("manifestPath", manifestPath).put("contentHash", contentHash)
        .put("generation", generation).put("kind", kind.name)

    companion object {
        /** Decode persisted descriptors only for this supported schema. */
        fun fromJson(json: JSONObject): SceneDescriptor {
            require(json.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION)
            return SceneDescriptor(json.getString("sceneId"), json.getString("directory"),
                json.optString("sourcePLY"), json.optString("manifestPath"), json.optString("contentHash"),
                json.getLong("generation"), ViewerSceneKind.valueOf(json.getString("kind")))
        }
    }
}

/** Settings envelope matching FViewerConfig; runtime contains existing shared runtime keys.
 * @property runtime Requested FSplatLODRuntimeOptions object, not effective settings.
 * @property maximumInternalEdge Hard internal longest-edge bound.
 * @property maximumInternalPixels Hard internal pixel-count bound.
 * @property generation Monotonic requested settings identity.
 * @property configHash Canonical configuration identity, assigned by admission.
 * @property shaderIdentity Packaged Shader inventory identity.
 */
data class ViewerConfig(val runtime: JSONObject = mobileRuntimeDefaults(),
    val maximumInternalEdge: Int = 1280, val maximumInternalPixels: Long = 921600,
    val generation: Long = 1, val configHash: String = "", val shaderIdentity: String = "") {
    init { require(maximumInternalEdge > 0 && maximumInternalPixels > 0 && generation > 0) }
    /** Serialize request; effective settings are returned independently by native snapshot. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("runtime", JSONObject(runtime.toString())).put("maximumInternalEdge", maximumInternalEdge)
        .put("maximumInternalPixels", maximumInternalPixels).put("generation", generation)
        .put("configHash", configHash).put("shaderIdentity", shaderIdentity)

    companion object {
        /** Initial mobile budgets match FViewerConfig; no measurements are implied. */
        fun mobileRuntimeDefaults(): JSONObject = JSONObject().put("activeCapacity", 100000)
            .put("sortCapacity", 2000000).put("projectedFusion", false).put("shStorageMode", 0)
            .put("rasterSharedMemoryMode", 0).put("radixSortBlocksPerWorkgroup", 32)
            .put("radixSortPhaseTiming", false)
            .put("streaming", JSONObject().put("sceneGPUBytes", 128L shl 20)
                .put("stagingBytes", 16L shl 20).put("cpuCacheBytes", 64L shl 20)
                .put("temporaryGPUBytes", 256L shl 20).put("maxUploadBytesPerFrame", 2L shl 20)
                .put("maxConcurrentReads", 2))
            .put("lod", JSONObject().put("qualityPixels", 3).put("hysteresis", 0.2)
                .put("activeBudget", 100000).put("sortBudget", 2000000))

        /** Decode a settings request while retaining all shared runtime settings. */
        fun fromJson(json: JSONObject): ViewerConfig {
            require(json.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION)
            return ViewerConfig(JSONObject(json.getJSONObject("runtime").toString()),
                json.getInt("maximumInternalEdge"), json.getLong("maximumInternalPixels"),
                json.getLong("generation"), json.optString("configHash"), json.optString("shaderIdentity"))
        }
    }
}

/** Pointer lifecycle spelling matches EViewerPointerPhase. */
enum class ViewerPointerPhase { Begin, Move, End, Cancel }

/** Owner-queued normalized gesture, with Surface identity to reject stale input.
 * @property phase Pointer lifecycle.
 * @property pointerId Stable Android pointer ID; -1 for synthesized gestures.
 * @property orbitX Horizontal normalized orbit delta.
 * @property orbitY Vertical normalized orbit delta.
 * @property panX Horizontal normalized pan delta.
 * @property panY Vertical normalized pan delta.
 * @property pinchScale Positive multiplicative zoom.
 * @property reset Reset camera/history.
 * @property surfaceGeneration Attached Surface identity.
 */
data class CameraInputState(val phase: ViewerPointerPhase = ViewerPointerPhase.Move,
    val pointerId: Int = -1, val orbitX: Float = 0f, val orbitY: Float = 0f,
    val panX: Float = 0f, val panY: Float = 0f, val pinchScale: Float = 1f,
    val reset: Boolean = false, val surfaceGeneration: Long = 0) {
    init { require(listOf(orbitX, orbitY, panX, panY, pinchScale).all { it.isFinite() })
        require(pinchScale > 0 && surfaceGeneration >= 0) }
    /** Serialize one bounded gesture command, never Gaussian arrays. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("phase", phase.name).put("pointerId", pointerId).put("orbitX", orbitX)
        .put("orbitY", orbitY).put("panX", panX).put("panY", panY).put("pinchScale", pinchScale)
        .put("reset", reset).put("surfaceGeneration", surfaceGeneration)
}

/** Stable checkpoint stage spelling and numeric order match ESplatBuildStage. */
enum class BuildStage { Created, Copying, Normalizing, Partitioning, BuildingLeaves, Reducing,
    Validating, Publishing, Completed, Paused, Cancelled, Failed, WaitingForResources }

/** Serialized native build progress. Zero totals mean unknown, never an invented ETA.
 * @property stage Current native computational/control stage.
 * @property processedBytes Actual current-stage bytes.
 * @property processedRecords Actual current-stage records.
 * @property totalBytes Current-stage denominator, zero if unknown.
 * @property totalRecords Current-stage denominator, zero if unknown.
 * @property workingBytes Admitted CPU workspace including metadata.
 * @property metadataBytes Metadata subset of workspace.
 * @property peakWorkingBytes Observed workspace high water.
 * @property diskBytes Actual retained job live set.
 * @property checkpointGeneration Last durable commit.
 * @property status Bounded stage/resource explanation.
 * @property error Terminal error detail.
 */
data class BuildProgress(val stage: BuildStage = BuildStage.Created,
    val processedBytes: Long = 0, val processedRecords: Long = 0, val totalBytes: Long = 0,
    val totalRecords: Long = 0, val workingBytes: Long = 0, val metadataBytes: Long = 0,
    val peakWorkingBytes: Long = 0, val diskBytes: Long = 0, val checkpointGeneration: Long = 0,
    val status: String = "", val error: String = "") {
    init { require(listOf(processedBytes, processedRecords, totalBytes, totalRecords, workingBytes,
        metadataBytes, peakWorkingBytes, diskBytes, checkpointGeneration).all { it >= 0 }) }
    /** Serialize a copied progress snapshot suitable for the persistent job store. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("stage", stage.name).put("processedBytes", processedBytes).put("processedRecords", processedRecords)
        .put("totalBytes", totalBytes).put("totalRecords", totalRecords).put("workingBytes", workingBytes)
        .put("metadataBytes", metadataBytes).put("peakWorkingBytes", peakWorkingBytes).put("diskBytes", diskBytes)
        .put("checkpointGeneration", checkpointGeneration).put("status", status).put("error", error)

    companion object {
        /** Read a versioned progress payload; malformed values remain explicit failures. */
        fun fromJson(json: JSONObject): BuildProgress {
            require(json.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION)
            return BuildProgress(BuildStage.valueOf(json.getString("stage")),
                json.getLong("processedBytes"), json.getLong("processedRecords"), json.getLong("totalBytes"),
                json.getLong("totalRecords"), json.getLong("workingBytes"), json.getLong("metadataBytes"),
                json.getLong("peakWorkingBytes"), json.getLong("diskBytes"), json.getLong("checkpointGeneration"),
                json.optString("status"), json.optString("error"))
        }
    }
}

/** CPU builder admission and algorithm policy matching FSplatBoundedBuildConfig.
 * @property workingMemoryBytes Total admitted workspace, including metadata and scratch.
 * @property metadataBytes Metadata subset of the workspace cap.
 * @property diskReserveBytes Required free disk headroom beyond the live set.
 * @property maxDiskBytes Maximum retained job live set.
 * @property maxSourceBytes Maximum stable source byte size.
 * @property maxSourceRecords Maximum source record count.
 * @property maxManifestBytes Maximum final manifest allocation.
 * @property leafCapacity Maximum records per leaf representation.
 * @property pageCapacity Maximum records per page.
 * @property maxProxyCount Maximum nonleaf representation size.
 * @property rootProxyCount Maximum root representation size.
 * @property batchCapacity Maximum parser and spool batch size.
 * @property maxDepth Finite partition depth limit.
 * @property maxNodes Maximum manifest node count.
 * @property maxPages Maximum manifest page count.
 * @property workerCount Combined-budget worker count, one or two.
 * @property pollIntervalRecords Maximum record operations between control polls.
 * @property maxHeaderBytes Maximum parser header storage.
 * @property maxRecordBytes Maximum parser record storage.
 * @property supportSigma Conservative Gaussian support multiplier.
 * @property proxyReduction Requested reduction ratio.
 * @property geometricErrorScale Conservative geometry error multiplier.
 * @property appearanceWeight Nonnegative appearance metric weight.
 * @property maxAppearanceError Nonnegative merge appearance tolerance.
 * @property builderVersion Stable independent mobile algorithm identity.
 * @property parserVersion Stable shared activation/parser identity.
 * @property builderHash Exact tool build identity, supplied before checkpoint admission.
 * @property schemaVersion Native intermediate journal schema identity.
 * @property retainCancelledWork Keep committed work for explicit recovery or cleanup.
 */
data class BoundedBuildConfig(
    val workingMemoryBytes: Long = 256L shl 20,
    val metadataBytes: Long = 32L shl 20,
    val diskReserveBytes: Long = 256L shl 20,
    val maxDiskBytes: Long = 32L shl 30,
    val maxSourceBytes: Long = 16L shl 30,
    val maxSourceRecords: Long = 100000000,
    val maxManifestBytes: Long = 64L shl 20,
    val leafCapacity: Int = 8192, val pageCapacity: Int = 8192,
    val maxProxyCount: Int = 8192, val rootProxyCount: Int = 8192,
    val batchCapacity: Int = 8192, val maxDepth: Int = 32,
    val maxNodes: Int = 1000000, val maxPages: Int = 1000000,
    val workerCount: Int = 1, val pollIntervalRecords: Int = 256,
    val maxHeaderBytes: Int = 1 shl 20, val maxRecordBytes: Int = 1 shl 20,
    val supportSigma: Float = 3f, val proxyReduction: Float = 0.25f,
    val geometricErrorScale: Float = 1f, val appearanceWeight: Float = 1f,
    val maxAppearanceError: Float = 0.1f,
    val builderVersion: String = "MonsterEngine-MobileLOD-1",
    val parserVersion: String = "MonsterEngine-PLY-Activated-1",
    val builderHash: String = "", val schemaVersion: Int = 1,
    val retainCancelledWork: Boolean = true
) {
    init {
        require(listOf(workingMemoryBytes, metadataBytes, maxDiskBytes, maxSourceBytes,
            maxSourceRecords, maxManifestBytes).all { it > 0 })
        require(diskReserveBytes >= 0 && metadataBytes <= workingMemoryBytes && maxManifestBytes <= workingMemoryBytes)
        require(listOf(leafCapacity, pageCapacity, maxProxyCount, rootProxyCount, batchCapacity,
            maxDepth, maxNodes, maxPages, pollIntervalRecords, maxHeaderBytes, maxRecordBytes).all { it > 0 })
        require(rootProxyCount <= maxProxyCount && workerCount in 1..2)
        require(listOf(supportSigma, proxyReduction, geometricErrorScale, appearanceWeight,
            maxAppearanceError).all { it.isFinite() })
        require(supportSigma >= 3 && proxyReduction > 0 && proxyReduction <= 1 && geometricErrorScale >= 1)
        require(appearanceWeight >= 0 && maxAppearanceError >= 0 && schemaVersion == 1)
        require(builderVersion == "MonsterEngine-MobileLOD-1" && parserVersion == "MonsterEngine-PLY-Activated-1")
        require(builderHash.isEmpty() || builderHash.matches(Regex("[0-9a-f]{64}")))
    }

    /** Serialize admission policy; native code performs checked allocation/disk admission. */
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", schemaVersion)
        .put("workingMemoryBytes", workingMemoryBytes).put("metadataBytes", metadataBytes)
        .put("diskReserveBytes", diskReserveBytes).put("maxDiskBytes", maxDiskBytes)
        .put("maxSourceBytes", maxSourceBytes).put("maxSourceRecords", maxSourceRecords)
        .put("maxManifestBytes", maxManifestBytes).put("leafCapacity", leafCapacity)
        .put("pageCapacity", pageCapacity).put("maxProxyCount", maxProxyCount)
        .put("rootProxyCount", rootProxyCount).put("batchCapacity", batchCapacity)
        .put("maxDepth", maxDepth).put("maxNodes", maxNodes).put("maxPages", maxPages)
        .put("workerCount", workerCount).put("pollIntervalRecords", pollIntervalRecords)
        .put("maxHeaderBytes", maxHeaderBytes).put("maxRecordBytes", maxRecordBytes)
        .put("supportSigma", supportSigma).put("proxyReduction", proxyReduction)
        .put("geometricErrorScale", geometricErrorScale).put("appearanceWeight", appearanceWeight)
        .put("maxAppearanceError", maxAppearanceError).put("builderVersion", builderVersion)
        .put("parserVersion", parserVersion).put("builderHash", builderHash)
        .put("retainCancelledWork", retainCancelledWork)

    companion object {
        /** Restore all persisted policy fields; absent or incompatible fields fail explicitly. */
        fun fromJson(json: JSONObject): BoundedBuildConfig = BoundedBuildConfig(
            json.getLong("workingMemoryBytes"), json.getLong("metadataBytes"),
            json.getLong("diskReserveBytes"), json.getLong("maxDiskBytes"),
            json.getLong("maxSourceBytes"), json.getLong("maxSourceRecords"), json.getLong("maxManifestBytes"),
            json.getInt("leafCapacity"), json.getInt("pageCapacity"), json.getInt("maxProxyCount"),
            json.getInt("rootProxyCount"), json.getInt("batchCapacity"), json.getInt("maxDepth"),
            json.getInt("maxNodes"), json.getInt("maxPages"), json.getInt("workerCount"),
            json.getInt("pollIntervalRecords"), json.getInt("maxHeaderBytes"), json.getInt("maxRecordBytes"),
            json.getDouble("supportSigma").toFloat(), json.getDouble("proxyReduction").toFloat(),
            json.getDouble("geometricErrorScale").toFloat(), json.getDouble("appearanceWeight").toFloat(),
            json.getDouble("maxAppearanceError").toFloat(), json.getString("builderVersion"),
            json.getString("parserVersion"), json.getString("builderHash"), json.getInt("schemaVersion"),
            json.getBoolean("retainCancelledWork"))
    }
}
