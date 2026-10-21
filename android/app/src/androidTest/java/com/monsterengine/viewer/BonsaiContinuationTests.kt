package com.monsterengine.viewer

import android.content.Context
import android.content.ContextWrapper
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import java.io.File
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject

/** Deterministic production sampler and isolated durable settings contracts. */
object BonsaiContinuationTests {
    fun run(context: Context, test: (String, () -> Unit) -> Unit) {
        fun sample(frame: Long = 1, token: Long = frame, scene: Long = 1, surface: Long = 1,
            ms: Double = 20.0) = ObservedFpsSample(frame, token, scene, surface, ms)
        test("fps_first_then_progressing_frame") {
            val s = ObservedFpsSampler(); check(s.observe(sample()) == null)
            check(s.observe(sample(4)) == 50.0); check(s.observe(sample(8, ms = 40.0)) == 25.0)
        }
        test("fps_repeated_and_decreasing_frames_clear_continuity") {
            for (next in listOf(sample(2), sample(1), sample(3, token = 2))) {
                val s = ObservedFpsSampler(); s.observe(sample(2))
                check(s.observe(next) == null); check(s.observe(sample(4)) == null)
                check(s.observe(sample(5)) == 50.0)
            }
        }
        test("fps_scene_and_surface_generation_change_require_new_pair") {
            for (next in listOf(sample(2, scene = 2), sample(2, surface = 2))) {
                val s = ObservedFpsSampler(); s.observe(sample())
                check(s.observe(next) == null)
                check(s.observe(next.copy(frameId = 3, submissionToken = 3)) == 50.0)
            }
        }
        test("fps_null_invalid_identity_and_cadence_reset") {
            val invalid = listOf(null, sample(0), sample(token = 0), sample(scene = 0),
                sample(surface = 0), sample(ms = 0.0), sample(ms = -1.0),
                sample(ms = Double.NaN), sample(ms = Double.POSITIVE_INFINITY))
            for (next in invalid) {
                val s = ObservedFpsSampler(); s.observe(sample()); check(s.observe(sample(2)) == 50.0)
                check(s.observe(next) == null); check(s.observe(sample(3)) == null)
                check(s.observe(sample(4)) == 50.0)
            }
        }
        test("fps_explicit_lifecycle_reset_and_overflow_never_show_value") {
            val s = ObservedFpsSampler(); s.observe(sample()); s.reset()
            check(s.observe(sample(2)) == null)
            check(s.observe(sample(3, ms = Double.MIN_VALUE)) == null)
            s.reset(); check(s.observe(sample(4)) == null); check(s.observe(sample(5)) == 50.0)
        }
        fun isolated(body: (ViewerSettingsStore, File) -> Unit) {
            val root = File(context.cacheDir, "bonsai-settings-${UUID.randomUUID()}")
            check(root.mkdirs())
            val wrapper = object : ContextWrapper(context) {
                override fun getApplicationContext(): Context = this
                override fun getFilesDir(): File = root
            }
            try { body(ViewerSettingsStore(wrapper), File(root, "viewer-settings.json")) }
            finally { root.deleteRecursively() }
        }
        test("settings_fresh_defaults_then_saved_selection_survives_refresh") { isolated { store, _ ->
            val fresh = store.load(); check(fresh.selectedSceneId.isEmpty())
            store.save(fresh.requested, selectedSceneId = "retained-private-scene")
            store.save(fresh.requested.copy(generation = 2), fresh.requested)
            val saved = store.load(); check(saved.selectedSceneId == "retained-private-scene")
            check(saved.requested.generation == 2L && saved.effective?.generation == 1L)
        } }
        test("settings_unknown_schema_and_malformed_original_preserved") { isolated { store, file ->
            for (original in listOf("{\"schemaVersion\":999,\"opaque\":\"retain\"}", "{broken", "{}")) {
                file.writeText(original)
                check(runCatching { store.load() }.isFailure)
                check(runCatching { store.save(ViewerConfig(), selectedSceneId = "replacement") }.isFailure)
                check(file.readText() == original)
            }
        } }
        test("settings_invalid_request_does_not_overwrite_saved_selection") { isolated { store, file ->
            store.save(ViewerConfig(), selectedSceneId = "retained")
            val before = file.readBytes()
            check(runCatching { store.save(ViewerConfig(maximumInternalEdge = 1)) }.isFailure)
            check(file.readBytes().contentEquals(before)); check(store.load().selectedSceneId == "retained")
        } }
        test("quality_preset_is_lossless_bounded_and_detached_from_original") {
            val base = ViewerConfig(configHash = "previous", generation = 7)
            base.runtime.put("vsync", false)
            val original = base.toJson().toString()
            val high = ViewerSettingsStore.validate(ViewerConfig.highQualityPreset(base))
            check(base.toJson().toString() == original)
            check(high.generation == 8L && high.configHash.isEmpty())
            check(high.maximumInternalEdge == 1920 && high.maximumInternalPixels == 2073600L)
            val r = high.runtime; val l = r.getJSONObject("lod"); val s = r.getJSONObject("streaming")
            check(r.getLong("activeCapacity") == 1000000L && r.getLong("sortCapacity") == 4500000L)
            check(l.getDouble("qualityPixels") == 0.1 && l.getLong("activeBudget") == 1000000L)
            check(l.getLong("sortBudget") == 4500000L && !l.getBoolean("freeze"))
            check(r.getInt("displaySHDegree") == 3 && r.getInt("shStorageMode") == 0)
            check(!r.getBoolean("projectedFusion") && !r.getBoolean("projectionColorCompression"))
            check(s.getLong("sceneGPUBytes") == 224L shl 20 && s.getLong("cpuCacheBytes") == 24L shl 20)
            check(s.getInt("maxConcurrentReads") == 1 && !r.getBoolean("vsync"))
            r.put("vsync", true); check(!base.runtime.getBoolean("vsync"))
        }
        test("quality_explicit_save_preserves_selection_and_unknown_schema") { isolated { store, file ->
            store.save(ViewerConfig(), selectedSceneId = "retained-user-scene")
            val saved = store.load(); store.save(ViewerConfig.highQualityPreset(saved.requested))
            check(store.load().selectedSceneId == "retained-user-scene")
            check(store.load().requested.maximumInternalEdge == 1920)
            file.writeText("{\"schemaVersion\":999,\"original\":true}")
            val before = file.readBytes()
            check(runCatching { store.save(ViewerConfig.highQualityPreset(ViewerConfig())) }.isFailure)
            check(file.readBytes().contentEquals(before))
        } }
        test("quality_fresh_default_is_high_without_mutating_saved_low_request") { isolated { store, _ ->
            check(store.load().requested.maximumInternalEdge == 1920)
            check(store.load().requested.runtime.getJSONObject("lod").getDouble("qualityPixels") == 0.1)
            store.save(ViewerConfig(), selectedSceneId = "saved-low-quality")
            check(store.load().requested.maximumInternalEdge == 1280)
            check(store.load().selectedSceneId == "saved-low-quality")
            check(runCatching { ViewerConfig.highQualityPreset(ViewerConfig(generation = Long.MAX_VALUE)) }.isFailure)
        } }
        test("controller_blocked_diagnostics_expires_fps_and_gui_readiness_independent_of_fps_switch") {
            // Scheduler-only contract: this fixture makes no rendering/GPU acceptance claim.
            val root = File(context.cacheDir, "bonsai-controller-${UUID.randomUUID()}")
            check(root.mkdirs())
            val wrapper = object : ContextWrapper(context) {
                override fun getApplicationContext(): Context = this
                override fun getFilesDir(): File = root
            }
            val repository = SceneRepository(wrapper); val jobs = BuildJobStore(wrapper)
            val main = Handler(Looper.getMainLooper())
            val controller = AtomicReference<ViewerController>()
            fun onMain(body: () -> Unit) {
                val done = CountDownLatch(1); val error = AtomicReference<Throwable>()
                main.post { try { body() } catch (e: Throwable) { error.set(e) } finally { done.countDown() } }
                check(done.await(10, TimeUnit.SECONDS)) { "Main thread fixture timed out" }
                error.get()?.let { throw it }
            }
            fun field(name: String) = ViewerController::class.java.getDeclaredField(name).apply { isAccessible = true }
            try {
                onMain {
                    val c = ViewerController(wrapper); controller.set(c)
                    field("repository").set(c, repository); field("jobStore").set(c, jobs)
                    field("settingsStore").set(c, ViewerSettingsStore(wrapper))
                    c.setResumed(true)
                    if (BuildConfig.FPS_OVERLAY_ENABLED) {
                        val sampler = field("fpsSampler").get(c) as ObservedFpsSampler
                        sampler.observe(sample()); val fps = sampler.observe(sample(2)); check(fps == 50.0)
                        field("state").set(c, c.state.copy(observedFps = fps))
                        (field("pollQueued").get(c) as AtomicBoolean).set(true)
                    } else {
                        // GUI status observations now run with FPS disabled; stale READY
                        // must expire on the main thread even when that worker is blocked.
                        check(c.state.observedFps == null)
                        field("state").set(c, c.state.copy(presentation = ViewerPresentation.READY,
                            effective = ViewerConfig()))
                        field("presentationObservedAt").setLong(c, android.os.SystemClock.elapsedRealtime())
                        (field("pollQueued").get(c) as AtomicBoolean).set(true)
                    }
                }
                val elapsed = CountDownLatch(1); main.postDelayed({ elapsed.countDown() }, 4600)
                check(elapsed.await(10, TimeUnit.SECONDS))
                onMain {
                    val c = controller.get()
                    if (BuildConfig.FPS_OVERLAY_ENABLED)
                        check(c.state.observedFps == null) { "Stale FPS survives blocked diagnostics for more than two refresh periods" }
                    else {
                        check(c.state.observedFps == null)
                        check(c.state.presentation != ViewerPresentation.READY && c.state.effective == null) {
                            "Stale GUI readiness/effective settings survive blocked diagnostics with FPS disabled"
                        }
                    }
                }
            } finally {
                onMain { controller.get()?.close() }; root.deleteRecursively()
            }
        }
        test("controller_queued_initialize_then_close_publishes_nothing") {
            val root = File(context.cacheDir, "bonsai-closed-${UUID.randomUUID()}")
            check(root.mkdirs())
            val wrapper = object : ContextWrapper(context) {
                override fun getApplicationContext(): Context = this
                override fun getFilesDir(): File = root
            }
            val main = Handler(Looper.getMainLooper())
            val controller = AtomicReference<ViewerController>()
            val events = AtomicInteger(0)
            fun onMain(body: () -> Unit) {
                val done = CountDownLatch(1); val error = AtomicReference<Throwable>()
                main.post { try { body() } catch (e: Throwable) { error.set(e) } finally { done.countDown() } }
                check(done.await(10, TimeUnit.SECONDS)) { "Main thread fixture timed out" }
                error.get()?.let { throw it }
            }
            try {
                onMain {
                    val c = ViewerController(wrapper); controller.set(c)
                    c.onState = { events.incrementAndGet() }
                    c.initialize()
                    c.close()
                }
                Thread.sleep(500)
                onMain {
                    val c = controller.get()
                    c.setResumed(true); c.setResumed(false); c.retry()
                    check(c.onState == null) { "Closed controller retained its state observer" }
                    check(events.get() == 0) { "Closed controller published ${events.get()} state updates" }
                }
            } finally {
                onMain { controller.get()?.close() }; root.deleteRecursively()
            }
        }
        test("controller_corrupt_settings_stays_visible_and_preserves_bytes") {
            val root = File(context.cacheDir, "bonsai-corrupt-settings-${UUID.randomUUID()}")
            check(root.mkdirs())
            val file = File(root, "viewer-settings.json")
            val original = "{\"schemaVersion\":999,\"opaque\":\"retain\"}"
            file.writeText(original)
            val wrapper = object : ContextWrapper(context) {
                override fun getApplicationContext(): Context = this
                override fun getFilesDir(): File = root
            }
            val main = Handler(Looper.getMainLooper())
            val controller = AtomicReference<ViewerController>()
            fun onMain(body: () -> Unit) {
                val done = CountDownLatch(1); val error = AtomicReference<Throwable>()
                main.post { try { body() } catch (e: Throwable) { error.set(e) } finally { done.countDown() } }
                check(done.await(10, TimeUnit.SECONDS)) { "Main thread fixture timed out" }
                error.get()?.let { throw it }
            }
            try {
                onMain { controller.set(ViewerController(wrapper).also { it.initialize() }) }
                val deadline = SystemClock.elapsedRealtime() + 10000
                while (SystemClock.elapsedRealtime() < deadline && controller.get().state.busy) Thread.sleep(50)
                onMain {
                    val c = controller.get()
                    check(!c.state.busy) { "Corrupt settings left the controller busy" }
                    check(!c.state.nativeAvailable) { "Corrupt settings were silently replaced by a fresh profile" }
                    check(c.state.message.contains("初始化未完成")) { "Permanent settings failure was hidden: ${c.state.message}" }
                    check(file.readText() == original) { "Original unknown-schema bytes were rewritten" }
                }
            } finally {
                onMain { controller.get()?.close() }; root.deleteRecursively()
            }
        }
        test("controller_recreation_waits_for_native_retirement_acknowledgement") {
            val root = File(context.cacheDir, "bonsai-owner-recreation-${UUID.randomUUID()}")
            check(root.mkdirs())
            val wrapper = object : ContextWrapper(context) {
                override fun getApplicationContext(): Context = this
                override fun getFilesDir(): File = root
            }
            val main = Handler(Looper.getMainLooper())
            val first = AtomicReference<ViewerController>()
            val second = AtomicReference<ViewerController>()
            fun onMain(body: () -> Unit) {
                val done = CountDownLatch(1); val error = AtomicReference<Throwable>()
                main.post { try { body() } catch (e: Throwable) { error.set(e) } finally { done.countDown() } }
                check(done.await(10, TimeUnit.SECONDS)) { "Main thread fixture timed out" }
                error.get()?.let { throw it }
            }
            fun await(timeoutMillis: Long, predicate: () -> Boolean) {
                val deadline = SystemClock.elapsedRealtime() + timeoutMillis
                while (SystemClock.elapsedRealtime() < deadline) {
                    if (predicate()) return
                    Thread.sleep(50)
                }
                error("Timed out waiting for native owner lifecycle event: ${File(root, "viewer-owner-lifecycle.json").takeIf { it.exists() }?.readText()}")
            }
            try {
                onMain { first.set(ViewerController(wrapper).also { it.initialize() }) }
                await(30000) {
                    val c = first.get()
                    c.state.nativeAvailable || File(root, "viewer-owner-lifecycle.json").let { file ->
                        file.exists() && JSONObject(file.readText()).optString("phase") == "creation_failed"
                    }
                }
                check(first.get().state.nativeAvailable) {
                    "Initial native owner creation failed: ${File(root, "viewer-owner-lifecycle.json").readText()}"
                }
                onMain {
                    first.get().close()
                    second.set(ViewerController(wrapper).also { it.initialize() })
                }
                await(30000) {
                    second.get().state.nativeAvailable || File(root, "viewer-owner-lifecycle.json").let { file ->
                        file.exists() && JSONObject(file.readText()).optString("phase") == "creation_failed"
                    }
                }
                check(second.get().state.nativeAvailable) {
                    "Successor native owner creation failed: ${File(root, "viewer-owner-lifecycle.json").readText()}"
                }
                val record = JSONObject(File(root, "viewer-owner-lifecycle.json").readText())
                check(record.optString("phase") == "created")
                check(record.optJSONObject("previousRetirement")?.optBoolean("ok") == true) {
                    "Successor was created without a successful prior retirement acknowledgement: $record"
                }
            } finally {
                onMain { second.get()?.close() }
                root.deleteRecursively()
            }
        }
    }
}
