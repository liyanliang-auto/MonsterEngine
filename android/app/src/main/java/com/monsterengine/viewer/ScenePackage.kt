package com.monsterengine.viewer

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.RandomAccessFile
import java.nio.file.Files
import java.util.Locale
import java.util.UUID
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/** Schema-1 .monsterlod ZIP. ZIP64, multidisk and directory entries are deliberately
 * rejected by the first-release bounded interchange policy; pages remain ordinary files.
 */
class ScenePackage(private val repository: SceneRepository) {
    private data class Entry(val name: String, val size: Long, val crc: Long, val method: Int)

    /** Extract to an isolated private transaction, verify every inventory hash and native page. */
    fun importPackage(archive: File, cancelled: () -> Boolean = { false }, progress: (Long) -> Unit = {}): SceneDescriptor {
        val entries = inspect(archive)
        val staging = repository.createStaging()
        try {
            var actual = 0L
            ZipFile(archive).use { zip ->
                entries.forEach { entry ->
                    check(!cancelled()) { "Cancelled" }
                    val target = File(staging, entry.name)
                    check(target.canonicalPath.startsWith(staging.canonicalPath + File.separator))
                    check(target.parentFile!!.mkdirs() || target.parentFile!!.isDirectory)
                    val zipped = checkNotNull(zip.getEntry(entry.name)); check(zipped.method == entry.method && zipped.size == entry.size)
                    val crc = CRC32(); var size = 0L
                    zip.getInputStream(zipped).use { input -> FileOutputStream(target).use { output ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            check(!cancelled()) { "Cancelled" }
                            val count = input.read(buffer); if (count < 0) break; check(count > 0)
                            size = Math.addExact(size, count.toLong()); actual = Math.addExact(actual, count.toLong())
                            check(size <= entry.size && size <= repository.limits.maxEntryBytes && actual <= repository.limits.maxExpandedBytes) { "Package expansion limit exceeded" }
                            check(staging.usableSpace >= repository.limits.diskReserveBytes + count) { "Insufficient extraction disk space" }
                            output.write(buffer, 0, count); crc.update(buffer, 0, count); progress(actual)
                        }
                        output.fd.sync()
                    } }
                    check(size == entry.size && crc.value == entry.crc) { "Package size/CRC mismatch" }
                }
            }
            val descriptorFile = File(staging, "package.json")
            check(descriptorFile.length() in 1..repository.limits.maxMetadataBytes)
            val descriptor = JSONObject(descriptorFile.readText())
            check(descriptor.getInt("schemaVersion") == 1 && descriptor.getString("format") == "monsterlod" && descriptor.getString("manifest") == "scene.mgs")
            val inventory = descriptor.getJSONArray("files")
            check(inventory.length() == entries.size - 1)
            val seen = mutableSetOf<String>()
            for (index in 0 until inventory.length()) {
                val item = inventory.getJSONObject(index); val name = safeName(item.getString("path"))
                check(name != "package.json" && seen.add(name)) { "Duplicate package inventory path" }
                val file = File(staging, name); val expected = item.getString("sha256")
                check(expected.matches(Regex("[a-f0-9]{64}")) && file.isFile && file.length() == item.getLong("bytes"))
                check(SceneRepository.sha256(file, cancelled) == expected) { "Package hash mismatch" }
            }
            check(seen == entries.map { it.name }.filter { it != "package.json" }.toSet() && "scene.mgs" in seen)
            val manifestHash = SceneRepository.sha256(File(staging, "scene.mgs"), cancelled)
            // New exports carry the source descriptor identity. Older schema-1 packages
            // remain importable, but any declared identity must match the verified payload.
            val declaredContentHash = descriptor.optString("contentHash", manifestHash)
            check(declaredContentHash.matches(Regex("[a-f0-9]{64}")) && declaredContentHash == manifestHash) {
                "Package descriptor identity mismatch"
            }
            descriptor.optString("sceneId", "").takeIf { it.isNotEmpty() }?.let {
                check(it.matches(Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))) {
                    "Invalid source scene identity"
                }
            }
            val sourceSceneId = descriptor.optString("sceneId", "")
            val sourceGeneration = descriptor.optLong("generation", 1L)
            check(sourceGeneration > 0) { "Invalid source scene generation" }
            if (sourceSceneId.isNotEmpty()) {
                val provenance = JSONObject().put("schemaVersion", 1).put("sceneId", sourceSceneId)
                    .put("generation", sourceGeneration).put("contentHash", manifestHash)
                SceneRepository.writeDurable(File(staging, PROVENANCE_FILE), provenance.toString().toByteArray(Charsets.UTF_8))
            }
            return repository.publish(staging, ViewerSceneKind.LOD, manifestHash, cancelled)
        } catch (error: Exception) { repository.discard(staging); throw error }
    }

    /** Export a leased native scene; returned file is under files/exports for FileProvider. */
    fun exportPackage(sceneId: String, cancelled: () -> Boolean = { false }): File {
        repository.acquire(sceneId).use { lease ->
            check(lease.descriptor.kind == ViewerSceneKind.LOD) { "Build a LOD scene before package export" }
            val directory = File(lease.descriptor.directory)
            val validation = NativeBridge.validateScene(directory.absolutePath); check(validation.ok) { validation.message }
            val files = mutableListOf<Pair<String, File>>(); var metadata = 0L; var expanded = 0L
            Files.walk(directory.toPath()).use { paths -> paths.forEach { path ->
                check(!Files.isSymbolicLink(path)) { "Links cannot be exported" }
                if (Files.isRegularFile(path)) {
                    val name = directory.toPath().relativize(path).toString().replace(File.separatorChar, '/')
                    if (name != "descriptor.json" && name != "delete-request.json" && name != "package.json" && name != PROVENANCE_FILE) {
                        safeName(name); metadata = Math.addExact(metadata, 512L + name.length * 4L)
                        check(metadata <= repository.limits.maxMetadataBytes && files.size < minOf(65533, repository.limits.maxEntries - 1))
                        val file = path.toFile(); check(file.length() <= repository.limits.maxEntryBytes)
                        expanded = Math.addExact(expanded, file.length()); check(expanded <= repository.limits.maxExpandedBytes)
                        files.add(name to file)
                    }
                }
            } }
            val manifestFile = files.firstOrNull { it.first == "scene.mgs" }?.second
            check(manifestFile != null) { "Scene manifest missing" }
            val manifestHash = SceneRepository.sha256(manifestFile, cancelled)
            check(manifestHash == lease.descriptor.contentHash) { "Scene descriptor identity mismatch" }
            val provenanceFile = File(directory, PROVENANCE_FILE)
            val provenance = if (provenanceFile.exists()) {
                check(provenanceFile.isFile && !Files.isSymbolicLink(provenanceFile.toPath()) && provenanceFile.length() in 1..4096)
                JSONObject(provenanceFile.readText()).also { source ->
                    check(source.getInt("schemaVersion") == 1 && source.getString("contentHash") == manifestHash)
                    check(source.getString("sceneId").matches(Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")))
                    check(source.getLong("generation") > 0)
                }
            } else JSONObject().put("sceneId", lease.descriptor.sceneId).put("generation", lease.descriptor.generation)
            val inventory = JSONArray()
            files.sortedBy { it.first }.forEach { (name, file) -> inventory.put(JSONObject().put("path", name).put("bytes", file.length()).put("sha256", SceneRepository.sha256(file, cancelled))) }
            val descriptor = JSONObject().put("schemaVersion", 1).put("format", "monsterlod").put("manifest", "scene.mgs")
                .put("contentHash", manifestHash).put("sceneId", provenance.getString("sceneId"))
                .put("generation", provenance.getLong("generation")).put("files", inventory)
                .toString().toByteArray(Charsets.UTF_8)
            check(descriptor.size.toLong() <= repository.limits.maxMetadataBytes)
            // Import counts package.json in the expanded archive budget as well as scene files.
            check(Math.addExact(expanded, descriptor.size.toLong()) <= repository.limits.maxExpandedBytes) { "Package expansion limit exceeded" }
            val exports = File(repository.root.parentFile, "exports"); check(exports.mkdirs() || exports.isDirectory)
            val destination = File(exports, UUID.randomUUID().toString() + ".monsterlod")
            val temporary = File(exports, destination.name + ".part")
            try {
                FileOutputStream(temporary).use { raw -> ZipOutputStream(raw).use { zip ->
                    zip.putNextEntry(ZipEntry("package.json")); zip.write(descriptor); zip.closeEntry()
                    files.sortedBy { it.first }.forEach { (name, file) ->
                        zip.putNextEntry(ZipEntry(name)); file.inputStream().use { input -> val buffer = ByteArray(65536)
                            while (true) { check(!cancelled()) { "Cancelled" }; val count = input.read(buffer); if (count < 0) break; check(count > 0)
                                check(exports.usableSpace >= repository.limits.diskReserveBytes + count)
                                zip.write(buffer, 0, count); check(temporary.length() <= repository.limits.maxArchiveBytes) }
                        }; zip.closeEntry()
                    }; zip.finish(); raw.fd.sync()
                } }
                check(temporary.length() <= repository.limits.maxArchiveBytes)
                // Enforce identical supported ZIP policy on our writer's output.
                inspect(temporary)
                android.system.Os.rename(temporary.absolutePath, destination.absolutePath); SceneRepository.sync(exports)
                return destination
            } catch (error: Exception) { temporary.delete(); throw error }
        }
    }

    private fun inspect(archive: File): List<Entry> {
        check(archive.length() in 22..repository.limits.maxArchiveBytes)
        RandomAccessFile(archive, "r").use { input ->
            fun u16(): Int = input.readUnsignedByte() or (input.readUnsignedByte() shl 8)
            fun u32(): Long = u16().toLong() or (u16().toLong() shl 16)
            val start = maxOf(0, input.length() - 65557); val tail = ByteArray((input.length() - start).toInt())
            input.seek(start); input.readFully(tail)
            var end = -1
            for (at in tail.size - 22 downTo 0) {
                if (tail[at] == 0x50.toByte() && tail[at+1] == 0x4b.toByte() && tail[at+2] == 5.toByte() && tail[at+3] == 6.toByte()) {
                    val comment = (tail[at+20].toInt() and 255) or ((tail[at+21].toInt() and 255) shl 8)
                    if (at + 22 + comment == tail.size) { end = at; break }
                }
            }
            check(end >= 0) { "Missing ZIP end record" }; input.seek(start + end + 4)
            check(u16() == 0 && u16() == 0) { "Multidisk ZIP unsupported" }
            val diskCount = u16(); val count = u16(); val bytes = u32(); val offset = u32()
            check(count == diskCount && count in 1..minOf(65534, repository.limits.maxEntries)) { "ZIP64 or entry limit unsupported" }
            check(bytes <= repository.limits.maxMetadataBytes && offset + bytes == start + end) { "Invalid ZIP central directory" }
            val entries = ArrayList<Entry>(); val seen = mutableSetOf<String>(); var metadata = 0L; var expanded = 0L
            input.seek(offset)
            repeat(count) {
                check(u32() == 0x02014b50L); u16(); val version = u16(); val flags = u16(); val method = u16(); u16(); u16()
                val crc = u32(); val compressed = u32(); val size = u32(); val nameBytes = u16(); val extra = u16(); val comment = u16()
                check(u16() == 0); u16(); val attributes = u32(); val local = u32()
                check(version <= 20 && flags and 0xf7f1 == 0 && method in listOf(ZipEntry.STORED, ZipEntry.DEFLATED)) { "Encrypted/unsupported ZIP" }
                val mode = ((attributes shr 16) and 0xf000).toInt()
                check(mode == 0 || mode == 0x8000) { "ZIP links/special files forbidden" }; check(attributes and 0x10L == 0L)
                check(nameBytes in 1..4096 && size <= repository.limits.maxEntryBytes && compressed != 0xffffffffL && size != 0xffffffffL && local < offset)
                metadata = Math.addExact(metadata, 512L + nameBytes * 4L); check(metadata <= repository.limits.maxMetadataBytes)
                val nameData = ByteArray(nameBytes); input.readFully(nameData)
                check(nameData.all { it.toInt() in 32..126 }) { "Package paths must be portable ASCII" }
                val name = safeName(nameData.toString(Charsets.US_ASCII)); check(seen.add(name.lowercase(Locale.ROOT))) { "Duplicate normalized ZIP entry" }
                check(name.lowercase(Locale.ROOT) !in setOf("descriptor.json", "delete-request.json")) { "Reserved scene-store entry" }
                val next = input.filePointer + extra + comment; check(next <= offset + bytes)
                input.seek(local); check(u32() == 0x04034b50L); val localVersion = u16(); val localFlags = u16(); val localMethod = u16()
                check(localVersion <= 20 && localFlags == flags && localMethod == method); input.skipBytes(16)
                val localNameSize = u16(); val localExtra = u16(); check(localNameSize == nameBytes)
                val localName = ByteArray(localNameSize); input.readFully(localName); check(localName.contentEquals(nameData))
                check(input.filePointer + localExtra + compressed <= offset) { "ZIP payload exceeds data region" }
                input.seek(next); expanded = Math.addExact(expanded, size); check(expanded <= repository.limits.maxExpandedBytes)
                entries.add(Entry(name, size, crc, method))
            }
            check(input.filePointer == offset + bytes && entries.any { it.name == "package.json" } && entries.any { it.name == "scene.mgs" })
            return entries
        }
    }

    companion object {
        private const val PROVENANCE_FILE = "package-origin.json"

        internal fun safeName(name: String): String {
            require(name.length in 1..4096 && name.all { it.code in 32..126 } && !name.contains('\\') && !name.contains(':') && !name.startsWith('/') && !name.endsWith('/'))
            require(name.split('/').all { it.isNotEmpty() && it != "." && it != ".." && !it.endsWith('.') && !it.endsWith(' ') })
            return name
        }
    }
}
