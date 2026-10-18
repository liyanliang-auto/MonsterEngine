package com.monsterengine.viewer

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.Choreographer
import android.view.Surface
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** Activity-independent render/library owner. Disk, import, native creation and
 * exports use one worker. Main-thread frame callbacks send one coalesced token only. */
class ViewerController(context: Context) : Choreographer.FrameCallback {
    data class State(val message: String = "正在准备场景", val busy: Boolean = true,
        val scenes: List<SceneDescriptor> = emptyList(), val builtin: SceneDescriptor? = null,
        val jobs: List<BuildJobRecord> = emptyList(), val requested: ViewerConfig = ViewerConfig(),
        val effective: ViewerConfig? = null, val selectedSceneId: String = "",
        val nativeAvailable: Boolean = false, val detachPending: Boolean = false,
        val buildBlocked: Boolean = false, val pendingImport: ImportOutcome.BuildRequired? = null,
        val diagnostics: JSONObject = NativeBridge.partialDiagnostics())
    private data class Drawable(val surface: Surface, val generation: Long, val width: Int, val height: Int)
    private data class PrivateFailure(val operation: String, val code: String,
        val cause: Throwable? = null, val detail: String = "")
    private val app = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { task -> Thread(task, "MonsterViewerIO") }
    private val surfaceIds = AtomicLong(0)
    private val sceneIds = AtomicLong(0)
    private val importCancelled = AtomicBoolean(false)
    private val pollQueued = AtomicBoolean(false)
    private val initializing = AtomicBoolean(false)
    private var pollScheduled = false
    private val pollTask = Runnable { pollScheduled=false;poll();schedulePoll() }
    private var handle = NativeHandle()
    private val nativeOwner = AtomicReference(NativeHandle())
    private var drawable: Drawable? = null
    private var attachedGeneration = 0L
    private var pendingDetach = 0L
    private var resumed = false
    private var framePosted = false
    private var openAccepted = false
    @Volatile private var closed = false
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
        if(closed||!initializing.compareAndSet(false,true))return
        worker.execute {
            try {
                repository=SceneRepository(app);jobStore=BuildJobStore(app);settingsStore=ViewerSettingsStore(app)
                val saved = try { settingsStore.load() } catch (_: Exception) { ViewerSettingsStore.Saved(ViewerConfig()) }
                if (!LodBuildService.hasActiveJob()) jobStore.discoverInterrupted()
                val previous=nativeOwner.get()
                val created = if(previous.isValid)previous else NativeBridge.create(app.assets, app.filesDir.absolutePath, saved.requested).also { nativeOwner.set(it) }
                val builtinResult = if (created.isValid) NativeBridge.snapshot(created, request("builtinScene")) else NativeResult()
                val builtin = builtinResult.payload.optJSONObject("descriptor")?.let { SceneDescriptor.fromJson(it) }
                val scenes=repository.list();val jobs=jobStore.list()
                val probe=NativeBridge.probe()
                main.post {
                    initializing.set(false)
                    if (closed) return@post
                    handle=created
                    update(state.copy(busy=false,requested=saved.requested,effective=saved.effective,scenes=scenes,jobs=jobs,builtin=builtin,
                        nativeAvailable=created.isValid,diagnostics=probe.payload,
                        message=if(created.isValid)"请选择场景，或打开内置示例" else "当前设备无法启动渲染；仍可查看任务和导出报告"))
                    attachCurrent();NativeBridge.resume(handle,resumed)
                    if(builtin!=null&&!LodBuildService.hasActiveJob())openBuiltin()
                    schedulePoll()
                }
            } catch (_: Exception) { main.post { initializing.set(false);handle=nativeOwner.get();update(state.copy(busy=false,nativeAvailable=handle.isValid,message="初始化未完成，请重试或导出报告")) } }
        }
    }
    private fun update(next: State) { if(!closed){state=next;onState?.invoke(next)} }
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
    fun attachSurface(surface: Surface,generation: Long,width: Int,height: Int) {
        drawable=Drawable(surface,generation,width,height);attachCurrent()
    }
    private fun attachCurrent() {
        val target=drawable?:return
        if(!handle.isValid||pendingDetach!=0L||!target.surface.isValid)return
        val result=NativeBridge.attach(handle,target.surface,target.generation,target.width,target.height)
        if(result.ok){attachedGeneration=target.generation;scheduleFrames()}
        else update(state.copy(message="画面暂不可用，可点击重试"))
    }
    /** Direct synchronous handshake, no Java queue lock. Failure retains pending
     * generation and native ownership; replacement and disk-lease release stay blocked. */
    fun detachSurface(generation: Long) {
        stopFrames();if(drawable?.generation==generation)drawable=null
        if(generation!=attachedGeneration&&pendingDetach!=generation)return
        pendingDetach=generation;attachedGeneration=0
        val result=NativeBridge.detach(handle,generation)
        if(result.ok){pendingDetach=0;update(state.copy(detachPending=false))}
        else {update(state.copy(detachPending=true,message="正在释放旧画面，请稍候"));retryDetach(generation,handle)}
    }
    private fun retryDetach(generation: Long, owner: NativeHandle) {
        worker.execute {
            val result=NativeBridge.detach(owner,generation)
            main.post {
                if(closed||pendingDetach!=generation)return@post
                if(result.ok){pendingDetach=0;update(state.copy(detachPending=false,message="旧画面已释放"));attachCurrent()}
                else main.postDelayed({ if(!closed&&pendingDetach==generation)retryDetach(generation,owner) },1000)
            }
        }
    }
    fun setResumed(value: Boolean) {
        resumed=value;if(!value){stopFrames();main.removeCallbacks(pollTask);pollScheduled=false};NativeBridge.resume(handle,value)
        if(value){attachCurrent();scheduleFrames();schedulePoll()}
    }
    private fun scheduleFrames() {
        if(!closed&&resumed&&handle.isValid&&attachedGeneration>0&&pendingDetach==0L&&openAccepted&&!state.buildBlocked&&!framePosted){
            framePosted=true;Choreographer.getInstance().postFrameCallback(this)
        }
    }
    private fun stopFrames(){if(framePosted){Choreographer.getInstance().removeFrameCallback(this);framePosted=false}}
    override fun doFrame(frameTimeNanos: Long) {
        framePosted=false
        if(!closed&&resumed&&attachedGeneration>0&&pendingDetach==0L&&openAccepted&&!state.buildBlocked){NativeBridge.frame(handle,frameTimeNanos);scheduleFrames()}
    }
    fun cameraInput(input: CameraInputState) {if(!closed&&input.surfaceGeneration==attachedGeneration&&!state.buildBlocked)NativeBridge.input(handle,input)}
    fun trim(level: Int) {NativeBridge.trim(handle,level);if(level>=80){stopFrames();openAccepted=false;update(state.copy(message="内存不足，已暂停画面；可手动重试"))}}
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
        val owner=handle;val generation=sceneIds.incrementAndGet()
        worker.execute {
            var candidateLease: SceneLease? = null
            try {
                candidateLease=libraryId?.let{repository.acquire(it)}
                val result=NativeBridge.openScene(owner,(candidateLease?.descriptor?:descriptor).copy(generation=generation))
                if(!result.ok){
                    lastOpenFailure=PrivateFailure("open",result.code,detail=result.message.take(MAX_PRIVATE_FAILURE_DETAIL))
                    candidateLease?.close();candidateLease=null
                    main.post { update(state.copy(busy=false,message="场景暂时无法打开，请重试或查看报告"));scheduleFrames() }
                    return@execute
                }
                val previousLease=sceneLease
                sceneLease=candidateLease;candidateLease=null
                previousLease?.close()
                lastOpenFailure=null
                main.post {
                    stopFrames();openAccepted=true
                    update(state.copy(busy=false,buildBlocked=false,selectedSceneId=descriptor.sceneId,message="正在准备画面"))
                    scheduleFrames()
                }
            }catch(error: Exception){
                candidateLease?.close()
                lastOpenFailure=PrivateFailure("open","open_failed",error)
                main.post { update(state.copy(busy=false,message="场景载入未完成，原资源仍受保护；请重试"));scheduleFrames() }
            }
        }
    }
    fun retry() {
        if(!working("正在重试"))return
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
        worker.execute {try {
            val result=SceneImportController(app,repository).importDocument(uri,importCancelled::get)
            val scenes=repository.list()
            lastImportFailure=null
            main.post {update(state.copy(busy=false,scenes=scenes,pendingImport=result as? ImportOutcome.BuildRequired,
                message=if(result is ImportOutcome.BuildRequired)"文件已导入，需要先构建才能观看" else "导入完成，请从场景库打开"))}
        }catch(error: Exception){
            lastImportFailure=PrivateFailure("import",importFailureCode(error),error)
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
        if(!working("正在释放画面，准备构建"))return
        stopFrames();openAccepted=false;update(state.copy(buildBlocked=true));val owner=handle
        worker.execute {val result=releaseScene(owner);main.post{update(state.copy(busy=false,message=if(result.ok)"画面已暂停，可开始构建" else "仍在释放画面，请稍后重试"));onReady(result)}}
    }
    fun applySettings(config: ViewerConfig) {
        if(!working("正在保存设置"))return;val owner=handle
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
        if(closed||!resumed||!::repository.isInitialized||!::jobStore.isInitialized||!::settingsStore.isInitialized||!pollQueued.compareAndSet(false,true))return
        val owner=handle
        worker.execute{try {
            val result=NativeBridge.snapshot(owner,request("diagnostics"));val scenes=repository.list();val jobs=jobStore.list()
            val effective=result.payload.optJSONObject("effectiveConfig")?.let{ViewerConfig.fromJson(it)}
            if(effective!=null)settingsStore.save(state.requested,effective)
            main.post {update(state.copy(diagnostics=result.payload,scenes=scenes,jobs=jobs,effective=effective?:state.effective,
                message=when {state.busy->state.message;LodBuildService.hasActiveJob()->"正在后台构建，可在任务页暂停或取消";state.detachPending->"正在释放旧画面";else->state.message}))}
        }catch(_: Exception){/* Leave the last observed immutable UI state intact. */}finally{pollQueued.set(false)}}
    }
    fun exportDiagnostics(onReady: (File)->Unit) {
        if(!working("正在生成报告"))return;val owner=handle
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
        if(!working("正在截取三维画面"))return;val owner=handle
        worker.execute {
            val result=NativeBridge.snapshot(owner,request("screenshot"))
            main.post {if(!result.ok){update(state.copy(busy=false,message="当前画面无法截图"))}else pollScreenshot(owner,0,onReady)}
        }
    }
    private fun pollScreenshot(owner: NativeHandle,attempt: Int,onReady: (File)->Unit) {
        if(closed)return
        worker.execute{try {
            val response=NativeBridge.snapshot(owner,request("screenshotStatus"));val p=response.payload
            if(response.ok&&p.optBoolean("gpuCompleted")&&p.optString("status")=="completed") {
                val diagnostics=NativeBridge.snapshot(owner,request("diagnostics"))
                val bundle=DiagnosticsExporter(app).createBundle(diagnostics.payload,diagnostics.payload.optString("framesCSV").takeIf{it.isNotEmpty()},
                    screenshot=File(p.getString("path")),screenshotMetadata=p.getJSONObject("metadata"))
                main.post{update(state.copy(busy=false,message="截图报告已生成，可选择分享"));onReady(bundle)}
            }else if(attempt<20)main.postDelayed({pollScreenshot(owner,attempt+1,onReady)},250)
            else fail("截图尚未完成，请保持画面可见后重试")
        }catch(_: Exception){fail("截图未完成，可导出普通报告")}}
    }
    fun close() {
        if(closed)return;stopFrames();NativeBridge.resume(handle,false);closed=true;onState=null;importCancelled.set(true)
        main.removeCallbacksAndMessages(null);pollScheduled=false;handle=NativeHandle()
        worker.execute {val retired=NativeBridge.destroy(nativeOwner.getAndSet(NativeHandle()));if(retired.ok){sceneLease?.close();sceneLease=null}}
        worker.shutdown() // runs queued retirement; never waits on Activity/UI
    }
    companion object {
        private const val MAX_PRIVATE_FAILURE_DETAIL = 256
        private fun request(kind: String)=JSONObject().put("schemaVersion",VIEWER_SCHEMA_VERSION).put("kind",kind).put("includePrivatePaths",false)
    }
}
