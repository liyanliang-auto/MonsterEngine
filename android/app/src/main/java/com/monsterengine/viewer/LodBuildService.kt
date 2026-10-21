package com.monsterengine.viewer

import android.app.Activity
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** User-initiated dataSync service. It never retains an Activity or Surface.
 * One process worker owns native Run; a small monitor copies/persists progress.
 * Timeout/destroy only signal control, release wakelock and stop foreground;
 * committed journal recovery never depends on an onDestroy checkpoint/join. */
class LodBuildService : Service() {
    private lateinit var notifications: BuildNotifications
    private lateinit var monitor: ScheduledExecutorService
    private val main = Handler(Looper.getMainLooper())
    private var wakeLock: PowerManager.WakeLock? = null
    private var lastWake = 0L
    @Volatile private var destroyed = false

    override fun onCreate() {
        super.onCreate()
        notifications = BuildNotifications(applicationContext)
        notifications.createChannel()
        monitor = Executors.newSingleThreadScheduledExecutor { task -> Thread(task, "MonsterBuildProgress") }
    }
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val id = intent?.getStringExtra(BuildNotifications.EXTRA_JOB_ID).orEmpty()
        if (!id.matches(ID_PATTERN)) { stopSelf(startId); return START_NOT_STICKY }
        // Deadline first. No store access, source hash, copy or JNI Run precedes it.
        try {
            val notification = notifications.notification(id)
            if (Build.VERSION.SDK_INT >= 34) startForeground(BuildNotifications.NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            else startForeground(BuildNotifications.NOTIFICATION_ID, notification)
        } catch (error: RuntimeException) {
            // Includes API31+ start restrictions, API35+ exhausted dataSync quota,
            // and manifest/type/security errors. Never retry/start-loop around it.
            val reason = "foreground_start_rejected: ${error.javaClass.simpleName}"
            WORKER.execute { recordStopped(applicationContext, id, reason, BuildStage.WaitingForResources) }
            stopSelf(startId)
            return START_NOT_STICKY
        }
        val current = active.get()
        val action = intent?.action ?: ACTION_START
        if (current != null) {
            if (current.id.isEmpty()) { finishForeground(startId); return START_NOT_STICKY }
            if (current.id == id && action in setOf(ACTION_PAUSE, ACTION_CANCEL))
                requestStop(current, if (action == ACTION_CANCEL) "cancel" else "pause", "user_$action")
            // A resume during retirement cannot clear a sticky pause/cancel signal.
            notifications.post(current.id, current.progress, false)
            return START_NOT_STICKY
        }
        if (action in setOf(ACTION_PAUSE, ACTION_CANCEL)) {
            WORKER.execute {
                recordStopped(applicationContext, id, "user_$action",
                    if (action == ACTION_CANCEL) BuildStage.Cancelled else BuildStage.Paused)
                main.post { finishForeground(startId) }
            }
            return START_NOT_STICKY
        }
        if (action != ACTION_START && action != ACTION_RESUME) { finishForeground(startId); return START_NOT_STICKY }
        val run = ActiveRun(id)
        if (!active.compareAndSet(null, run)) { finishForeground(startId); return START_NOT_STICKY }
        val app = applicationContext
        try {
            acquireWake()
            run.observer = monitor.scheduleAtFixedRate({ observe(run) }, 0, 1, TimeUnit.SECONDS)
            WORKER.execute {
                try {
                    android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND)
                    execute(app, run)
                } finally {
                    run.observer?.cancel(false)
                    // execute destroys native ownership before this acknowledgement.
                    main.post {
                        if (active.compareAndSet(run, null)) {
                            finishForeground(startId)
                            // Stop the current component too if Android recreated
                            // the Service while this process worker was retiring.
                            app.stopService(Intent(app, LodBuildService::class.java))
                            notifications.post(run.id, run.progress, true)
                        }
                    }
                }
            }
        } catch (error: RuntimeException) {
            run.observer?.cancel(false)
            active.compareAndSet(run, null)
            finishForeground(startId)
            WORKER.execute { recordStopped(app, id, "worker_start_rejected: ${error.javaClass.simpleName}", BuildStage.WaitingForResources) }
        }
        return START_NOT_STICKY
    }

    private fun execute(app: Context, run: ActiveRun) {
        var handle = NativeHandle()
        try {
            val store = BuildJobStore(app)
            var record = store.read(run.id)
            run.config = record.config
            run.jobDirectory = record.jobDirectory
            store.update(run.id) { it.copy(notificationPermissionGranted = notifications.permissionGranted(), stopReason = "") }
            store.verifySource(record) { run.control.get().isNotEmpty() }
            val repository = SceneRepository(app)
            if (record.publicationHash.isNotEmpty()) {
                val published = repository.list().firstOrNull { it.kind == ViewerSceneKind.LOD && it.contentHash == record.publicationHash }
                if (published != null) {
                    run.progress = record.progress.copy(stage = BuildStage.Completed, status = "completed", error = "")
                    store.update(run.id) { it.copy(progress = run.progress, publishedSceneId = published.sceneId) }
                    return
                }
            }
            if (record.publishedSceneId.isNotEmpty()) { run.progress = record.progress; return }
            check(run.control.get().isEmpty()) { "Build stopped before native creation" }
            run.progress = record.progress.copy(stage = BuildStage.Created, status = "awaiting exclusive build admission", error = "")
            store.update(run.id) { it.copy(progress = run.progress) }
            handle = NativeBridge.createBuild(record.stableSource, record.jobDirectory, record.outputDirectory, record.config)
            check(handle.isValid) {
                val failure = NativeBridge.takeBuildCreationFailure()
                "Native CPU builder ${failure.code}: ${failure.message}".take(4096)
            }
            run.handle = handle
            val requested = run.control.get()
            if (requested.isNotEmpty()) NativeBridge.buildControl(handle, requested)
            val result = NativeBridge.runBuild(handle) // Blocking only on this dedicated worker.
            synchronized(run) {
                capture(run, NativeBridge.buildSnapshot(handle), store)
                run.observeNative = false
            }
            if (!result.ok) {
                val stage = when (result.code) { "paused" -> BuildStage.Paused; "cancelled" -> BuildStage.Cancelled
                    "waiting_for_resources" -> BuildStage.WaitingForResources; else -> BuildStage.Failed }
                run.progress = run.progress.copy(stage = stage, status = result.code, error = result.message.take(4096))
                store.update(run.id) { it.copy(progress = run.progress, stopReason = run.reason.get()) }
                return
            }
            if (run.control.get().isNotEmpty()) {
                run.progress = run.progress.copy(stage = if (run.control.get() == "cancel") BuildStage.Cancelled else BuildStage.Paused,
                    status = "native output retained; publication deferred")
                store.update(run.id) { it.copy(progress = run.progress, stopReason = run.reason.get()) }
                return
            }
            // Native builder already validated/published its staging directory.
            // Persist hash intent BEFORE SceneRepository can rename it. Discovery
            // after process death finds an equivalent exact-hash published scene.
            record = store.read(run.id)
            val output = File(record.outputDirectory)
            val hash = SceneRepository.sha256(File(output, "scene.mgs")) { run.control.get().isNotEmpty() }
            run.progress = run.progress.copy(stage = BuildStage.Publishing, status = "publishing scene library entry", error = "")
            store.update(run.id) { it.copy(progress = run.progress, publicationHash = hash) }
            val descriptor = repository.publish(output, ViewerSceneKind.LOD, hash) { run.control.get().isNotEmpty() }
            run.progress = run.progress.copy(stage = BuildStage.Completed, status = "completed", error = "")
            store.update(run.id) { it.copy(progress = run.progress, publicationHash = hash, publishedSceneId = descriptor.sceneId) }
        } catch (error: Exception) {
            val requested = run.control.get()
            run.progress = run.progress.copy(stage = if (requested == "cancel") BuildStage.Cancelled else
                if (requested == "pause") BuildStage.Paused else BuildStage.Failed,
                status = run.reason.get().ifEmpty { "build_failed" }, error = error.message.orEmpty().take(4096))
            try { BuildJobStore(app).update(run.id) { it.copy(progress = run.progress, stopReason = run.reason.get()) } }
            catch (_: Exception) { /* Last durable journal/store remains the recovery authority. */ }
        } finally {
            synchronized(run) { run.observeNative = false }
            run.handle = NativeHandle()
            if (handle.isValid) NativeBridge.destroyBuild(handle) // Same worker; Run has already retired.
            releaseWake()
        }
    }

    private fun observe(run: ActiveRun) {
        if (destroyed || active.get() !== run) return
        try {
            val store = BuildJobStore(applicationContext)
            synchronized(run) {
                if (!run.observeNative) return
                if (run.handle.isValid) capture(run, NativeBridge.buildSnapshot(run.handle), store)
            }
            if (run.control.get().isEmpty()) {
                val power = getSystemService(PowerManager::class.java)
                val battery = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
                val level = battery?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
                val scale = battery?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
                val plugged = (battery?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0
                val reason = when {
                    Build.VERSION.SDK_INT >= 29 && power.currentThermalStatus >= PowerManager.THERMAL_STATUS_SEVERE -> "thermal_severe"
                    !plugged && level >= 0 && scale > 0 && 100L * level / scale < 15 -> "battery_below_15_percent"
                    run.jobDirectory.isNotEmpty() && File(run.jobDirectory).parentFile!!.usableSpace < run.config.diskReserveBytes -> "disk_reserve_low"
                    else -> ""
                }
                if (reason.isNotEmpty()) requestStop(run, "pause", reason)
                else if (SystemClock.elapsedRealtime() - lastWake >= WAKE_RENEW_MILLIS) acquireWake()
            }
            notifications.post(run.id, run.progress, false)
        } catch (_: Exception) {
            requestStop(run, "pause", "progress_or_resource_observation_failed")
        }
    }

    private fun capture(run: ActiveRun, result: NativeResult, store: BuildJobStore) {
        if (!result.ok) return
        val payload = result.payload
        val progress = BuildProgress.fromJson(payload.optJSONObject("progress") ?: payload)
        run.progress = progress
        val hash = payload.optString("builderHash")
        store.update(run.id) { old -> old.copy(progress = progress,
            config = if (hash.matches(Regex("[a-f0-9]{64}"))) old.config.copy(builderHash = hash) else old.config) }
    }

    private fun requestStop(run: ActiveRun, action: String, reason: String) {
        if (action == "cancel") run.control.set("cancel") else run.control.compareAndSet("", "pause")
        run.reason.compareAndSet("", reason)
        val handle = run.handle
        try { if (handle.isValid) NativeBridge.buildControl(handle, run.control.get()) }
        catch (_: RuntimeException) { /* Sticky signal remains; do not block framework retirement. */ }
        finally { releaseWake() }
    }

    @Synchronized private fun acquireWake() {
        if (destroyed) return
        val power = getSystemService(PowerManager::class.java)
        val lock = wakeLock ?: power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MonsterEngine:LODConstruction")
            .also { it.setReferenceCounted(false); wakeLock = it }
        lock.acquire(WAKE_TIMEOUT_MILLIS)
        lastWake = SystemClock.elapsedRealtime()
    }
    @Synchronized private fun releaseWake() {
        try { wakeLock?.let { if (it.isHeld) it.release() } }
        catch (_: RuntimeException) { /* Bounded platform timeout still guarantees eventual release. */ }
    }
    private fun finishForeground(startId: Int) {
        releaseWake()
        try { stopForeground(STOP_FOREGROUND_REMOVE) } finally { stopSelf(startId) }
    }
    /** Android15+ dataSync deadline: no store fsync, checkpoint, future wait or native join here. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        try { active.get()?.let { requestStop(it, "pause", "platform_datasync_timeout") } }
        finally { finishForeground(startId) }
    }
    override fun onTimeout(startId: Int) {
        try { active.get()?.let { requestStop(it, "pause", "platform_service_timeout") } }
        finally { finishForeground(startId) }
    }
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_RUNNING_CRITICAL && level != TRIM_MEMORY_UI_HIDDEN)
            active.get()?.let { requestStop(it, "pause", "memory_pressure") }
    }
    override fun onDestroy() {
        destroyed = true
        active.get()?.let { requestStop(it, "pause", "service_destroyed") }
        monitor.shutdownNow() // Interrupts only lightweight observations, never native Run.
        releaseWake()
        super.onDestroy()
    }

    private class ActiveRun(val id: String) {
        val control = AtomicReference("")
        val reason = AtomicReference("")
        @Volatile var handle = NativeHandle()
        @Volatile var progress = BuildProgress()
        @Volatile var config = BoundedBuildConfig()
        @Volatile var jobDirectory = ""
        @Volatile var observeNative = true
        @Volatile var observer: ScheduledFuture<*>? = null
    }
    companion object {
        const val ACTION_START = "start"
        const val ACTION_PAUSE = "pause"
        const val ACTION_CANCEL = "cancel"
        const val ACTION_RESUME = "resume"
        private const val WAKE_TIMEOUT_MILLIS = 10L * 60 * 1000
        private const val WAKE_RENEW_MILLIS = 5L * 60 * 1000
        private val ID_PATTERN = Regex("[a-f0-9-]{36}")
        private val active = AtomicReference<ActiveRun?>(null)
        private val WORKER = Executors.newSingleThreadExecutor { task -> Thread(task, "MonsterLODBuilder") }
        fun hasActiveJob(): Boolean = active.get() != null

        /** Read-only process observation; a stop request is not a retirement acknowledgement.
         * @property jobId The actual user job holding the worker.
         * @property stopRequested Whether its existing sticky stop signal is set. */
        data class GuiRunState(val jobId: String, val stopRequested: Boolean)

        /** Read existing atomics only; maintenance reservations have no user job. */
        fun guiRunState(): GuiRunState? {
            val run = active.get() ?: return null
            if (run.id.isEmpty()) return null
            return GuiRunState(run.id, run.control.get().isNotEmpty())
        }

        /** Claim maintenance without blocking Activity startup on filesystem I/O.
         * A competing start fails admission until cleanup/discovery releases it. */
        internal fun <T> withRetiredJobAccess(block: () -> T): T {
            val maintenance = ActiveRun("")
            check(active.compareAndSet(null, maintenance)) { "Wait for native build retirement" }
            try { return block() } finally { active.compareAndSet(maintenance, null) }
        }

        /** Call from a visible user interaction after renderer-safe heavy scene release.
         * Platform rejection is returned without automatic retry or quota evasion. */
        fun startFromVisibleActivity(activity: Activity, jobId: String): NativeResult {
            require(jobId.matches(ID_PATTERN))
            if (activity.isFinishing || activity.isDestroyed || !activity.hasWindowFocus())
                return NativeResult(false, "visible_activity_required")
            if (hasActiveJob()) return NativeResult(false, "build_busy", "Wait for current native worker retirement")
            return start(activity, jobId, ACTION_START)
        }
        /** App controls remain available if notification permission is denied.
         * Resume requires visible Activity; notification resume uses a user PendingIntent. */
        fun control(context: Context, jobId: String, action: String): NativeResult {
            require(jobId.matches(ID_PATTERN) && action in setOf(ACTION_PAUSE, ACTION_CANCEL, ACTION_RESUME))
            if (action == ACTION_RESUME) return if (context is Activity) startFromVisibleActivity(context, jobId)
                else NativeResult(false, "visible_activity_required")
            return start(context, jobId, action)
        }
        private fun start(context: Context, jobId: String, action: String): NativeResult = try {
            context.startForegroundService(Intent(context, LodBuildService::class.java).setAction(action)
                .putExtra(BuildNotifications.EXTRA_JOB_ID, jobId))
            NativeResult(true, "dispatched")
        } catch (error: RuntimeException) {
            NativeResult(false, "foreground_start_rejected", error.javaClass.simpleName)
        }
        private fun recordStopped(context: Context, id: String, reason: String, stage: BuildStage) {
            try { BuildJobStore(context).update(id) { it.copy(progress = it.progress.copy(stage = stage,
                status = reason), stopReason = reason) } } catch (_: Exception) { /* Existing durable state is retained. */ }
        }
    }
}
