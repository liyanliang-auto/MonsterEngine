package com.monsterengine.viewer

import android.content.Context
import android.system.Os
import android.system.OsConstants
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

/** Explicit local import/package policy, independent from renderer residency. */
data class SceneStorageLimits(
    val maxSourceBytes: Long = 16L shl 30, val maxArchiveBytes: Long = 0xffffffffL,
    val maxExpandedBytes: Long = 16L shl 30, val maxEntryBytes: Long = 256L shl 20,
    val maxEntries: Int = 100000, val maxMetadataBytes: Long = 16L shl 20,
    val diskReserveBytes: Long = 256L shl 20, val tinyPLYBytes: Long = 32L shl 20,
    val tinyPLYRecords: Long = 100000
) {
    init { require(listOf(maxSourceBytes, maxArchiveBytes, maxExpandedBytes, maxEntryBytes,
        maxMetadataBytes, tinyPLYBytes, tinyPLYRecords).all { it > 0 })
        require(maxArchiveBytes <= 0xffffffffL && maxEntries in 1..100000 && diskReserveBytes in 0..Long.MAX_VALUE-65536) }
}

/** Single authority for private scene publication. Slow operations belong on a worker.
 * All instances share process leases; a tombstone survives process death, while leases do not.
 */
class SceneRepository(context: Context, val limits: SceneStorageLimits = SceneStorageLimits()) {
    val root: File = File(context.applicationContext.filesDir, "scene-store")
    val buildInputs: File = File(root, "build-inputs")
    private val stagingRoot = File(root, "staging")
    private val publishedRoot = File(root, "published")
    init { listOf(root, buildInputs, stagingRoot, publishedRoot).forEach {
            check(!Files.isSymbolicLink(it.toPath()) && (it.mkdirs() || it.isDirectory)) }
        check(root.canonicalFile.parentFile == context.applicationContext.filesDir.canonicalFile)
        check(buildInputs.canonicalFile.parentFile == root.canonicalFile)
        check(stagingRoot.canonicalFile.parentFile == root.canonicalFile)
        check(publishedRoot.canonicalFile.parentFile == root.canonicalFile)
        synchronized(lock) { publishedRoot.listFiles()?.filter { it.isDirectory && File(it, "delete-request.json").isFile }
            ?.forEach { if ((leases[it.canonicalPath] ?: 0) == 0) removeTree(it) } } }

    /** Allocate a new isolated transaction; caller owns cleanup until publish succeeds. */
    fun createStaging(): File = File(stagingRoot, UUID.randomUUID().toString()).also {
        check(it.mkdir())
        try { sync(stagingRoot) } catch (error: Exception) { removeTree(it); throw error }
    }

    /** Revalidate complete native content, durably commit descriptor, then atomically publish. */
    fun publish(staging: File, kind: ViewerSceneKind, contentHash: String, cancelled: () -> Boolean = { false }): SceneDescriptor {
        requireStaging(staging)
        val id = UUID.randomUUID().toString()
        val destination = File(publishedRoot, id)
        var renamed = false
        try {
            check(!cancelled()) { "Cancelled" }
            require(contentHash.matches(Regex("[0-9a-f]{64}")))
            validateTransactionTree(staging, cancelled)
            val payload = if (kind == ViewerSceneKind.PLY) File(staging, "source.ply") else File(staging, "scene.mgs")
            check(payload.isFile && !Files.isSymbolicLink(payload.toPath())) { "Scene payload missing" }
            check(payload.length() in 1..limits.maxSourceBytes) { "Scene payload exceeds storage limit" }
            val result = NativeBridge.validateScene(if (kind == ViewerSceneKind.PLY) payload.absolutePath else staging.absolutePath)
            check(result.ok) { "Scene validation failed: ${result.code}: ${result.message}" }
            check(!cancelled()) { "Cancelled" }
            check(sha256(payload, cancelled) == contentHash) { "Published content identity mismatch" }
            val canonicalDestination = destination.canonicalPath
            val descriptor = SceneDescriptor(id, canonicalDestination,
                sourcePLY = if (kind == ViewerSceneKind.PLY) File(canonicalDestination, "source.ply").path else "",
                manifestPath = if (kind == ViewerSceneKind.LOD) File(canonicalDestination, "scene.mgs").path else "",
                contentHash = contentHash, generation = 1, kind = kind)
            writeDurable(File(staging, "descriptor.json"), descriptor.toJson().toString().toByteArray(Charsets.UTF_8))
            syncTransactionTree(staging, cancelled)
            synchronized(lock) {
                check(!cancelled()) { "Cancelled" }
                requireStaging(staging)
                check(!destination.exists()) { "Scene destination already exists" }
                Os.rename(staging.absolutePath, destination.absolutePath)
                renamed = true
                sync(publishedRoot)
            }
            return descriptor
        } catch (error: Exception) {
            try {
                if (renamed) { removeTree(destination); sync(publishedRoot) }
                else if (staging.exists()) removeTree(staging)
                sync(stagingRoot)
            } catch (cleanup: Exception) { error.addSuppressed(cleanup) }
            throw error
        }
    }

    /** List only atomically published, untombstoned descriptors. */
    fun list(): List<SceneDescriptor> = synchronized(lock) {
        publishedRoot.listFiles()?.asSequence()?.filter { it.isDirectory && !Files.isSymbolicLink(it.toPath()) }
            ?.filter { !File(it, "delete-request.json").exists() }
            ?.mapNotNull { directory -> try { readDescriptor(directory) } catch (_: Exception) { null } }?.toList() ?: emptyList()
    }

    /** A lease pins on-disk scene files until the render/build owner acknowledges release. */
    fun acquire(sceneId: String): SceneLease = synchronized(lock) {
        val directory = sceneDirectory(sceneId)
        check(!File(directory, "delete-request.json").exists()) { "Scene deletion pending" }
        val descriptor = readDescriptor(directory)
        val key = directory.canonicalPath
        leases[key] = Math.addExact(leases[key] ?: 0, 1)
        SceneLease(descriptor) { synchronized(lock) {
            val remaining = checkNotNull(leases[key]) - 1
            if (remaining == 0) {
                leases.remove(key)
                if (File(directory, "delete-request.json").exists()) removeTree(directory)
            } else leases[key] = remaining
        } }
    }

    /** False means existing owners still hold leases; the scene is already hidden from new opens. */
    fun requestDelete(sceneId: String): Boolean = synchronized(lock) {
        val directory = sceneDirectory(sceneId)
        if (!directory.exists()) return@synchronized true
        writeDurable(File(directory, "delete-request.json"), "{}".toByteArray())
        sync(directory)
        if ((leases[directory.canonicalPath] ?: 0) > 0) return@synchronized false
        removeTree(directory); sync(publishedRoot); true
    }

    /** Only this transaction's private staging tree can be discarded by import code. */
    fun discard(staging: File) {
        requireStaging(staging)
        if (staging.exists()) { removeTree(staging); sync(stagingRoot) }
    }

    /** Remove an unleased stable import only after its build owner has retired. */
    fun discardBuildInput(source: File) {
        require(source.canonicalFile.parentFile == buildInputs.canonicalFile && !Files.isSymbolicLink(source.toPath()))
        require(source.isFile)
        Files.deleteIfExists(source.toPath()); sync(buildInputs)
    }

    private fun sceneDirectory(id: String): File {
        require(id.matches(Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")))
        return File(publishedRoot, id).also {
            require(!Files.isSymbolicLink(it.toPath()))
            require(it.canonicalFile.parentFile == publishedRoot.canonicalFile)
        }
    }
    private fun readDescriptor(directory: File): SceneDescriptor {
        require(!Files.isSymbolicLink(directory.toPath()) && directory.canonicalFile.parentFile == publishedRoot.canonicalFile)
        val file = File(directory, "descriptor.json")
        check(file.isFile && file.length() in 1..65536 && !Files.isSymbolicLink(file.toPath()))
        val descriptor = SceneDescriptor.fromJson(JSONObject(file.readText()))
        check(descriptor.sceneId == directory.name && File(descriptor.directory).canonicalFile == directory.canonicalFile)
        check(descriptor.contentHash.matches(Regex("[0-9a-f]{64}")) && descriptor.generation > 0)
        val source = File(directory, "source.ply")
        val manifest = File(directory, "scene.mgs")
        when (descriptor.kind) {
            ViewerSceneKind.PLY -> check(descriptor.sourcePLY.isNotEmpty() && descriptor.manifestPath.isEmpty() &&
                File(descriptor.sourcePLY).canonicalFile == source.canonicalFile && source.isFile && !Files.isSymbolicLink(source.toPath()))
            ViewerSceneKind.LOD -> check(descriptor.manifestPath.isNotEmpty() && descriptor.sourcePLY.isEmpty() &&
                File(descriptor.manifestPath).canonicalFile == manifest.canonicalFile && manifest.isFile && !Files.isSymbolicLink(manifest.toPath()))
        }
        return descriptor
    }

    private fun requireStaging(staging: File) {
        require(staging.name.matches(Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")))
        require(!Files.isSymbolicLink(staging.toPath()) && staging.canonicalFile.parentFile == stagingRoot.canonicalFile)
    }

    private fun validateTransactionTree(staging: File, cancelled: () -> Boolean) {
        var count = 0
        Files.walk(staging.toPath()).use { paths -> paths.forEach { path ->
            check(!cancelled()) { "Cancelled" }
            check(++count <= limits.maxEntries + 16) { "Scene file count exceeded" }
            check(!Files.isSymbolicLink(path)) { "Scene links are forbidden" }
            val file = path.toFile()
            check(file.isDirectory || file.isFile) { "Scene special files are forbidden" }
            if (file.isFile) check(file.length() <= limits.maxEntryBytes || file.name == "source.ply") { "Scene entry exceeds storage limit" }
        } }
    }

    private fun syncTransactionTree(staging: File, cancelled: () -> Boolean) {
        val paths = Files.walk(staging.toPath()).use { stream -> stream.collect(java.util.stream.Collectors.toList()) }
        paths.filter { !Files.isDirectory(it) }.forEach { path -> check(!cancelled()) { "Cancelled" }; sync(path.toFile()) }
        paths.filter { Files.isDirectory(it) }.sortedWith(Comparator.comparingInt<java.nio.file.Path> { it.nameCount }.reversed())
            .forEach { path -> check(!cancelled()) { "Cancelled" }; sync(path.toFile()) }
    }

    companion object {
        private val lock = Any()
        private val leases = mutableMapOf<String, Int>()
        internal fun sync(file: File) {
            val directory = file.isDirectory
            val fd = Os.open(file.absolutePath,
                OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW, 0)
            try {
                if (directory) check(OsConstants.S_ISDIR(Os.fstat(fd).st_mode)) { "Expected directory for sync" }
                Os.fsync(fd)
            } finally { Os.close(fd) }
        }
        internal fun writeDurable(file: File, bytes: ByteArray) {
            FileOutputStream(file).use { it.write(bytes); it.fd.sync() }
        }
        internal fun removeTree(root: File) {
            if (!root.exists()) return
            Files.walk(root.toPath()).use { paths -> paths.sorted(Comparator.reverseOrder()).forEach { Files.delete(it) } }
        }
        internal fun sha256(file: File, cancelled: () -> Boolean = { false }): String {
            val hash = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { input -> val buffer = ByteArray(65536)
                while (true) { check(!cancelled()) { "Cancelled" }; val count = input.read(buffer); if (count < 0) break
                    check(count > 0); hash.update(buffer, 0, count) } }
            return hash.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        }
    }
}

/** Idempotent process lease. Close only after native submissions and workers retire. */
class SceneLease internal constructor(val descriptor: SceneDescriptor, private val release: () -> Unit) : AutoCloseable {
    private val closed = AtomicBoolean(false)
    override fun close() { if (closed.compareAndSet(false, true)) release() }
}
