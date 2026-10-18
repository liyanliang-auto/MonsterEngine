package com.monsterengine.viewer

import android.content.Context
import android.os.Looper
import android.util.AtomicFile
import org.json.JSONObject
import java.io.File

/** Worker-only durable requested/effective settings; observations never replace requests. */
class ViewerSettingsStore(context: Context) {
    private val app = context.applicationContext
    private val file = AtomicFile(File(app.filesDir, "viewer-settings.json"))
    data class Saved(val requested: ViewerConfig, val effective: ViewerConfig? = null)

    fun load(): Saved {
        checkWorker()
        if (!file.baseFile.exists() && !File(file.baseFile.path + ".bak").exists()) {
            val text = app.assets.open("resources/android/default-viewer.json").bufferedReader().use { it.readText() }
            require(text.length <= MAX_BYTES)
            return Saved(validate(ViewerConfig.fromJson(JSONObject(text))))
        }
        return file.openRead().use { input ->
            val bytes=ByteArray(MAX_BYTES+1);var used=0
            while(used<bytes.size){val count=input.read(bytes,used,bytes.size-used);if(count<0)break;require(count>0);used+=count}
            require(used in 1..MAX_BYTES)
            val json = JSONObject(String(bytes,0,used,Charsets.UTF_8))
            require(json.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION)
            Saved(validate(ViewerConfig.fromJson(json.getJSONObject("requested"))),
                json.optJSONObject("effective")?.let { validate(ViewerConfig.fromJson(it)) })
        }
    }
    fun save(requested: ViewerConfig, effective: ViewerConfig? = null) {
        checkWorker(); validate(requested); effective?.let { validate(it) }
        val bytes = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION).put("requested", requested.toJson())
            .put("effective", effective?.toJson() ?: JSONObject.NULL).toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= MAX_BYTES)
        val output = file.startWrite()
        try { output.write(bytes); file.finishWrite(output) } catch (error: Exception) { file.failWrite(output); throw error }
    }
    companion object {
        private const val MAX_BYTES = 65536
        private fun checkWorker() { check(Looper.myLooper() != Looper.getMainLooper()) }
        fun validate(config: ViewerConfig): ViewerConfig {
            require(config.maximumInternalEdge in 128..4096 && config.maximumInternalPixels in 16384..16777216)
            val runtime = config.runtime
            require(runtime.optLong("activeCapacity") in 1..10000000 && runtime.optLong("sortCapacity") in 1..64000000)
            val quality = runtime.getJSONObject("lod").getDouble("qualityPixels")
            require(quality.isFinite() && quality in 0.1..100.0)
            val streaming = runtime.getJSONObject("streaming")
            for (key in listOf("sceneGPUBytes", "stagingBytes", "cpuCacheBytes", "temporaryGPUBytes", "maxUploadBytesPerFrame"))
                require(streaming.getLong(key) in 1..(4L shl 30))
            return ViewerConfig.fromJson(config.toJson()) // detach mutable JSON ownership
        }
    }
}
