package com.monsterengine.viewer

import android.content.Context
import android.content.ContextWrapper
import android.graphics.Bitmap
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile

/** Exercise the production private bundle writer, not a duplicate sanitizer. */
object PresentationDiagnosticsTests {
    private val numeric = listOf("presentationFormat", "presentationColorSpace", "presentationWidth",
        "presentationHeight", "displayWidth", "displayHeight", "presentationDisplayScale", "presentationSharpeningStrength")
    private val memory = listOf("vulkanManagedLiveBytes", "vulkanManagedBackingBytes", "vulkanPooledUnusedBytes")

    private fun isolated(context: Context, body: (DiagnosticsExporter, File) -> Unit) {
        check(Looper.myLooper() != Looper.getMainLooper())
        val root = File(context.cacheDir, "presentation-export-${UUID.randomUUID()}")
        val files = File(root, "files"); val cache = File(root, "cache")
        check(files.mkdirs() && cache.mkdirs())
        val wrapper = object : ContextWrapper(context) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = files
            override fun getCacheDir(): File = cache
        }
        try { body(DiagnosticsExporter(wrapper), root) }
        finally { check(root.deleteRecursively()) }
    }

    private fun entries(bundle: File): Map<String, ByteArray> = ZipFile(bundle).use { zip ->
        zip.entries().asSequence().associate { entry -> entry.name to zip.getInputStream(entry).use { it.readBytes() } }
    }

    private fun viewer(exporter: DiagnosticsExporter, snapshot: JSONObject): JSONObject {
        val bytes = entries(exporter.createBundle(nativeSnapshot = snapshot)).getValue("report.json")
        return JSONObject(String(bytes, Charsets.UTF_8)).getJSONObject("viewer")
    }

    fun run(context: Context, test: (String, () -> Unit) -> Unit) {
        test("presentation_export_preserves_typed_format_scale_and_srgb") { isolated(context) { exporter, _ ->
            val observation = JSONObject().put("presentationFormat", 37).put("presentationColorSpace", 0)
                .put("presentationFormatSRGB", false).put("presentationWidth", 1920).put("presentationHeight", 815)
                .put("displayWidth", 2536).put("displayHeight", 1077).put("presentationDisplayScale", 2536.0 / 1920)
                .put("presentationSharpeningStrength", 0.141).put("privatePath", "/private/model.ply")
                .put("detail", "/private/keep-secret").put("unknownCounter", 123)
            val result = viewer(exporter, JSONObject().put("latest", observation)).getJSONObject("latest")
            numeric.forEach { key -> check(result.get(key) is Number) { "$key changed type" } }
            check(result.getInt("presentationFormat") == 37 && result.getInt("presentationColorSpace") == 0)
            check(result.get("presentationFormatSRGB") is Boolean && !result.getBoolean("presentationFormatSRGB"))
            check(result.getDouble("presentationDisplayScale") == 2536.0 / 1920)
            check(result.getDouble("presentationSharpeningStrength") == 0.141)
            check(!result.has("privatePath") && !result.has("unknownCounter"))
            check(result.getString("detail") == "omitted_for_privacy")
            check(!result.toString().contains("keep-secret"))
        } }
        test("presentation_export_omits_wrong_scalar_types_and_missing_values") { isolated(context) { exporter, _ ->
            for (wrong in listOf<Any>("37", true, JSONObject().put("width", 37), JSONArray().put(37), JSONObject.NULL)) {
                val source = JSONObject(); numeric.forEach { source.put(it, wrong) }
                val result = viewer(exporter, source)
                numeric.forEach { check(!result.has(it)) { "Accepted wrong type for $it" } }
            }
            for (wrong in listOf<Any>("true", 1, JSONObject(), JSONArray(), JSONObject.NULL)) {
                check(!viewer(exporter, JSONObject().put("presentationFormatSRGB", wrong)).has("presentationFormatSRGB"))
            }
            check(viewer(exporter, JSONObject()).length() == 0)
        } }
        test("presentation_export_memory_numeric_null_and_type_contract") { isolated(context) { exporter, _ ->
            val source = JSONObject().put(memory[0], 65536L).put(memory[1], Long.MAX_VALUE).put(memory[2], 0L)
            val result = viewer(exporter, source)
            check(result.getLong(memory[0]) == 65536L && result.getLong(memory[1]) == Long.MAX_VALUE)
            check(result.getLong(memory[2]) == 0L)
            val unavailable = JSONObject(); memory.forEach { unavailable.put(it, JSONObject.NULL) }
            val nulls = viewer(exporter, unavailable)
            memory.forEach { check(nulls.has(it) && nulls.isNull(it)) }
            for (wrong in listOf<Any>("/private/memory", "123", true, JSONObject(), JSONArray())) {
                val bad = JSONObject(); memory.forEach { bad.put(it, wrong) }
                val rejected = viewer(exporter, bad); memory.forEach { check(!rejected.has(it)) }
            }
        } }
        test("presentation_export_rejects_nonfinite_observations") { isolated(context) { exporter, _ ->
            for (nonfinite in listOf(Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY)) {
                // JSON text cannot encode these values. An in-memory JSONObject
                // boundary may still return them; exercise that boundary directly.
                val source = object : JSONObject() {
                    override fun opt(name: String): Any? = if (name in numeric || name in memory) nonfinite else super.opt(name)
                }
                (numeric + memory).forEach { source.put(it, 0) }
                val result = viewer(exporter, source)
                (numeric + memory).forEach { check(!result.has(it)) { "Accepted nonfinite $it" } }
            }
        } }
        test("presentation_export_screenshot_encoding_png_and_manifest_hashes") { isolated(context) { exporter, root ->
            val screenshot = File(root, "cache/fixture.png")
            val bitmap = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888)
            try {
                bitmap.eraseColor(0xff804020.toInt())
                screenshot.outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            } finally { bitmap.recycle() }
            val original = screenshot.readBytes()
            val metadata = JSONObject().put("gpuCompleted", true).put("origin", "internal_offscreen_output")
                .put("frameId", 7).put("submissionToken", 9).put("width", 2).put("height", 2)
                .put("format", "RGBA8_UNORM").put("colorSpace", "srgb_encoded_rgb").put("rowOrigin", "top_left")
                .put("privatePath", screenshot.path).put("detail", "/private/screenshot-secret")
            val archive = entries(exporter.createBundle(screenshot = screenshot, screenshotMetadata = metadata))
            check(archive.getValue("screenshot.png").contentEquals(original))
            check(screenshot.readBytes().contentEquals(original))
            val clean = JSONObject(String(archive.getValue("screenshot.json"), Charsets.UTF_8))
            check(clean.getString("colorSpace") == "srgb_encoded_rgb")
            check(!clean.has("privatePath") && clean.getString("detail") == "omitted_for_privacy")
            val manifest = JSONObject(String(archive.getValue("manifest.json"), Charsets.UTF_8)).getJSONArray("artifacts")
            check(manifest.length() == archive.size - 1)
            val inventoried = mutableSetOf<String>()
            for (index in 0 until manifest.length()) {
                val item = manifest.getJSONObject(index); val name = item.getString("name")
                check(inventoried.add(name))
                val bytes = archive.getValue(name)
                check(item.getLong("bytes") == bytes.size.toLong())
                val hash = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it.toInt() and 255) }
                check(item.getString("sha256") == hash)
            }
            check(inventoried == archive.keys - "manifest.json")
        } }
    }
}
