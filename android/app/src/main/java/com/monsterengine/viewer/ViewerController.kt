package com.monsterengine.viewer

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.Surface
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** Activity-independent render/library owner. Disk, import, native creation and
 * exports use the process-owned serial worker. Old close/retirement precedes a
 * recreated Activity's initialization; main-thread callbacks never wait for it. */
class ViewerController(context: Context) : Choreographer.FrameCallback {
    data class State(val message: String = "正在校验场景资源，请稍候", val busy: Boolean = true,
        val scenes: List<SceneDescriptor> = emptyList(), val builtin: SceneDescriptor? = null,
        val jobs: List<BuildJobRecord> = emptyList(), val requested: ViewerConfig = ViewerConfig(),
        val effective: ViewerConfig? = null, val selectedSceneId: String = "",
        val nativeAvailable: Boolean = false, val detachPending: Boolean = false,
        val buildBlocked: Boolean = false, val pendingImport: ImportOutcome.BuildRequired? = null,
        val diagnostics: JSONObject = NativeBridge.partialDiagnostics(), val observedFps: Double? = null,
        val presentation: ViewerPresentation = ViewerPresentation.INITIALIZING,
        val retryAvailable: Boolean = false, val importInProgress: Boolean = false)
    private data class Drawable(val surface: Surface, val generation: Long, val width: Int, val height: Int,
        val displayWidth: Int, val displayHeight: Int)
    private data class PrivateFailure(val operation: String, val code: String,
        val cause: Throwable? = null, val detail: String = "")
    private val app = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val worker = viewerWorker
    private val surfaceIds = AtomicLong(0)
    private val sceneIds = AtomicLong(0)
    private val importCancelled = AtomicBoolean(false)
    private val pollQueued = AtomicBoolean(false)
    private val initializing = AtomicBoolean(false)
    private val fpsSampler = ObservedFpsSampler() // main-thread only
    private var presentationSample: ObservedFpsSample? = null
    private var presentationObservedAt = 0L
    private val freshnessWatch = object : Runnable {
        override fun run() {
            if (closed || !resumed) return
            if (state.presentation == ViewerPresentation.READY &&
                SystemClock.elapsedRealtime() - presentationObservedAt >= FPS_FRESHNESS_MILLIS) {
                presentationSample = null
                update(state.copy(presentation=ViewerPresentation.PREPARING, effective=null,
                    retryAvailable=true, message="画面更新尚未确认；可等待、重试或导出报告"))
            }
            main.postDelayed(this, 1000)
        }
    }
    private var observationEpoch = 0L
    private var lastObservationAt = SystemClock.elapsedRealtime()
    private var pollScheduled = false
    private val pollTask = Runnable { pollScheduled=false;poll();schedulePoll() }
    private var handle = NativeHandle()
    private val nativeOwner = AtomicReference(NativeHandle())
    private var drawable: Drawable? = null
    private var attachedGeneration = 0L
    private var pendingDetach = 0L
    private var pendingAttachGeneration = 0L
    @Volatile private var attachRequestedGeneration = 0L
    @Volatile private var attachToken = 0L
    private val surfaceRetirements = LinkedHashMap<Long, () -> Unit>()
    private var attachCallback: Choreographer.FrameCallback? = null
    private var stallWatch: java.util.concurrent.ScheduledFuture<*>? = null
    private var resumed = false
    private var framePosted = false
    private var openAccepted = false
    private var screenshotRequestSequence = 0L
    private var initialized = false
    private var initWatchdog: Runnable? = null
    private var detachAttempts = 0
    @Volatile private var closed = false
    @Volatile private var mainHeartbeat = SystemClock.elapsedRealtime()
    @Volatile private var attachPendingSince = 0L
    @Volatile private var lastStallReport = 0L
    private lateinit var repository: SceneRepository
    private lateinit var jobStore: BuildJobStore
    private lateinit var settingsStore: ViewerSettingsStore
    private var sceneLease: SceneLease? = null // worker ownership only
    @Volatile private var lastImportFailure: PrivateFailure? = null
    @Volatile private var lastOpenFailure: PrivateFailure? = null
    @Volatile var state = State()
        private set
    var onState: ((State) -> Unit)? = null

    fun initialize() {
        if(closed||initialized||!initializing.compareAndSet(false,true))return
        val watchdog=Runnable {
            if(closed||initialized||!initializing.get())return@Runnable
            // Bounded visibility: the worker keeps completing the real creation,
            // but the user can export diagnostics or retry instead of an endless
            // "validating scene resources" state.
            update(state.copy(busy=false,nativeAvailable=handle.isValid,
                presentation=ViewerPresentation.FAILED,retryAvailable=true,message="初始化耗时较长（已超过 ${INIT_WATCHDOG_MILLIS/1000} 秒）；可导出报告或稍后重试"))
        }
        initWatchdog=watchdog
        main.postDelayed(watchdog,INIT_WATCHDOG_MILLIS)
        worker.execute {
            if(closed)return@execute
            try {
                repository=SceneRepository(app);jobStore=BuildJobStore(app);settingsStore=ViewerSettingsStore(app)
                val saved = settingsStore.load() // Unknown/malformed originals must remain untouched.
                if (!LodBuildService.hasActiveJob()) jobStore.discoverInterrupted()
                val previous=nativeOwner.get()
                val blockedRetirement=retirementBlocker!=null
                val created = if(previous.isValid)previous else if(blockedRetirement)NativeHandle() else NativeBridge.create(app.assets, app.filesDir.absolutePath, saved.requested).also { nativeOwner.set(it) }
                val creationFailure=if(created.isValid)null else if(blockedRetirement)NativeResult(false,"previous_owner_retirement_failed","Previous owner resources remain protected") else NativeBridge.takeRendererCreationFailure()
                if(creationFailure!=null){
                    runCatching { File(app.filesDir,"viewer-initialization-error.json").writeText(creationFailure.toJson().put("elapsedRealtimeMillis",SystemClock.elapsedRealtime()).toString()) }
                    recordOwnerEvent("creation_failed",creationFailure)
                }else recordOwnerEvent("created",NativeBridge.snapshot(created,request("budget")))
                val builtinResult = if (created.isValid) NativeBridge.snapshot(created, request("builtinScene")) else NativeResult()
                val builtin = builtinResult.payload.optJSONObject("descriptor")?.let { SceneDescriptor.fromJson(it) }
                val scenes=repository.list();val jobs=jobStore.list()
                // Host snapshot only: no extra Vulkan instance on this worker.
                val diagnostics=if(created.isValid)NativeBridge.snapshot(created,request("diagnostics")).payload else NativeBridge.partialDiagnostics()
                main.post {
                    initializing.set(false)
                    main.removeCallbacks(watchdog);initWatchdog=null
                    if (closed) return@post
                    initialized=created.isValid
                    handle=created
                    update(state.copy(busy=false,requested=saved.requested,effective=null,selectedSceneId=saved.selectedSceneId,scenes=scenes,jobs=jobs,builtin=builtin,
                        nativeAvailable=created.isValid,diagnostics=diagnostics,
                        presentation=if(created.isValid)ViewerPresentation.PREPARING else ViewerPresentation.FAILED,retryAvailable=!created.isValid,
                        message=if(created.isValid)"请选择场景，或打开内置示例" else if(blockedRetirement)"旧画面未安全释放；请关闭并重新启动应用，原场景与设置已保留" else "渲染初始化失败；已保留场景与设置，可重试或导出报告"))
                    attachCurrent();NativeBridge.resume(handle,resumed)
                    if(created.isValid&&!LodBuildService.hasActiveJob()) {
                        val preferred = saved.selectedSceneId
                        // Let the UI frame and the Surface/device handoff complete
                        // before the first scene request; concurrent Vulkan startup
                        // and HWUI queueing stalls vendor gralloc on this tablet.
                        main.postDelayed({
                            if(closed||!initialized||handle!=created)return@postDelayed
                            when {
                                preferred.isNotEmpty() && preferred == builtin?.sceneId -> openBuiltin()
                                preferred.isNotEmpty() && scenes.any { it.sceneId == preferred } -> openScene(preferred)
                                preferred.isEmpty() && builtin != null -> openBuiltin()
                                preferred.isNotEmpty() -> update(state.copy(message="已保存的场景暂不可用；原选择已保留，请从场景库手动选择"))
                            }
                        },INITIAL_SCENE_DELAY_MILLIS)
                    }
                    schedulePoll()
                }
            } catch (_: Exception) {
                main.post {
                    initializing.set(false);main.removeCallbacks(watchdog);initWatchdog=null
                    handle=nativeOwner.get();update(state.copy(busy=false,nativeAvailable=handle.isValid,presentation=ViewerPresentation.FAILED,retryAvailable=true,message="初始化未完成；已保留原设置和场景，请检查设置兼容性或导出报告"))
                }
            }
        }
    }
    /** Bounded private last-event evidence; never included in public exports. */
    private fun recordOwnerEvent(phase: String,result: NativeResult) {
        runCatching {
            val record=JSONObject().put("schemaVersion",1).put("phase",phase)
                .put("elapsedRealtimeMillis",SystemClock.elapsedRealtime()).put("result",result.toJson())
                .put("previousRetirement",lastRetirement?.toJson()?:JSONObject.NULL)
            File(app.filesDir,"viewer-owner-lifecycle.json").writeText(record.toString())
        }
    }
    private fun update(next: State) { mainHeartbeat=SystemClock.elapsedRealtime();if(!closed){state=next;onState?.invoke(next)} }
    private fun working(message: String): Boolean {
        if(closed||state.busy)return false
        update(state.copy(busy=true,message=message));return true
    }
    private fun fail(message: String) { main.post { update(state.copy(busy=false,message=message));scheduleFrames() } }
    private fun importFailureCode(error: Throwable): String = when {
        importCancelled.get() -> "cancelled"
        error is SecurityException -> "access_denied"
        error is java.io.IOException -> "document_io"
        error is IllegalArgumentException -> "invalid_document"
        error is ArithmeticException -> "size_overflow"
        else -> "import_failed"
    }
    fun newSurfaceGeneration(): Long = surfaceIds.incrementAndGet()
    fun attachSurface(surface: Surface,generation: Long,width: Int,height: Int,
                      displayWidth: Int = width,displayHeight: Int = height) {
        if(drawable?.generation!=generation)resetFps()
        cancelPendingAttach();drawable=Drawable(surface,generation,width,height,displayWidth,displayHeight);attachCurrent()
    }
    /** The viewer worker queues native producer ownership. Texture composition
     * keeps Activity drawing independent of first-buffer success or admission. */
    private fun attachCurrent() {
        val target=drawable?:return
        if(!handle.isValid||pendingDetach!=0L||!target.surface.isValid)return
        if(target.generation==attachedGeneration||target.generation==pendingAttachGeneration)return
        pendingAttachGeneration=target.generation
        attachPendingSince=SystemClock.elapsedRealtime();armStallWatch()
        val token=++attachToken
        val owner=handle
        worker.execute {
            if(closed||token!=attachToken||pendingDetach!=0L||!target.surface.isValid)return@execute
            attachRequestedGeneration=target.generation
            val result=NativeBridge.attach(owner,target.surface,target.generation,target.width,target.height,
                target.displayWidth,target.displayHeight)
            main.post {
                if(closed||token!=attachToken||pendingAttachGeneration!=target.generation)return@post
                pendingAttachGeneration=0
                if(result.ok){attachedGeneration=target.generation;attachPendingSince=SystemClock.elapsedRealtime();armStallWatch();scheduleFrames();observeSurface(owner,target.generation)}
                else {attachRequestedGeneration=0;resetFps();update(state.copy(presentation=ViewerPresentation.FAILED,retryAvailable=true,message="画面暂不可用，可点击重试"))}
            }
        }
    }
    /** Bounded startup observations complement the low-frequency GUI status poll. */
    private fun observeSurface(owner: NativeHandle, generation: Long, attempt: Int = 0) {
        if(closed||!resumed||owner!=handle||generation!=attachedGeneration)return
        val epoch=observationEpoch;val requestedAt=SystemClock.elapsedRealtime()
        worker.execute {
            val result=NativeBridge.snapshot(owner,request("diagnostics"))
            main.post {
                if(closed||!resumed||owner!=handle||generation!=attachedGeneration||epoch!=observationEpoch||
                    SystemClock.elapsedRealtime()-requestedAt>=FPS_FRESHNESS_MILLIS)return@post
                val ready = observePresentation(result)
                if (!ready && attempt < STARTUP_OBSERVATION_ATTEMPTS && state.presentation != ViewerPresentation.FAILED)
                    main.postDelayed({observeSurface(owner,generation,attempt+1)},500)

            }
        }
    }
    private fun cancelPendingAttach() {
        attachToken++;pendingAttachGeneration=0;attachPendingSince=0
        attachCallback?.let{Choreographer.getInstance().removeFrameCallback(it)};attachCallback=null
    }
    /** Bounded private evidence if the UI thread stops while the WSI is handed
     * over. Runs off the main thread, so a blocked UI cannot suppress the record. */
    private fun armStallWatch() {
        stallWatch?.cancel(false)
        stallWatch=stallWatcher.scheduleWithFixedDelay({
            if(!closed&&attachPendingSince!=0L){
                val now=SystemClock.elapsedRealtime()
                if(now-mainHeartbeat>=UI_STALL_MILLIS&&now-lastStallReport>=UI_STALL_REPORT_INTERVAL_MILLIS){
                    lastStallReport=now
                    runCatching {
                        val stacks=Thread.getAllStackTraces()
                        fun frames(thread: Thread?):JSONArray{
                            val frames:List<StackTraceElement> = thread?.let{stacks[it]?.toList()} ?: emptyList()
                            val out=JSONArray()
                            for(frame in frames.take(64))out.put(frame.toString())
                            return out
                        }
                        val record=JSONObject().put("schemaVersion",1).put("attachGeneration",attachedGeneration)
                            .put("elapsedRealtimeMillis",now).put("mainIdleMillis",now-mainHeartbeat)
                            .put("main",frames(Looper.getMainLooper().thread))
                            .put("renderThread",frames(stacks.keys.firstOrNull{it.name=="RenderThread"}))
                        File(app.filesDir,"viewer-ui-stall.json").writeText(record.toString())
                    }
                }
            }
        },UI_STALL_MILLIS,UI_STALL_POLL_MILLIS,TimeUnit.MILLISECONDS)
    }
    /** Native detach is worker-only. The UI keeps responding and replacement stays
     * blocked on this generation until the retirement acknowledgement arrives. */
    fun detachSurface(generation: Long, onRetired: () -> Unit = {}) {
        stopFrames();cancelPendingAttach();if(drawable?.generation==generation)drawable=null
        val owner=nativeOwner.get()
        // Always serialize after an in-flight attach, even if pause cancelled its
        // main-thread acknowledgement before this callback arrived.
        if(!owner.isValid){onRetired();return}
        surfaceRetirements[generation]=onRetired
        pendingDetach=generation;attachedGeneration=0;attachRequestedGeneration=0;detachAttempts=0
        update(state.copy(detachPending=true))
        retryDetach(generation,owner)
    }
    private fun releaseRetiredSurface(generation: Long) { surfaceRetirements.remove(generation)?.invoke() }
    private fun retryDetach(generation: Long, owner: NativeHandle) {
        worker.execute {
            val result=NativeBridge.detach(owner,generation)
            main.post {
                // The serial worker has processed any queued attach first. An
                // unknown generation was never admitted by this live owner.
                val retired=result.ok||result.code=="unknown_surface"||(closed&&!nativeOwner.get().isValid)
                if(retired)releaseRetiredSurface(generation)
                if(closed||pendingDetach!=generation)return@post
                if(retired){pendingDetach=0;detachAttempts=0;update(state.copy(detachPending=false,message="旧画面已释放"));attachCurrent()}
                else if(++detachAttempts>=MAX_DETACH_ATTEMPTS){
                    update(state.copy(detachPending=true,message="旧画面释放未确认；请关闭并重新启动应用，原场景与设置已保留"))
                } else main.postDelayed({ if(!closed&&pendingDetach==generation)retryDetach(generation,owner) },1000)
            }
        }
    }
    fun setResumed(value: Boolean) {
        resumed=value
        main.removeCallbacks(freshnessWatch)
        if(value)main.post(freshnessWatch)
        else update(state.copy(presentation=ViewerPresentation.PAUSED,effective=null))
        if(!value){stopFrames();main.removeCallbacks(pollTask);pollScheduled=false};if(!NativeBridge.resume(handle,value).ok)resetFps()
        if(value){attachCurrent();scheduleFrames();schedulePoll();if(attachedGeneration>0)observeSurface(handle,attachedGeneration)}
    }
    private fun scheduleFrames() {
        if(!closed&&resumed&&handle.isValid&&attachedGeneration>0&&pendingDetach==0L&&openAccepted&&!state.buildBlocked&&!framePosted){
            framePosted=true;Choreographer.getInstance().postFrameCallback(this)
        }
    }
    private fun resetFps() {
        observationEpoch++
        presentationSample=null
        if(state.effective!=null||state.presentation==ViewerPresentation.READY)
            update(state.copy(effective=null,presentation=if(resumed)ViewerPresentation.PREPARING else ViewerPresentation.PAUSED))
        lastObservationAt=SystemClock.elapsedRealtime()
        fpsSampler.reset()
        if(state.observedFps!=null)update(state.copy(observedFps=null))
    }
    private fun canObserveFrames() = !closed&&resumed&&handle.isValid&&attachedGeneration>0&&
        pendingDetach==0L&&openAccepted&&!state.buildBlocked
    private fun stopFrames(){resetFps();if(framePosted){Choreographer.getInstance().removeFrameCallback(this);framePosted=false}}
    override fun doFrame(frameTimeNanos: Long) {
        framePosted=false;attachPendingSince=0
        if(!closed&&resumed&&attachedGeneration>0&&pendingDetach==0L&&openAccepted&&!state.buildBlocked){if(!NativeBridge.frame(handle,frameTimeNanos).ok)resetFps();scheduleFrames()}
    }
    fun cameraInput(input: CameraInputState) {if(!closed&&input.surfaceGeneration==attachedGeneration&&!state.buildBlocked)NativeBridge.input(handle,input)}
    fun trim(level: Int) {NativeBridge.trim(handle,level);if(level>=80){stopFrames();openAccepted=false;update(state.copy(presentation=ViewerPresentation.PAUSED,retryAvailable=true,message="内存不足，已暂停画面；可手动重试"))}}
    private fun control(owner: NativeHandle, action: String): NativeResult {
        if(!owner.isValid)return if(!nativeOwner.get().isValid&&sceneLease==null)NativeResult(true,"no_native_owner") else NativeResult(false,"invalid_handle")
        return try {NativeResult.fromJson(NativeBridge.nativeSettings(owner.value,JSONObject().put("schemaVersion",VIEWER_SCHEMA_VERSION)
            .put("viewerControl",JSONObject().put("action",action)).toString()))}
        catch (_: LinkageError){NativeResult(false,"native_unavailable")}catch(_: Exception){NativeResult(false,"invalid_native_result")}
    }
    /** Worker release ACK is required before dropping the filesystem lease. */
    private fun releaseScene(owner: NativeHandle): NativeResult {
        val result=control(owner,"releaseScene")
        if(result.ok){sceneLease?.close();sceneLease=null}
        return result
    }
    fun openBuiltin(){val descriptor=state.builtin?:return;openDescriptor(descriptor,null)}
    fun openScene(id: String) {
        if(closed)return
        val descriptor=state.scenes.firstOrNull{it.sceneId==id}
        if(descriptor!=null){openDescriptor(descriptor,id);return}
        if(!::repository.isInitialized)return
        worker.execute {try {
            val scenes=repository.list();val published=scenes.firstOrNull{it.sceneId==id}
            main.post {if(!closed){update(state.copy(scenes=scenes));if(published!=null)openDescriptor(published,id) else update(state.copy(message="场景尚未发布，请刷新后重试"))}}
        }catch(_: Exception){fail("无法读取构建结果，请稍后重试")}}
    }
    private fun openDescriptor(descriptor: SceneDescriptor,libraryId: String?) {
        if(LodBuildService.hasActiveJob()){update(state.copy(message="构建尚未结束，请先暂停并等待任务释放资源"));return}
        if(!working("正在载入场景"))return
        stopFrames()
        val owner=handle;val generation=sceneIds.incrementAndGet()
        worker.execute {
            var candidateLease: SceneLease? = null
            try {
                candidateLease=libraryId?.let{repository.acquire(it)}
                val result=NativeBridge.openScene(owner,(candidateLease?.descriptor?:descriptor).copy(generation=generation))
                if(!result.ok){
                    lastOpenFailure=PrivateFailure("open",result.code,detail=result.message.take(MAX_PRIVATE_FAILURE_DETAIL))
                    candidateLease?.close();candidateLease=null
                    main.post { update(state.copy(busy=false,presentation=ViewerPresentation.FAILED,retryAvailable=true,message="场景暂时无法打开，请重试或查看报告"));scheduleFrames() }
                    return@execute
                }
                val previousLease=sceneLease
                sceneLease=candidateLease;candidateLease=null
                previousLease?.close()
                lastOpenFailure=null
                settingsStore.save(state.requested,state.effective,descriptor.sceneId)
                main.post {
                    stopFrames();openAccepted=true
                    update(state.copy(busy=false,buildBlocked=false,selectedSceneId=descriptor.sceneId,effective=null,presentation=ViewerPresentation.PREPARING,retryAvailable=false,message="正在准备画面"))
                    scheduleFrames();if(attachedGeneration>0)observeSurface(handle,attachedGeneration)
                }
            }catch(error: Exception){
                candidateLease?.close()
                lastOpenFailure=PrivateFailure("open","open_failed",error)
                main.post { update(state.copy(busy=false,presentation=ViewerPresentation.FAILED,retryAvailable=true,message="场景载入未完成，原资源仍受保护；请重试"));scheduleFrames() }
            }
        }
    }
    fun retry() {
        if(closed)return
        if(initializing.get()){update(state.copy(message="初始化仍在进行；完成后再试或导出报告"));return}
        if(!working("正在重试"))return
        resetFps()
        if(!handle.isValid||!::repository.isInitialized||!::jobStore.isInitialized||!::settingsStore.isInitialized){initialize();return}
        val owner=handle
        worker.execute {
            val result=NativeBridge.retry(owner)
            main.post {update(state.copy(busy=false,message=if(result.ok)"已请求恢复，请选择场景" else "暂时无法恢复，可导出报告"));attachCurrent()}
        }
    }
    fun refresh() {
        if(closed||!::repository.isInitialized)return
        worker.execute {try {val scenes=repository.list();val jobs=jobStore.list();main.post{update(state.copy(scenes=scenes,jobs=jobs))}}catch(_: Exception){fail("无法读取场景或任务列表")}}
    }
    fun importDocument(uri: Uri) {
        if(!working("正在复制并检查文件"))return;importCancelled.set(false)
        update(state.copy(importInProgress=true))
        worker.execute {try {
            val result=SceneImportController(app,repository).importDocument(uri,importCancelled::get)
            val scenes=repository.list()
            lastImportFailure=null
            main.post {update(state.copy(busy=false,importInProgress=false,scenes=scenes,pendingImport=result as? ImportOutcome.BuildRequired,
                message=if(result is ImportOutcome.BuildRequired)"文件已导入，需要先构建才能观看" else "导入完成，请从场景库打开"))}
        }catch(error: Exception){
            lastImportFailure=PrivateFailure("import",importFailureCode(error),error)
            main.post { update(state.copy(importInProgress=false)) }
            fail(if(importCancelled.get())"导入已取消" else "无法导入，请检查文件格式和剩余空间")
        }}
    }
    fun cancelImport(){importCancelled.set(true)}
    fun discardPendingImport(){val source=state.pendingImport?:return;update(state.copy(pendingImport=null));worker.execute{try{repository.discardBuildInput(source.stableSource)}catch(_: Exception){fail("临时文件暂未清理，可稍后重试")}}}
    fun createBuild(onCreated: (String)->Unit) {
        val source=state.pendingImport?:return;if(!working("正在创建构建任务"))return
        worker.execute {try {val job=jobStore.create(source.stableSource,source.sha256,source.bytes);val jobs=jobStore.list()
            main.post{update(state.copy(busy=false,pendingImport=null,jobs=jobs,message="任务已保存；构建期间将暂停画面"));onCreated(job.id)}}
            catch(_: Exception){fail("无法创建任务，请检查剩余空间")}}
    }
    /** User start/resume is completed by visible Activity only after safe release. */
    fun prepareBuild(onReady: (NativeResult)->Unit) {
        if(!working("正在释放画面，准备构建")){onReady(NativeResult(false,"build_busy"));return}
        stopFrames();openAccepted=false;update(state.copy(buildBlocked=true,presentation=ViewerPresentation.PAUSED,effective=null,retryAvailable=false));val owner=handle
        worker.execute {val result=releaseScene(owner);main.post{update(state.copy(busy=false,message=if(result.ok)"画面已暂停，可开始构建" else "仍在释放画面，请稍后重试"));onReady(result)}}
    }
    fun applySettings(config: ViewerConfig) {
        if(!working("正在保存设置"))return;resetFps();val owner=handle
        worker.execute {try {
            val requested=ViewerSettingsStore.validate(config);settingsStore.save(requested,state.effective)
            val result=NativeBridge.settings(owner,requested)
            main.post{update(state.copy(busy=false,requested=requested,message=if(result.ok)"设置已提交，实际效果以画面状态为准" else "设置已保存，渲染恢复后可重试"))}
        }catch(_: Exception){fail("设置无效，未应用")}}
    }
    fun deleteScene(id: String) {
        if(!working("正在删除场景"))return
        val selected=state.selectedSceneId==id;val owner=handle;if(selected){stopFrames();openAccepted=false}
        worker.execute {try {
            if(selected)check(releaseScene(owner).ok)
            val removed=repository.requestDelete(id);val scenes=repository.list()
            main.post{update(state.copy(busy=false,scenes=scenes,selectedSceneId=if(selected)"" else state.selectedSceneId,
                message=if(removed)"场景已删除" else "场景已隐藏，将在使用结束后删除"))}
        }catch(_: Exception){fail("场景仍在使用，未确认删除")}}
    }
    fun cleanupJob(id: String) {
        if(!working("正在清理已结束任务"))return
        worker.execute{try{jobStore.cleanupRetired(id,true);val jobs=jobStore.list();main.post{update(state.copy(busy=false,jobs=jobs,message="任务和临时源文件已清理"))}}catch(_: Exception){fail("任务仍在使用或共享源文件，请等待后重试")}}
    }
    private fun schedulePoll() {
        if(!closed&&resumed&&!pollScheduled){pollScheduled=true;main.postDelayed(pollTask,1500)}
    }
    private fun poll() {
        // Expire on the main thread even when imports or diagnostics block the worker.
        if(BuildConfig.FPS_OVERLAY_ENABLED&&SystemClock.elapsedRealtime()-lastObservationAt>=FPS_FRESHNESS_MILLIS)resetFps()
        if(closed||!resumed||!::repository.isInitialized||!::jobStore.isInitialized||!::settingsStore.isInitialized||!pollQueued.compareAndSet(false,true))return
        val owner=handle;val epoch=observationEpoch
        val requestedAt=SystemClock.elapsedRealtime()
        worker.execute{try {
            val result=NativeBridge.snapshot(owner,request("diagnostics"))
            if(result.ok)runCatching{File(app.filesDir,"viewer-last-diagnostics.json").writeText(result.payload.toString().take(32768))}
            val scenes=repository.list();val jobs=jobStore.list()
            main.post {
                // A reply requested before pause/detach/open must not repopulate the overlay.
                if(closed||epoch!=observationEpoch)return@post
                if(SystemClock.elapsedRealtime()-requestedAt>=FPS_FRESHNESS_MILLIS){resetFps();return@post}
                val latest=result.payload.optJSONObject("diagnostics")?.optJSONObject("latest")
                val surface=result.payload.optLong("surfaceGeneration",0)
                val valid=result.ok&&canObserveFrames()&&result.payload.optBoolean("resumed")&&
                    result.payload.optBoolean("windowInUse")&&surface==attachedGeneration&&
                    latest?.optLong("surfaceGeneration",0)==attachedGeneration&&
                    latest?.optJSONObject("status")?.optBoolean("ok")==true
                val sample=if(valid&&latest!=null)ObservedFpsSample(latest.optLong("frameId",0),
                    latest.optLong("submissionToken",0),latest.optLong("sceneGeneration",0),
                    latest.optLong("surfaceGeneration",0),latest.optJSONObject("timing")
                        ?.optDouble("presentReturnCadenceMilliseconds",Double.NaN)?:Double.NaN) else null
                if(sample!=null)lastObservationAt=SystemClock.elapsedRealtime()
                observePresentation(result)
                update(state.copy(diagnostics=result.payload,observedFps=if(BuildConfig.FPS_OVERLAY_ENABLED)fpsSampler.observe(sample) else null,
                    scenes=scenes,jobs=jobs,
                    message=when {state.busy->state.message;LodBuildService.hasActiveJob()->"正在后台构建，可在任务页暂停或取消";state.detachPending->"正在释放旧画面";
                        else->state.message}))
            }
        }catch(_: Exception){main.post {if(epoch==observationEpoch)resetFps()}}
        finally{pollQueued.set(false)}}
    }
    /** Advancing submissions in the current epoch prove readiness, never saved state or a single old frame. */
    private fun observePresentation(result: NativeResult): Boolean {
        if(!canObserveFrames())return false
        val payload=result.payload
        val latest=payload.optJSONObject("diagnostics")?.optJSONObject("latest")
        val status=payload.optJSONObject("status")
        if(!result.ok||status?.optBoolean("ok")==false) {
            stopFrames()
            update(state.copy(effective=null,presentation=ViewerPresentation.FAILED,retryAvailable=true,
                diagnostics=payload,message="画面暂不可用；可重试或导出报告"))
            return false
        }
        val valid=canObserveFrames()&&payload.optBoolean("resumed")&&payload.optBoolean("windowInUse")&&
            payload.optLong("surfaceGeneration",0)==attachedGeneration&&
            latest?.optLong("surfaceGeneration",0)==attachedGeneration&&
            latest?.optLong("sceneGeneration",0)==sceneIds.get()&&
            latest?.optJSONObject("status")?.optBoolean("ok")==true
        if(!valid||latest==null) { presentationSample=null;return false }
        val sample=ObservedFpsSample(latest.optLong("frameId",0),latest.optLong("submissionToken",0),
            latest.optLong("sceneGeneration",0),latest.optLong("surfaceGeneration",0),Double.NaN)
        if(sample.frameId<=0||sample.submissionToken<=0)return false
        val before=presentationSample
        presentationSample=sample
        val advancing=before!=null&&before.sceneGeneration==sample.sceneGeneration&&
            before.surfaceGeneration==sample.surfaceGeneration&&sample.frameId>before.frameId&&
            sample.submissionToken>before.submissionToken
        if(advancing) {
            presentationObservedAt=SystemClock.elapsedRealtime()
            val effective=runCatching { payload.optJSONObject("effectiveConfig")?.let{ViewerConfig.fromJson(it)} }.getOrNull()
                ?.takeIf { it.generation==state.requested.generation }
            update(state.copy(presentation=ViewerPresentation.READY,retryAvailable=false,effective=effective,
                diagnostics=payload))
        }
        return advancing
    }
    /** Diagnostics export stays available during a bounded/slow initialization. */
    fun exportDiagnostics(onReady: (File)->Unit) {
        if(closed)return;val owner=handle
        worker.execute{try {
            val snapshot=NativeBridge.snapshot(owner,request("diagnostics"))
            val reports=(if(::jobStore.isInitialized)jobStore.list() else emptyList()).take(32).map{JSONObject().put("schemaVersion",1).put("progress",it.progress.toJson())}
            val file=DiagnosticsExporter(app).createBundle(if(snapshot.ok)snapshot.payload else null,snapshot.payload.optString("framesCSV").takeIf{it.isNotEmpty()},reports)
            main.post{update(state.copy(busy=false,message="报告已生成，可选择分享"));onReady(file)}
        }catch(_: Exception){fail("报告生成失败，可稍后重试")}}
    }
    fun exportScene(sceneId: String,onReady: (File)->Unit) {
        if(!working("正在生成场景包"))return
        worker.execute{try{val file=ScenePackage(repository).exportPackage(sceneId);main.post{update(state.copy(busy=false,message="场景包已生成，可选择分享"));onReady(file)}}catch(_: Exception){fail("场景包导出失败，请检查剩余空间并稍后重试")}}
    }
    fun requestScreenshot(onReady: (File)->Unit) {
        if(!working("正在截取三维画面"))return
        val owner=handle;val sequence=++screenshotRequestSequence
        val deadline=SystemClock.elapsedRealtime()+SCREENSHOT_WAIT_MILLIS
        worker.execute {
            val result=NativeBridge.snapshot(owner,request("screenshot"))
            main.post {
                if(closed||sequence!=screenshotRequestSequence)return@post
                if(!result.ok)update(state.copy(busy=false,message="截图请求失败（${result.code}）；可导出普通报告"))
                else {
                    if(result.code.startsWith("screenshot_resume_"))update(state.copy(message="正在继续获取上次请求的截图；图像保留原始帧与相机状态"))
                    pollScreenshot(owner,sequence,deadline,onReady)
                }
            }
        }
    }
    private fun pollScreenshot(owner: NativeHandle,sequence: Long,deadline: Long,onReady: (File)->Unit) {
        if(closed||sequence!=screenshotRequestSequence)return
        worker.execute{try {
            val response=NativeBridge.snapshot(owner,request("screenshotStatus"));val p=response.payload
            if(response.ok&&p.optBoolean("gpuCompleted")&&p.optString("status")=="completed") {
                val diagnostics=NativeBridge.snapshot(owner,request("diagnostics"))
                val bundle=DiagnosticsExporter(app).createBundle(diagnostics.payload,diagnostics.payload.optString("framesCSV").takeIf{it.isNotEmpty()},
                    screenshot=File(p.getString("path")),screenshotMetadata=p.getJSONObject("metadata"))
                main.post {if(!closed&&sequence==screenshotRequestSequence){update(state.copy(busy=false,message="截图报告已生成，可选择分享"));onReady(bundle)}}
            }else main.post {
                if(closed||sequence!=screenshotRequestSequence)return@post
                when {
                    response.code!="pending" -> update(state.copy(busy=false,message="截图失败（${response.code}）；可导出普通报告"))
                    SystemClock.elapsedRealtime()<deadline -> main.postDelayed({pollScreenshot(owner,sequence,deadline,onReady)},250)
                    else -> update(state.copy(busy=false,message="截图等待超过 60 秒；原请求仍保留，再次截图可继续获取结果"))
                }
            }
        }catch(_: Exception){main.post {if(!closed&&sequence==screenshotRequestSequence)update(state.copy(busy=false,message="截图报告生成未完成，可导出普通报告"))}}}
    }
    fun close() {
        if(closed)return;stopFrames();cancelPendingAttach();stallWatch?.cancel(false);stallWatch=null
        initWatchdog?.let{main.removeCallbacks(it)};initWatchdog=null
        NativeBridge.resume(handle,false);closed=true;onState=null;importCancelled.set(true)
        main.removeCallbacksAndMessages(null);pollScheduled=false;handle=NativeHandle()
        worker.execute {
            val owner=nativeOwner.get();val retired=NativeBridge.destroy(owner);lastRetirement=retired
            recordOwnerEvent("retired",retired)
            if(retired.ok){nativeOwner.set(NativeHandle());sceneLease?.close();sceneLease=null
                main.post { surfaceRetirements.keys.toList().forEach(::releaseRetiredSurface) }
            }
            else retirementBlocker=this // Keep leases/provider ownership pinned; never admit a replacement blindly.
        }
    }
    /** Detach the Activity's callback without retiring the process-owned renderer.
     * Used for configuration changes and transient Activity destruction. */
    fun detachActivity() {
        stallWatch?.cancel(false);stallWatch=null;onState=null;setResumed(false)
    }
    companion object {
        // Process lifetime is intentional: a private executor per Activity permits
        // its successor to Create while the predecessor still retains GPU backing.
        private val viewerWorker=Executors.newSingleThreadExecutor { task -> Thread(task,"MonsterViewerIO") }
        private val stallWatcher=Executors.newSingleThreadScheduledExecutor { task -> Thread(task,"MonsterUIWatchdog").apply{isDaemon=true} }
        private var retirementBlocker: ViewerController?=null // worker-only; retained until process restart on failure
        private var lastRetirement: NativeResult?=null // worker-only private observation
        private const val FPS_FRESHNESS_MILLIS = 3000L
        private const val SCREENSHOT_WAIT_MILLIS = 60000L
        private const val MAX_PRIVATE_FAILURE_DETAIL = 256
        private const val INIT_WATCHDOG_MILLIS = 120000L
        private const val INITIAL_SCENE_DELAY_MILLIS = 250L
        private const val MAX_DETACH_ATTEMPTS = 30
        private const val STARTUP_OBSERVATION_ATTEMPTS = 20
        private const val UI_STALL_MILLIS = 10000L
        private const val UI_STALL_POLL_MILLIS = 5000L
        private const val UI_STALL_REPORT_INTERVAL_MILLIS = 60000L
        private fun request(kind: String)=JSONObject().put("schemaVersion",VIEWER_SCHEMA_VERSION).put("kind",kind).put("includePrivatePaths",false)
    }
}
