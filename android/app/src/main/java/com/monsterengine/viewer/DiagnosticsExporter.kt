package com.monsterengine.viewer

import android.app.Activity
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Looper
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Private diagnostic archives. Invoke createBundle on an application worker;
 * nothing uploads or grants a URI until the explicit share action is invoked.
 * No scene source, user filenames, private paths or arbitrary error text is copied.
 */
class DiagnosticsExporter(context: Context) {
    private val app = context.applicationContext
    private val exportRoot get() = File(app.filesDir, "exports")

    /** Native report and CSV must come from the same immutable snapshot request.
     * A screenshot is included only with native metadata proving completed internal
     * readback. A missing native library still produces a useful partial archive.
     * Errors propagate to the worker caller; incomplete archives are removed.
     */
    fun createBundle(nativeSnapshot: JSONObject? = null, framesCSV: String? = null,
                     buildReports: List<JSONObject> = emptyList(), screenshot: File? = null,
                     screenshotMetadata: JSONObject? = null): File = NativeBridge.profileScope("ME/kotlin.export_bundle") {
        check(Looper.myLooper() != Looper.getMainLooper()) { "Diagnostic export requires a worker" }
        require(buildReports.size <= 32) { "Too many build reports" }
        require(nativeSnapshot == null || nativeSnapshot.toString().length <= 16 * 1024 * 1024)
        if (!exportRoot.isDirectory && !exportRoot.mkdirs()) throw IOException("Cannot create private export directory")
        val target = File.createTempFile("diagnostics-", ".zip", exportRoot)
        try {
            val artifacts = linkedMapOf<String, ByteArray>()
            val report = JSONObject().put("schemaVersion", 1).put("kind", "diagnostic_bundle")
                .put("privacy", "paths_and_source_omitted").put("native", safeObject(NativeBridge.partialDiagnostics()))
                .put("nativeReportScope", "library_load_only")
                .put("nativeLoadFailure", when {
                    NativeBridge.loadError != null -> "captured_native_load_failure_details_omitted"
                    NativeBridge.isAvailable -> JSONObject.NULL
                    else -> "native_unavailable"
                })
                .put("viewer", nativeSnapshot?.let { safeObject(it) } ?: JSONObject.NULL)
                .put("viewerUnavailableReason", if (nativeSnapshot == null) "not_observed" else JSONObject.NULL)
            val builds = JSONArray()
            buildReports.forEach { builds.put(safeObject(it)) }
            report.put("buildReports", builds)
            artifacts["report.json"] = report.toString(2).toByteArray(Charsets.UTF_8)
            if (framesCSV != null) artifacts["frames.csv"] = safeCSV(framesCSV).toByteArray(Charsets.UTF_8)
            nativeSnapshot?.optString("profilingRawJSON")?.takeIf { it.isNotEmpty() }?.let { raw ->
                require(raw.length <= 12 * 1024 * 1024)
                val profile = JSONObject(raw)
                require(profile.optInt("schemaVersion") == 1)
                validateProfiling(profile)
                // Preserve original uint64 text: JSONObject round-trips values above Long.MAX_VALUE as Double.
                artifacts["profiling.json"] = raw.toByteArray(Charsets.UTF_8)
            }
            if (screenshot != null && screenshotMetadata != null) {
                require(screenshotMetadata.optBoolean("gpuCompleted", false)) { "Readback has not completed" }
                require(screenshotMetadata.optString("origin") == "internal_offscreen_output") { "Wrong screenshot origin" }
                require(screenshotMetadata.optLong("frameId") > 0 && screenshotMetadata.optLong("submissionToken") > 0)
                val privateFile = screenshot.canonicalFile
                val allowed = listOf(app.filesDir.canonicalFile, app.cacheDir.canonicalFile)
                    .any { privateFile.toPath().startsWith(it.toPath()) }
                require(allowed && privateFile.isFile && privateFile.length() in 1..MAX_SCREENSHOT_BYTES)
                val bytes = privateFile.inputStream().use { input ->
                    val output = java.io.ByteArrayOutputStream()
                    val chunk = ByteArray(65536)
                    while (true) {
                        val count = input.read(chunk)
                        if (count < 0) break
                        require(output.size().toLong() + count <= MAX_SCREENSHOT_BYTES)
                        output.write(chunk, 0, count)
                    }
                    output.toByteArray()
                }
                require(bytes.size <= MAX_SCREENSHOT_BYTES && bytes.take(8) == PNG_SIGNATURE.toList())
                val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)
                require(options.outWidth == screenshotMetadata.optInt("width") && options.outHeight == screenshotMetadata.optInt("height"))
                require(options.outWidth in 1..8192 && options.outHeight in 1..8192)
                artifacts["screenshot.png"] = bytes
                artifacts["screenshot.json"] = safeObject(screenshotMetadata).toString(2).toByteArray(Charsets.UTF_8)
            }
            val inventory = JSONArray()
            var totalBytes = 0L
            artifacts.forEach { (name, bytes) ->
                totalBytes += bytes.size
                require(totalBytes <= MAX_BUNDLE_BYTES) { "Diagnostic bundle exceeds limit" }
                inventory.put(JSONObject().put("name", name).put("bytes", bytes.size).put("sha256", sha256(bytes)))
            }
            val manifest = JSONObject().put("schemaVersion", 1).put("kind", "diagnostic_bundle")
                .put("privacy", "paths_and_source_omitted").put("artifacts", inventory)
            artifacts["manifest.json"] = manifest.toString(2).toByteArray(Charsets.UTF_8)
            ZipOutputStream(target.outputStream().buffered()).use { zip ->
                artifacts.forEach { (name, bytes) ->
                    zip.putNextEntry(ZipEntry(name).apply { time = 0 })
                    zip.write(bytes)
                    zip.closeEntry()
                }
            }
            target
        } catch (error: Exception) {
            if (!target.delete() && target.exists()) target.deleteOnExit()
            throw error
        }
    }

    /** Call only from the user's Share button. The chooser grants one content URI,
     * and accepts only completed .zip files in this exporter's private directory.
     */
    fun shareAfterUserAction(activity: Activity, bundle: File) {
        val file = bundle.canonicalFile
        require(file.parentFile == exportRoot.canonicalFile && file.isFile && file.extension == "zip")
        val uri = FileProvider.getUriForFile(app, "${app.packageName}.files", file)
        val intent = Intent(Intent.ACTION_SEND).setType("application/zip")
            .putExtra(Intent.EXTRA_STREAM, uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            .apply { clipData = ClipData.newUri(app.contentResolver, "Diagnostics", uri) }
        activity.startActivity(Intent.createChooser(intent, "Share diagnostics"))
    }

    private fun safeCSV(text: String): String {
        require(text.length <= 2 * 1024 * 1024)
        val lines = text.lineSequence().filter { it.isNotEmpty() }.toList()
        require(lines.isNotEmpty() && lines.size <= 513 && lines.first() == CSV_HEADER) { "Unexpected native CSV schema" }
        lines.drop(1).forEach { line ->
            val cells = line.split(',')
            require(cells.size == CSV_COLUMN_COUNT && cells.all { it.isEmpty() ||
                (it.length <= 32 && it.matches(Regex("[0-9.+eE-]+")) && it.toDoubleOrNull()?.isFinite() == true) })
            require(cells[21] == "2") { "Unsupported measurement schema" }
        }
        return lines.joinToString("\n", postfix = "\n")
    }

    /** Allowlisted scalar-only strings and bounded nested numeric observations.
     * Unknown fields are dropped, so future raw paths cannot silently leak.
     */
    private fun safeObject(source: JSONObject, depth: Int = 0, measurementObservation: Boolean = false): JSONObject {
        require(depth <= 12 && source.length() <= 128)
        val output = JSONObject()
        source.keys().forEach { key ->
            if (key !in SAFE_KEYS) return@forEach
            val value = source.opt(key)
            // Shared dimensions retain the presentation omission contract outside
            // the versioned measurement object, whose observations are strict.
            if (key in MEASUREMENT_NUMBER_KEYS && (measurementObservation || key !in PRESENTATION_NUMBER_KEYS)) {
                require(value == JSONObject.NULL || value is Number && value.toDouble().isFinite()) {
                    "Mistyped measurement observation"
                }
                if (key == "measurementSchemaVersion") require(value is Number && value.toDouble() == 2.0) {
                    "Unsupported measurement schema"
                }
                output.put(key, value)
                return@forEach
            }
            // Presentation observations have a scalar type contract. Do not let
            // the generic nested/string handling admit a mistyped observation.
            if (key in PRESENTATION_NUMBER_KEYS) {
                if (value is Number && value.toDouble().isFinite()) output.put(key, value)
                return@forEach
            }
            if (key in PRESENTATION_BOOLEAN_KEYS) {
                if (value is Boolean) output.put(key, value)
                return@forEach
            }
            if (key in MEMORY_NUMBER_KEYS) {
                if (value == JSONObject.NULL) output.put(key, JSONObject.NULL)
                else if (value is Number && value.toDouble().isFinite()) output.put(key, value)
                return@forEach
            }
            when (value) {
                null, JSONObject.NULL -> output.put(key, JSONObject.NULL)
                is Boolean -> output.put(key, value)
                is Number -> if (value.toDouble().isFinite()) output.put(key, value) else output.put(key, JSONObject.NULL)
                is JSONObject -> output.put(key, safeObject(value, depth + 1, key == "measurement"))
                is JSONArray -> {
                    require(value.length() <= 512)
                    val items = JSONArray()
                    for (index in 0 until value.length()) {
                        when (val item = value.opt(index)) {
                            is JSONObject -> items.put(safeObject(item, depth + 1))
                            is Number -> if (item.toDouble().isFinite()) items.put(item)
                            is String -> if (key == "supportedAbis" && item in setOf("arm64-v8a", "x86_64", "armeabi-v7a", "x86")) items.put(item)
                        }
                    }
                    output.put(key, items)
                }
                is String -> when {
                    key in HASH_KEYS -> output.put(key, if (value.matches(Regex("[0-9a-f]{40}|[0-9a-f]{64}"))) value else JSONObject.NULL)
                    key in CODE_KEYS && value.length <= 96 && value.matches(Regex("[a-zA-Z0-9_]+")) -> output.put(key, value)
                    value in SAFE_TEXT -> output.put(key, value)
                    else -> output.put(key, "omitted_for_privacy")
                }
            }
        }
        return output
    }

    /** Validate a path-free backend schema while retaining its exact numeric source text. */
    private fun validateProfiling(value: Any, depth: Int = 0) {
        require(depth <= 4)
        when (value) {
            is JSONObject -> {
                require(value.length() <= 32)
                value.keys().forEach { key ->
                    require(key in PROFILING_KEYS)
                    validateProfiling(value.get(key), depth + 1)
                }
            }
            is JSONArray -> {
                require(value.length() <= 16384)
                for (index in 0 until value.length()) validateProfiling(value.get(index), depth + 1)
            }
            is String -> require(value.length <= 96 && value.matches(Regex("[a-zA-Z0-9_.:/ -]+")))
            is Number -> require(value.toDouble().isFinite())
            is Boolean -> Unit
            else -> require(value == JSONObject.NULL)
        }
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it.toInt() and 255) }

    companion object {
        private val PROFILING_KEYS = setOf("schemaVersion", "clockDomain", "enabled", "captureEpoch", "ringCapacity",
            "droppedEvents", "overwrittenEvents", "recordCalls", "recordDurationNs", "exportCallsBeforeThis",
            "exportDurationNsBeforeThis", "exportDurationNsThisSnapshot", "enabledBackends", "tracyIntegration", "events",
            "eventSequence", "timestampNs", "threadId", "name", "phase", "requestId", "frameId", "submissionId",
            "sceneId", "pageId", "value")
        private const val MAX_SCREENSHOT_BYTES = 20L * 1024 * 1024
        private const val MAX_BUNDLE_BYTES = 40L * 1024 * 1024
        private val PNG_SIGNATURE = byteArrayOf(137.toByte(), 80, 78, 71, 13, 10, 26, 10)
        private const val CSV_COLUMN_COUNT = 73
        private const val CSV_HEADER = "frameId,sceneGeneration,surfaceGeneration,configGeneration,submissionToken,activeGaussians,sortEntries,sortCountFrame,residentBytes,uploadedBytes,cpuLeaseBytes,cpuFrameMilliseconds,gpuMilliseconds,gpuTimingFrame,presentReturnCadenceMilliseconds,degraded,streamBytesRead,streamPendingReads,streamActiveReads,streamUploadedPages,streamEvictedPages,measurementSchemaVersion,pipelineGeneration,ownerStartNanoseconds,presentReturnNanoseconds,sessionMilliseconds,acquireMilliseconds,presentMilliseconds,ownerWorkMilliseconds,sortStatus,sortOverflow,gpuPreprocessMilliseconds,gpuPreprocessStatus,gpuScanMilliseconds,gpuScanStatus,gpuAssignKeysMilliseconds,gpuAssignKeysStatus,gpuRadixSortMilliseconds,gpuRadixSortStatus,gpuTileBoundsMilliseconds,gpuTileBoundsStatus,gpuRenderMilliseconds,gpuRenderStatus,gpuPipelineMilliseconds,gpuPipelineStatus,cameraX,cameraY,cameraZ,cameraYaw,cameraPitch,cameraDistance,externalCamera,renderWidth,renderHeight,presentationWidth,presentationHeight,displayWidth,displayHeight,lodSelectionMilliseconds,uploadRecordMilliseconds,desiredNodes,selectedNodes,requestPages,prefetchPages,maxSelectedProxyErrorPixels,selectedLeafNodes,selectedProxyNodes,streamPendingUploads,streamReadyBytes,streamDeferredPages,streamLoading,streamRootReady,streamRequests"
        private val MEASUREMENT_NUMBER_KEYS = setOf("measurementSchemaVersion", "pipelineGeneration", "ownerStartNanoseconds", "presentReturnNanoseconds", "sessionMilliseconds", "acquireMilliseconds", "presentMilliseconds", "ownerWorkMilliseconds", "sortStatus", "sortOverflow", "gpuPreprocessMilliseconds", "gpuPreprocessStatus", "gpuScanMilliseconds", "gpuScanStatus", "gpuAssignKeysMilliseconds", "gpuAssignKeysStatus", "gpuRadixSortMilliseconds", "gpuRadixSortStatus", "gpuTileBoundsMilliseconds", "gpuTileBoundsStatus", "gpuRenderMilliseconds", "gpuRenderStatus", "gpuPipelineMilliseconds", "gpuPipelineStatus", "cameraX", "cameraY", "cameraZ", "cameraYaw", "cameraPitch", "cameraDistance", "externalCamera", "renderWidth", "renderHeight", "presentationWidth", "presentationHeight", "displayWidth", "displayHeight", "lodSelectionMilliseconds", "uploadRecordMilliseconds", "desiredNodes", "selectedNodes", "requestPages", "prefetchPages", "maxSelectedProxyErrorPixels", "selectedLeafNodes", "selectedProxyNodes", "streamPendingUploads", "streamReadyBytes", "streamDeferredPages", "streamLoading", "streamRootReady", "streamRequests")
        private val HASH_KEYS = setOf("contentHash", "buildIdentity", "configHash", "shaderIdentity", "sourceSHA256", "builderHash")
        private val CODE_KEYS = setOf("code", "name", "reason", "degradationReason", "stage", "state", "kind", "unavailableReason")
        private val PRESENTATION_NUMBER_KEYS = setOf("presentationFormat", "presentationColorSpace",
            "presentationWidth", "presentationHeight", "displayWidth", "displayHeight",
            "presentationDisplayScale", "presentationSharpeningStrength")
        private val PRESENTATION_BOOLEAN_KEYS = setOf("presentationFormatSRGB")
        private val MEMORY_NUMBER_KEYS = setOf("vulkanManagedLiveBytes", "vulkanManagedBackingBytes", "vulkanPooledUnusedBytes")
        private val SAFE_TEXT = setOf("viewer_diagnostics", "build_report", "diagnosticsOnly", "paths_and_source_omitted",
            "not_observed", "not_measured", "not_queried", "not_observed_or_identity_not_supplied", "omitted_for_privacy",
            "owner_cpu_interval_not_gpu", "completed_interval_original_gpuTimingFrame", "internal_offscreen_output",
            "linear_shader_output_before_srgb_present", "srgb_encoded_rgb", "RGBA8_UNORM", "MonsterEngine-MobileLOD-1", "MonsterEngine-PLY-Activated-1",
            "pending_until_nonblocking_resolve_or_ring_overwrite",
            "A", "B", "C", "D", "E", "split", "scalar", "pending", "top_left")
        private val SAFE_KEYS = HASH_KEYS + CODE_KEYS + MEASUREMENT_NUMBER_KEYS + PRESENTATION_NUMBER_KEYS + PRESENTATION_BOOLEAN_KEYS + MEMORY_NUMBER_KEYS + setOf("measurement", "unresolvedOverwrittenFrames", "unmatchedGPURecords", "schemaVersion", "privacy", "latest", "frames", "capacity", "droppedFrames",
            "frameId", "sceneGeneration", "surfaceGeneration", "configGeneration", "requestedConfig", "effectiveConfig", "generation",
            "maximumInternalEdge", "maximumInternalPixels", "width", "height", "renderWidth", "renderHeight", "mode", "vsync",
            "activeCapacity", "sortCapacity", "sceneGPUBytes", "stagingBytes", "cpuCacheBytes", "temporaryGPUBytes", "maxUploadBytesPerFrame",
            "maxConcurrentReads", "retries", "retryDelayMilliseconds", "qualityPixels", "hysteresis", "activeBudget", "sortBudget", "sourceBudget",
            "freeze", "tightTileCulling", "shStorageMode", "shStorageDegree", "displaySHDegree", "rasterSharedMemoryMode",
            "radixSortBlocksPerWorkgroup", "radixSortPhaseTiming", "projectedFusion", "projectionColorCompression", "fusionImplementation",
            "fusionRelativeRGBAError", "submissionToken", "degraded", "status", "ok", "detail", "counters", "activeGaussians", "sortEntries",
            "sortCountFrame", "residentBytes", "uploadedBytes", "cpuLeaseBytes", "cpuFrameMilliseconds", "gpuMilliseconds", "gpuTimingFrame",
            "timing", "presentReturnCadenceMilliseconds", "displayCadenceMilliseconds", "videoEncodingFPS", "cpuMeaning", "gpuMeaning",
            "nestedIntervalsAdditive", "budget", "limitBytes", "reservedBytes", "peakBytes", "exclusiveBuild", "components", "capabilities",
            "supported", "enabled", "executed", "streaming", "submittedFrameId", "activeCount", "selectedLeafNodes", "selectedProxyNodes",
            "selectionVersion", "residencyVersion", "uploadedPages", "evictedPages", "pendingUploads", "rejectedUploads", "sourceBackingBytes",
            "stagingBackingBytes", "cpuReservedBytes", "cpuReadyBytes", "bytesRead", "requests", "pendingReads", "activeReads", "failedReads",
            "terminalPageFailures", "deferredPages", "rootReady", "loading", "builderVersion", "parserVersion", "buildSchemaVersion", "sourceBytes",
            "sourceRecords", "processedBytes", "processedRecords", "totalBytes", "totalRecords", "workingBytes", "metadataBytes", "peakWorkingBytes",
            "diskBytes", "checkpointGeneration", "hasError", "nativeLibraryAvailable", "nativeLoadErrorAvailable", "sdkInt", "supportedAbis",
            "surfaceProbe", "available", "memoryBudget", "gpuCompleted", "format", "colorSpace", "origin", "rowOrigin",
            "diagnostics", "stages", "devices", "graphicsSmoke", "splatSmoke", "graphicsSmokePassed", "splatSmokePassed", "streamingPassed",
            "diagnosticsOnly", "renderEligible", "selectedByEngine", "enginePhysicalDeviceIndex", "vkResult", "submissionCompleted", "readbackValidated",
            "physicalDeviceIndex", "vendorID", "deviceID", "driverVersion", "apiVersion", "id", "required", "requested", "surface",
            "supportedUsageFlags", "minImageCount", "maxImageCount", "formats", "presentModes", "resumed", "windowInUse",
            "manualRetryRequired", "pressureRetired", "presentationReservationEstimate", "driverPresentationBackingMeasured")
    }
}
