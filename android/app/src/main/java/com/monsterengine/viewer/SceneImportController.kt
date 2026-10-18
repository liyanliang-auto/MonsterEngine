package com.monsterengine.viewer

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.UUID

/** Import never starts a foreground service or changes the currently rendered scene. */
sealed class ImportOutcome {
    data class Published(val descriptor: SceneDescriptor) : ImportOutcome()
    data class BuildRequired(val stableSource: File, val sha256: String, val bytes: Long) : ImportOutcome()
}

/** SAF import worker; content URIs never cross the native filesystem boundary. */
class SceneImportController(context: Context, private val repository: SceneRepository) {
    private val resolver = context.applicationContext.contentResolver

    /** Keep the copy transaction local so every failure path removes only its own files. */
    private class ImportTransaction(private val repository: SceneRepository) : AutoCloseable {
        val temporary = File(repository.buildInputs, UUID.randomUUID().toString() + ".part")
        private var retained = false
        fun retainStableSource() { retained = true }
        fun isRetained(): Boolean = retained
        override fun close() {
            if (!retained) temporary.delete()
        }
    }

    /** Copy/hash with bounded buffers, cancellation and repeated real free-space checks.
     * Large PLY yields BuildRequired; UI must ask/start the explicit user-initiated build.
     */
    fun importDocument(uri: Uri, cancelled: () -> Boolean = { false }, progress: (Long) -> Unit = {}): ImportOutcome {
        require(uri.scheme == "content") { "Choose a document through SAF" }
        val transaction = ImportTransaction(repository)
        val temporary = transaction.temporary
        var stableSource: File? = null
        try {
            val hash = MessageDigest.getInstance("SHA-256"); var copied = 0L
            var sourceLimit = repository.limits.maxSourceBytes
            val sampledPrefix = ByteArray(4); var prefixCount = 0; var classified = false
            check(!cancelled()) { "Cancelled" }
            val opened = checkNotNull(resolver.openInputStream(uri)) { "Cannot open selected document" }
            opened.use { input ->
                FileOutputStream(temporary).use { output -> val buffer = ByteArray(65536)
                    while (true) {
                        check(!cancelled()) { "Cancelled" }
                        val count = input.read(buffer); if (count < 0) break; check(count > 0)
                        if (!classified) {
                            val take = minOf(count, sampledPrefix.size - prefixCount)
                            buffer.copyInto(sampledPrefix, prefixCount, 0, take); prefixCount += take
                            if (prefixCount == sampledPrefix.size) {
                                if (isZipSignature(sampledPrefix, prefixCount)) sourceLimit = minOf(sourceLimit, repository.limits.maxArchiveBytes)
                                classified = true
                            }
                        }
                        copied = Math.addExact(copied, count.toLong())
                        check(copied <= sourceLimit) { "Source byte limit exceeded" }
                        val required = Math.addExact(repository.limits.diskReserveBytes, copied)
                        check(temporary.parentFile!!.usableSpace >= required) { "Insufficient disk space" }
                        output.write(buffer, 0, count); hash.update(buffer, 0, count); progress(copied)
                    }
                    output.fd.sync()
                }
            }
            check(!cancelled()) { "Cancelled" }
            val digest = hash.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
            val prefix = temporary.inputStream().use { input -> ByteArray(4).also { check(input.read(it) == 4) { "Document too short" } } }
            if (isZipSignature(prefix, prefix.size)) {
                check(copied <= repository.limits.maxArchiveBytes) { "Package byte limit exceeded" }
                return ImportOutcome.Published(ScenePackage(repository).importPackage(temporary, cancelled, progress))
            }
            check(prefix.contentEquals(byteArrayOf('p'.code.toByte(), 'l'.code.toByte(), 'y'.code.toByte(), 10)) ||
                prefix.contentEquals(byteArrayOf('p'.code.toByte(), 'l'.code.toByte(), 'y'.code.toByte(), 13))) {
                "Unsupported scene document"
            }
            val declared = declaredPLYCount(temporary, cancelled)
            if (copied > repository.limits.tinyPLYBytes || declared > repository.limits.tinyPLYRecords) {
                val stable = File(repository.buildInputs, UUID.randomUUID().toString() + ".ply")
                stableSource = stable
                android.system.Os.rename(temporary.absolutePath, stable.absolutePath)
                check(!cancelled()) { "Cancelled" }
                SceneRepository.sync(repository.buildInputs)
                transaction.retainStableSource()
                return ImportOutcome.BuildRequired(stable, digest, copied)
            }
            check(!cancelled()) { "Cancelled" }
            val staging = repository.createStaging()
            try {
                android.system.Os.rename(temporary.absolutePath, File(staging, "source.ply").absolutePath)
                check(!cancelled()) { "Cancelled" }
                return ImportOutcome.Published(repository.publish(staging, ViewerSceneKind.PLY, digest, cancelled))
            } catch (error: Exception) { repository.discard(staging); throw error }
        } finally {
            transaction.close()
            if (stableSource != null && !transaction.isRetained()) stableSource.delete()
        }
    }

    private fun declaredPLYCount(source: File, cancelled: () -> Boolean): Long {
        source.inputStream().buffered().use { input ->
            val line = StringBuilder(); var bytes = 0; var lineNumber = 0; var vertices: Long? = null; var formatSeen = false
            while (true) {
                val c = input.read(); check(c >= 0 && ++bytes <= (1 shl 20)) { "Invalid or oversized PLY header" }
                if (bytes and 4095 == 0 || c == 10) check(!cancelled()) { "Cancelled" }
                if (c == 10) {
                    val current = line.toString().removeSuffix("\r"); line.setLength(0); lineNumber++
                    if (lineNumber == 1) check(current == "ply") { "Invalid PLY signature" }
                    else if (current.startsWith("format ")) {
                        check(!formatSeen && current in setOf("format ascii 1.0", "format binary_little_endian 1.0", "format binary_big_endian 1.0")) { "Unsupported PLY format" }
                        formatSeen = true
                    } else if (current.startsWith("element vertex")) {
                        val tokens = current.split(Regex("\\s+"))
                        check(vertices == null && tokens.size == 3 && tokens[0] == "element" && tokens[1] == "vertex") { "Invalid PLY vertex declaration" }
                        vertices = tokens[2].toLong().also { check(it >= 0) { "Invalid PLY vertex count" } }
                    } else if (current == "end_header") {
                        check(formatSeen) { "PLY format is missing" }
                        return checkNotNull(vertices) { "PLY has no vertex element" }
                    }
                } else {
                    check(c in 0..127) { "PLY header must be ASCII" }
                    line.append(c.toChar())
                    check(line.length <= 4096) { "PLY header line is too long" }
                }
            }
        }
    }

    private fun isZipSignature(bytes: ByteArray, count: Int): Boolean {
        if (count < 4 || bytes[0] != 0x50.toByte() || bytes[1] != 0x4b.toByte()) return false
        return (bytes[2] == 0x03.toByte() && bytes[3] == 0x04.toByte()) ||
            (bytes[2] == 0x05.toByte() && bytes[3] == 0x06.toByte()) ||
            (bytes[2] == 0x07.toByte() && bytes[3] == 0x08.toByte())
    }
}
