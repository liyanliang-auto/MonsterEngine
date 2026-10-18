package com.monsterengine.viewer

import android.content.Context
import android.util.AtomicFile
import org.json.JSONObject
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.UUID

/** Small durable observation; never contains a process-specific native handle.
 * Source/job/output paths stay private. A paused/cancelled job retains committed
 * journal artifacts until explicit worker cleanup after ownership retirement.
 * @property id Opaque durable job UUID.
 * @property stableSource Private immutable import path.
 * @property sourceSHA256 Exact import identity.
 * @property sourceBytes Exact stable source size.
 * @property jobDirectory Private native checkpoint directory.
 * @property outputDirectory Reserved private staging destination.
 * @property config Frozen typed policy and measured expected binary identity.
 * @property progress Last observed actual native or publication progress.
 * @property policyHash Canonical typed policy identity excluding measured binary hash.
 * @property publicationHash Durable manifest publication intent.
 * @property publishedSceneId Resulting scene library UUID, empty until observed.
 * @property stopReason Last explicit user/platform/resource stop.
 * @property notificationPermissionGranted Observed notification visibility.
 * @property updatedAtMillis Last durable metadata update wall time. */
data class BuildJobRecord(
    val id: String, val stableSource: String, val sourceSHA256: String, val sourceBytes: Long,
    val jobDirectory: String, val outputDirectory: String, val config: BoundedBuildConfig,
    val progress: BuildProgress = BuildProgress(), val policyHash: String,
    val publicationHash: String = "", val publishedSceneId: String = "",
    val stopReason: String = "", val notificationPermissionGranted: Boolean = true,
    val updatedAtMillis: Long = System.currentTimeMillis()
) {
    fun toJson(): JSONObject = JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
        .put("id", id).put("stableSource", stableSource).put("sourceSHA256", sourceSHA256)
        .put("sourceBytes", sourceBytes).put("jobDirectory", jobDirectory).put("outputDirectory", outputDirectory)
        .put("config", config.toJson()).put("progress", progress.toJson()).put("policyHash", policyHash)
        .put("publicationHash", publicationHash).put("publishedSceneId", publishedSceneId)
        .put("stopReason", stopReason).put("notificationPermissionGranted", notificationPermissionGranted)
        .put("updatedAtMillis", updatedAtMillis)

    companion object {
        fun fromJson(value: JSONObject): BuildJobRecord {
            require(value.getInt("schemaVersion") == VIEWER_SCHEMA_VERSION)
            return BuildJobRecord(value.getString("id"), value.getString("stableSource"),
                value.getString("sourceSHA256"), value.getLong("sourceBytes"), value.getString("jobDirectory"),
                value.getString("outputDirectory"), BoundedBuildConfig.fromJson(value.getJSONObject("config")),
                BuildProgress.fromJson(value.getJSONObject("progress")), value.getString("policyHash"),
                value.optString("publicationHash"), value.optString("publishedSceneId"), value.optString("stopReason"),
                value.optBoolean("notificationPermissionGranted", true), value.getLong("updatedAtMillis"))
        }
    }
}

/** Bounded atomic job metadata. All methods perform filesystem I/O: use a worker.
 * Job creation consumes an already copied ImportOutcome.BuildRequired source.
 * No boot receiver, implicit restart or automatic incompatible checkpoint adoption. */
class BuildJobStore(context: Context) {
    private val app = context.applicationContext
    private val repository = SceneRepository(app)
    private val root = File(app.filesDir, "build-jobs")
    init { check(!Files.isSymbolicLink(root.toPath()) && (root.mkdirs() || root.isDirectory)) }

    fun create(stableSource: File, sourceSHA256: String, sourceBytes: Long,
               config: BoundedBuildConfig = BoundedBuildConfig()): BuildJobRecord = synchronized(lock) {
        require(sourceSHA256.matches(hashPattern) && sourceBytes > 0 && sourceBytes <= config.maxSourceBytes)
        require(stableSource.canonicalFile.parentFile == repository.buildInputs.canonicalFile)
        check(stableSource.isFile && !Files.isSymbolicLink(stableSource.toPath()) && stableSource.length() == sourceBytes)
        check(list().size < MAX_JOBS) { "Retained build job limit reached; clean up a retired job first" }
        val directory = File(root, UUID.randomUUID().toString()).also { check(it.mkdir()) }
        var output: File? = null
        try {
            val reserved = repository.createStaging().also { output = it }
            // The builder publishes a NEW directory, so retain only its reserved name.
            Files.delete(reserved.toPath())
            SceneRepository.sync(reserved.parentFile!!)
            val record = BuildJobRecord(directory.name, stableSource.canonicalPath, sourceSHA256, sourceBytes,
                File(directory, "journal").absolutePath, reserved.absolutePath, config, policyHash = policyHash(config))
            write(record)
            SceneRepository.sync(root)
            record
        } catch (error: Exception) {
            // Only this not-yet-exposed empty reservation/metadata is owned here.
            try { output?.let { if (it.exists()) Files.delete(it.toPath()) }; SceneRepository.removeTree(directory) }
            catch (cleanup: Exception) { error.addSuppressed(cleanup) }
            throw error
        }
    }

    fun read(id: String): BuildJobRecord = synchronized(lock) {
        val target = File(directory(id), "job.json")
        check(!Files.isSymbolicLink(target.toPath()) && !Files.isSymbolicLink(File(target.path + ".bak").toPath()))
        val file = AtomicFile(target)
        val record = file.openRead().use { input ->
            val buffer = ByteArray(MAX_METADATA_BYTES + 1)
            var used = 0
            while (used < buffer.size) { val count = input.read(buffer, used, buffer.size - used)
                if (count < 0) break; check(count > 0); used += count }
            check(used in 1..MAX_METADATA_BYTES) { "Build metadata limit exceeded" }
            BuildJobRecord.fromJson(JSONObject(String(buffer, 0, used, Charsets.UTF_8)))
        }
        validate(record)
        record
    }

    fun list(): List<BuildJobRecord> = synchronized(lock) {
        val entries = root.listFiles()?.filter { it.isDirectory && it.name.matches(idPattern) } ?: emptyList()
        check(entries.size <= MAX_JOBS) { "Retained build job limit exceeded" }
        entries.filter { File(it, "job.json").exists() || File(it, "job.json.bak").exists() }
            .map { read(it.name) }.sortedByDescending { it.updatedAtMillis }
    }

    fun update(id: String, transform: (BuildJobRecord) -> BuildJobRecord): BuildJobRecord = synchronized(lock) {
        val old = read(id)
        val next = transform(old).copy(updatedAtMillis = System.currentTimeMillis())
        require(next.id == old.id && next.stableSource == old.stableSource && next.sourceSHA256 == old.sourceSHA256 &&
            next.sourceBytes == old.sourceBytes && next.jobDirectory == old.jobDirectory &&
            next.outputDirectory == old.outputDirectory && next.policyHash == old.policyHash)
        validate(next)
        write(next)
        next
    }

    /** Call only when no process build worker owns a job. The last native journal
     * checkpoint is authoritative; these UI observations merely offer explicit resume. */
    fun discoverInterrupted(): List<BuildJobRecord> = LodBuildService.withRetiredJobAccess { synchronized(lock) {
        list().map { record ->
            if (record.progress.stage in ACTIVE_STAGES) update(record.id) {
                it.copy(progress = it.progress.copy(stage = BuildStage.Paused, status = "interrupted; explicit resume available"),
                    stopReason = "process_interrupted")
            } else record
        }
    } }

    /** Source compatibility check is cooperative and never runs on Activity/Surface threads. */
    fun verifySource(record: BuildJobRecord, cancelled: () -> Boolean = { false }) {
        validate(record)
        val source = File(record.stableSource)
        check(source.isFile && source.length() == record.sourceBytes) { "Retained source size changed" }
        check(SceneRepository.sha256(source, cancelled) == record.sourceSHA256) { "Retained source identity changed" }
    }

    /** Explicit destructive cleanup, only after service/native ownership retires.
     * No source shared by another retained job can be deleted. */
    fun cleanupRetired(id: String, removeSource: Boolean = false) = LodBuildService.withRetiredJobAccess { synchronized(lock) {
        val record = read(id)
        require(record.progress.stage !in ACTIVE_STAGES)
        val output = File(record.outputDirectory)
        if (output.exists()) repository.discard(output)
        if (removeSource) {
            check(list().none { it.id != id && it.stableSource == record.stableSource }) { "Another job retains this source" }
            repository.discardBuildInput(File(record.stableSource))
        }
        SceneRepository.removeTree(directory(id))
        SceneRepository.sync(root)
    } }

    private fun directory(id: String): File {
        require(id.matches(idPattern))
        return File(root, id).also { check(!Files.isSymbolicLink(it.toPath()) && it.canonicalFile.parentFile == root.canonicalFile) }
    }
    private fun validate(record: BuildJobRecord) {
        require(record.id.matches(idPattern) && record.sourceSHA256.matches(hashPattern) && record.sourceBytes > 0)
        require(record.sourceBytes <= record.config.maxSourceBytes && record.policyHash == policyHash(record.config))
        require(record.publicationHash.isEmpty() || record.publicationHash.matches(hashPattern))
        require(record.publishedSceneId.isEmpty() || record.publishedSceneId.matches(idPattern))
        require(record.stopReason.length <= 4096 && record.updatedAtMillis >= 0)
        val source = File(record.stableSource)
        require(!Files.isSymbolicLink(source.toPath()) && source.canonicalFile.parentFile == repository.buildInputs.canonicalFile)
        val journal = File(record.jobDirectory)
        require(!Files.isSymbolicLink(journal.toPath()) && journal.canonicalFile == File(directory(record.id), "journal").canonicalFile &&
            journal.canonicalFile.parentFile == directory(record.id).canonicalFile)
        val output = File(record.outputDirectory)
        require(output.name.matches(idPattern) && !Files.isSymbolicLink(output.toPath()) &&
            output.canonicalFile.parentFile == File(repository.root, "staging").canonicalFile)
    }
    private fun write(record: BuildJobRecord) {
        validate(record)
        val bytes = record.toJson().toString().toByteArray(Charsets.UTF_8)
        check(bytes.size <= MAX_METADATA_BYTES)
        val directory = directory(record.id)
        val target = AtomicFile(File(directory, "job.json"))
        val output = target.startWrite()
        try { output.write(bytes); target.finishWrite(output); SceneRepository.sync(directory) }
        catch (error: Exception) { target.failWrite(output); throw error }
    }

    companion object {
        private val lock = Any()
        private const val MAX_JOBS = 64
        private const val MAX_METADATA_BYTES = 65536
        private val idPattern = Regex("[a-f0-9-]{36}")
        private val hashPattern = Regex("[a-f0-9]{64}")
        private val ACTIVE_STAGES = setOf(BuildStage.Created, BuildStage.Copying, BuildStage.Normalizing,
            BuildStage.Partitioning, BuildStage.BuildingLeaves, BuildStage.Reducing, BuildStage.Validating, BuildStage.Publishing)
        /** Empty expected binary hash until worker measures it; all policy values remain frozen. */
        fun policyHash(config: BoundedBuildConfig): String = MessageDigest.getInstance("SHA-256")
            .digest(config.copy(builderHash = "").toJson().toString().toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it.toInt() and 255) }
    }
}
