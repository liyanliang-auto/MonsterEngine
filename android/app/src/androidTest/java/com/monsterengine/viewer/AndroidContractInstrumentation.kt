package com.monsterengine.viewer

import android.app.Instrumentation
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.system.Os
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** Framework-only instrumentation: every native and import call uses production code. */
class AndroidContractInstrumentation : Instrumentation() {
    override fun onCreate(arguments: Bundle?) { super.onCreate(arguments); start() }
    override fun onStart() {
        val cases = JSONArray()
        fun run(name: String, body: () -> Unit) {
            val result = JSONObject().put("name", name)
            try { body(); result.put("passed", true) }
            catch (error: Throwable) { result.put("passed", false).put("error", error.toString().take(4096)) }
            cases.put(result)
            sendStatus(0, Bundle().apply { putString("stream", result.toString() + "\n") })
        }
        BonsaiContinuationTests.run(targetContext, ::run)
        PresentationDiagnosticsTests.run(targetContext, ::run)
        run("native_public_descriptor_cannot_override_trusted_builtin_camera") {
            val owner = NativeBridge.create(targetContext.assets, targetContext.filesDir.absolutePath, ViewerConfig())
            check(owner.isValid) { "Native owner creation failed" }
            try {
                val request = JSONObject().put("schemaVersion", 1).put("kind", "builtinScene")
                val builtin = NativeBridge.snapshot(owner, request)
                check(builtin.ok) { builtin.toString() }
                val descriptor = builtin.payload.getJSONObject("descriptor")
                for (key in listOf("initialCamera", "camera")) {
                    val injected = JSONObject(descriptor.toString()).put(key, JSONObject().put("rollDegrees", 0))
                    val result = NativeResult.fromJson(NativeBridge.nativeOpenScene(owner.value, injected.toString()))
                    check(!result.ok) { "Public descriptor admitted camera override" }
                }
            } finally { check(NativeBridge.destroy(owner).ok) }
        }
        run("native_missing_leaves_and_mixed_aliases") { fixture { f ->
            val aliasSource = f.source.absolutePath.replace("/data/user/0/", "/data/data/")
            val handle = NativeBridge.createBuild(aliasSource, f.journal.path, f.output.path, BoundedBuildConfig())
            check(handle.isValid) { NativeBridge.takeBuildCreationFailure().toString() }
            check(!f.journal.exists() && !f.output.exists()) { "Creation wrote output before Run" }
            check(NativeBridge.destroyBuild(handle).ok)
        } }
        run("native_required_source_missing") { fixture { f ->
            check(f.source.delete()); reject(f, "stable_source")
        } }
        run("native_traversal_rejected") { fixture { f ->
            reject(f.copy(source = File(f.source.parentFile, "../build-inputs/${f.source.name}")), "Traversal")
        } }
        run("native_outside_root_rejected") { fixture { f ->
            reject(f.copy(source = File("/proc/version")), "escapes")
        } }
        run("native_missing_parent_rejected") { fixture { f ->
            reject(f.copy(output = File(f.output, "child")), "parent")
        } }
        run("native_nondirectory_parent_rejected") { fixture { f ->
            reject(f.copy(output = File(f.source, "child")), "filesystem error")
        } }
        run("native_dangling_link_rejected") { fixture { f ->
            Os.symlink(File(f.output.parentFile, "absent-${UUID.randomUUID()}").path, f.output.path)
            reject(f, "output_path")
        } }
        run("native_descendant_link_rejected") { fixture { f ->
            Os.symlink(f.source.path, f.output.path)
            reject(f, "symlink")
        } }
        run("native_descendant_root_alias_rejected") { fixture { f ->
            Os.symlink(targetContext.filesDir.canonicalPath, f.output.path)
            val alias = File(f.output, "scene-store/build-inputs/${f.source.name}")
            reject(f.copy(source = alias), "symlink")
        } }
        run("native_bounded_consumed_diagnostic_and_success_reset") { fixture { f ->
            val config = BoundedBuildConfig().toJson().put("x".repeat(6000), true).toString()
            check(NativeBridge.nativeCreateBuild(f.source.path, f.journal.path, f.output.path, config) == 0L)
            val error = NativeResult.fromJson(NativeBridge.nativeTakeBuildCreationError())
            check(error.message.length == 4096 && error.message.startsWith("build_policy:"))
            val empty = NativeResult.fromJson(NativeBridge.nativeTakeBuildCreationError())
            check(empty.message == "No creation diagnostic on this worker")
            check(NativeBridge.nativeCreateBuild("/proc/version", f.journal.path, f.output.path, "{}") == 0L)
            val token = NativeBridge.nativeCreateBuild(f.source.path, f.journal.path, f.output.path, BoundedBuildConfig().toJson().toString())
            check(token != 0L)
            try { check(NativeResult.fromJson(NativeBridge.nativeTakeBuildCreationError()).message == empty.message) }
            finally { NativeBridge.nativeDestroyBuild(token) }
        } }
        run("native_worker_local_concurrent_diagnostics") {
            val ready = CountDownLatch(2); val release = CountDownLatch(1)
            val captured = CountDownLatch(2); val consume = CountDownLatch(1)
            val failure = AtomicReference<Throwable?>()
            val threads = listOf("stable_source", "output_path").map { phase -> Thread {
                try { fixture { f ->
                    val invalid = if (phase == "stable_source") f.copy(source = File(f.source.parentFile, "missing-${UUID.randomUUID()}"))
                        else f.copy(output = File("/proc/version"))
                    check(NativeBridge.nativeCreateBuild(invalid.source.path, invalid.journal.path, invalid.output.path,
                        BoundedBuildConfig().toJson().toString()) == 0L)
                    ready.countDown(); check(release.await(10, TimeUnit.SECONDS))
                    check(NativeResult.fromJson(NativeBridge.nativeTakeBuildCreationError()).message.startsWith("$phase:"))
                    check(!NativeBridge.createBuild(invalid.source.path, invalid.journal.path, invalid.output.path, BoundedBuildConfig()).isValid)
                    captured.countDown(); check(consume.await(10, TimeUnit.SECONDS))
                    check(NativeBridge.takeBuildCreationFailure().message.startsWith("$phase:"))
                    check(NativeBridge.takeBuildCreationFailure().message == "No creation diagnostic on this worker")
                } } catch (error: Throwable) { failure.compareAndSet(null, error); ready.countDown(); captured.countDown() }
            }.also { it.start() } }
            check(ready.await(10, TimeUnit.SECONDS)); release.countDown()
            check(captured.await(10, TimeUnit.SECONDS)); consume.countDown()
            threads.forEach { it.join(15000); check(!it.isAlive) }
            failure.get()?.let { throw it }
        }
        run("native_incompatible_checkpoint_preserves_committed_work") { fixture { f ->
            val config = BoundedBuildConfig()
            val first = NativeBridge.createBuild(f.source.path, f.journal.path, f.output.path, config)
            check(first.isValid) { NativeBridge.takeBuildCreationFailure().toString() }
            try { check(NativeBridge.runBuild(first).ok) { NativeBridge.buildSnapshot(first).toString() } }
            finally { check(NativeBridge.destroyBuild(first).ok) }
            check(File(f.journal, "checkpoint.current").isFile && File(f.output, "scene.mgs").isFile)
            val journalBefore = treeInventory(f.journal)
            val outputBefore = treeInventory(f.output)
            val sourceBefore = SceneRepository.sha256(f.source)
            val rejectedOutput = File(f.output.parentFile, UUID.randomUUID().toString())
            try {
                val incompatible = NativeBridge.createBuild(f.source.path, f.journal.path, rejectedOutput.path, config.copy(supportSigma = 4f))
                check(incompatible.isValid) { NativeBridge.takeBuildCreationFailure().toString() }
                val result = try { NativeBridge.runBuild(incompatible) } finally { check(NativeBridge.destroyBuild(incompatible).ok) }
                check(!result.ok && result.message.contains("Incompatible checkpoint source/parser/builder/config identity")) { result.toString() }
                check(!rejectedOutput.exists()) { "Incompatible checkpoint published new output" }
                check(treeInventory(f.journal) == journalBefore && treeInventory(f.output) == outputBefore)
                check(SceneRepository.sha256(f.source) == sourceBefore)
            } finally { rejectedOutput.deleteRecursively() }
        } }
        run("import_active_copy_cancel_cleanup_preserves_published") { importCase { repository, before ->
            var copied = 0L
            val error = expectFailure { SceneImportController(targetContext, repository).importDocument(document("slow.ply"),
                cancelled = { copied > 0 }, progress = { copied = it }) }
            check(copied > 0 && error.message == "Cancelled")
            check(inventory(repository) == before)
        } }
        run("import_resource_rejection_cleanup_preserves_published") { importCase { repository, before ->
            val limited = SceneRepository(targetContext, SceneStorageLimits(maxSourceBytes = 65536, diskReserveBytes = 0))
            var copied = 0L
            val error = expectFailure { SceneImportController(targetContext, limited).importDocument(document("slow.ply"), progress = { copied = it }) }
            check(copied > 0 && error.message == "Source byte limit exceeded")
            check(inventory(repository) == before)
        } }
        run("import_malformed_rollback_preserves_published") { importCase { repository, before ->
            val error = expectFailure { SceneImportController(targetContext, repository).importDocument(document("malformed.ply")) }
            check(error.message == "Unsupported scene document")
            check(inventory(repository) == before)
        } }
        val failed = (0 until cases.length()).count { !cases.getJSONObject(it).getBoolean("passed") }
        val report = JSONObject().put("schemaVersion", 1).put("total", cases.length()).put("passed", cases.length() - failed)
            .put("failed", failed).put("cases", cases).put("coverage", "not_instrumented")
        File(targetContext.filesDir, "android-contract-tests.json").writeText(report.toString(2))
        finish(if (failed == 0) -1 else 0, Bundle().apply { putString("stream", report.toString()) })
    }
    private data class Fixture(val source: File, val journal: File, val output: File)
    private fun fixture(body: (Fixture) -> Unit) {
        val repository = SceneRepository(targetContext)
        val source = File(repository.buildInputs, "contract-${UUID.randomUUID()}.ply")
        source.writeText(VerificationDocumentsProvider.header(1) + "0 0 0 0 0 0 1 -3 -3 -3 1 0 0 0\n")
        val job = File(targetContext.filesDir, "build-jobs/${UUID.randomUUID()}").also { check(it.mkdirs()) }
        val output = File(repository.root, "staging/${UUID.randomUUID()}")
        try { body(Fixture(source, File(job, "journal"), output)) }
        finally { source.delete(); job.deleteRecursively(); if (java.nio.file.Files.isSymbolicLink(output.toPath())) output.delete() else output.deleteRecursively() }
    }
    private fun reject(f: Fixture, reason: String) {
        val handle = NativeBridge.createBuild(f.source.path, f.journal.path, f.output.path, BoundedBuildConfig())
        if (handle.isValid) { NativeBridge.destroyBuild(handle); error("Unexpected admitted invalid request") }
        val failure = NativeBridge.takeBuildCreationFailure()
        check(failure.code == "native_creation_rejected" && failure.message.contains(reason)) { failure.toString() }
    }
    private fun document(name: String): Uri = DocumentsContract.buildDocumentUri("com.monsterengine.viewer.debug.test.documents", name).also {
        val granted = targetContext.contentResolver.call(Uri.parse("content://com.monsterengine.viewer.debug.test.documents"),
            "grant-test-document", name, null)
        check(granted?.getBoolean("granted") == true) { "Provider did not grant generated fixture" }
    }
    private fun inventory(repository: SceneRepository): Map<String, String> = treeInventory(repository.root)
    private fun treeInventory(root: File): Map<String, String> = root.walkTopDown().associate {
        it.relativeTo(root).path to if (it.isDirectory) "directory" else SceneRepository.sha256(it)
    }
    private fun importCase(body: (SceneRepository, Map<String, String>) -> Unit) {
        val repository = SceneRepository(targetContext)
        val published = SceneImportController(targetContext, repository).importDocument(document("small.ply")) as ImportOutcome.Published
        try {
            val before = inventory(repository)
            check(before.keys.any { it.contains(published.descriptor.sceneId) })
            body(repository, before)
            check(repository.list().any { it.sceneId == published.descriptor.sceneId })
        } finally { check(repository.requestDelete(published.descriptor.sceneId)) }
    }
    private fun expectFailure(body: () -> Unit): Exception {
        try { body() } catch (error: Exception) { return error }
        error("Expected transaction rejection")
    }
}
