package com.monsterengine.viewer

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import org.json.JSONObject
import java.io.File

/** Local-only library/viewer/task/settings navigation. File and native work is
 * delegated to ViewerController; visible user actions alone start build/share. */
class MainActivity : Activity() {
    private lateinit var controller: ViewerController
    private lateinit var surface: ViewerSurfaceView
    private lateinit var status: TextView
    private lateinit var panel: LinearLayout
    private lateinit var scroll: ScrollView
    private lateinit var viewer: FrameLayout
    private lateinit var cancelImport: Button
    private var screen = "viewer"
    private var pendingJob: String? = null
    private var lastLibrary = ""
    private var lastJobs = ""
    private var effectiveSettings: TextView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        screen=savedInstanceState?.getString("screen")?:intent.getStringExtra("screen")?:"viewer"
        controller=ViewerController(applicationContext)
        val root=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setPadding(12,12,12,12)}
        val navigation=LinearLayout(this)
        for((key,label) in listOf("viewer" to "画面","library" to "场景","builds" to "任务","settings" to "设置","report" to "报告"))
            navigation.addView(Button(this).apply{text=label;setOnClickListener{showScreen(key)}},LinearLayout.LayoutParams(0,-2,1f))
        root.addView(navigation)
        status=TextView(this).apply{setPadding(6,10,6,10);textSize=15f;accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        root.addView(status)
        cancelImport=Button(this).apply{text="取消导入";visibility=View.GONE;setOnClickListener{controller.cancelImport()}}
        root.addView(cancelImport)
        viewer=FrameLayout(this);surface=ViewerSurfaceView(this,controller);viewer.addView(surface,FrameLayout.LayoutParams(-1,-1))
        root.addView(viewer,LinearLayout.LayoutParams(-1,0,1f))
        val viewerActions=LinearLayout(this)
        viewerActions.addView(Button(this).apply{text="复位相机";setOnClickListener{surface.resetCamera()}})
        viewerActions.addView(Button(this).apply{text="重试画面";setOnClickListener{controller.retry()}})
        viewerActions.tag="viewerActions";root.addView(viewerActions)
        panel=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL}
        scroll=ScrollView(this).apply{addView(panel)};root.addView(scroll,LinearLayout.LayoutParams(-1,0,1f))
        setContentView(root)
        controller.onState={state->
            status.text=state.message
            cancelImport.visibility=if(state.busy&&state.message.contains("复制"))View.VISIBLE else View.GONE
            if(screen=="library")renderLibrary(state)
            if(screen=="builds")renderJobs(state)
            if(screen=="settings")effectiveSettings?.text=effectiveText(state.effective)
        }
        showScreen(screen)
        controller.initialize()
    }
    override fun onResume(){super.onResume();if(::controller.isInitialized)controller.setResumed(true)}
    override fun onPause(){if(::controller.isInitialized)controller.setResumed(false);super.onPause()}
    override fun onDestroy(){if(::controller.isInitialized)controller.close();super.onDestroy()}
    override fun onTrimMemory(level: Int){super.onTrimMemory(level);if(::controller.isInitialized)controller.trim(level)}
    override fun onSaveInstanceState(outState: Bundle){outState.putString("screen",screen);super.onSaveInstanceState(outState)}
    override fun onNewIntent(intent: Intent){super.onNewIntent(intent);setIntent(intent);if(intent.getStringExtra("screen")=="builds")showScreen("builds")}

    private fun showScreen(next: String) {
        screen=next;viewer.visibility=if(next=="viewer")View.VISIBLE else View.GONE
        (viewer.parent as LinearLayout).findViewWithTag<View>("viewerActions").visibility=viewer.visibility
        scroll.visibility=if(next=="viewer")View.GONE else View.VISIBLE
        when(next){"library"->{lastLibrary="";renderLibrary(controller.state)};"builds"->{lastJobs="";renderJobs(controller.state);controller.refresh()};
            "settings"->renderSettings();"report"->renderReport()}
    }
    private fun text(value: String,size: Float=16f){panel.addView(TextView(this).apply{text=value;textSize=size;setPadding(6,12,6,8)})}
    private fun button(label: String,enabled: Boolean=true,action: ()->Unit){panel.addView(Button(this).apply{text=label;isEnabled=enabled;setOnClickListener{action()}})}
    private fun confirm(title: String,message: String,positive: String="确认",action: ()->Unit){
        AlertDialog.Builder(this).setTitle(title).setMessage(message).setNegativeButton("返回",null).setPositiveButton(positive){_,_->action()}.show()
    }
    private fun renderLibrary(state: ViewerController.State) {
        val identity="${state.scenes.map{it.sceneId}}:${state.busy}:${state.builtin!=null}:${state.pendingImport!=null}"
        if(identity==lastLibrary)return;lastLibrary=identity;val y=scroll.scrollY;panel.removeAllViews()
        text("场景库",22f)
        button("打开内置示例",!state.busy&&state.builtin!=null){controller.openBuiltin();showScreen("viewer")}
        text("内置示例由本项目生成，可离线使用。单指旋转，双指平移或缩放，双击复位。",14f)
        button("导入 PLY 或场景包",!state.busy&&state.pendingImport==null){
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION),IMPORT_REQUEST)
        }
        state.pendingImport?.let{source->
            text("已导入大场景：${source.bytes/(1024*1024)} MiB。需要先构建；构建会暂停画面，可在任务页暂停或取消。")
            button("创建并开始构建",!state.busy){confirm("开始构建？","构建在设备上完成。完成后可从场景库打开；暂停和取消会保留可恢复进度。","开始"){controller.createBuild{startJob(it)}}}
            button("放弃这次导入",!state.busy){confirm("清理导入文件？","尚未创建的构建源文件将被清理。","清理"){controller.discardPendingImport()}}
        }
        if(state.scenes.isEmpty())text("还没有导入的场景。")
        state.scenes.forEachIndexed{index,scene->
            text("场景 ${index+1} · ${if(scene.kind==ViewerSceneKind.LOD)"流式场景" else "小场景"}")
            button("打开场景 ${index+1}",!state.busy){controller.openScene(scene.sceneId);showScreen("viewer")}
            if(scene.kind==ViewerSceneKind.LOD)button("导出场景包 ${index+1}",!state.busy){controller.exportScene(scene.sceneId){file->offerShare(file,"场景包")}}
            button("删除场景 ${index+1}",!state.busy){confirm("删除场景？","场景将在使用结束后删除，不影响已导出的场景包。","删除"){controller.deleteScene(scene.sceneId)}}
        }
        scroll.post{scroll.scrollTo(0,y)}
    }
    private fun renderJobs(state: ViewerController.State) {
        val identity=state.jobs.joinToString{"${it.id}:${it.updatedAtMillis}"}+":${state.busy}"
        if(identity==lastJobs)return;lastJobs=identity;val y=scroll.scrollY;panel.removeAllViews();text("构建任务",22f)
        if(!BuildNotifications(this).permissionGranted())text("通知未开启；任务仍可运行，请在本页查看和控制。",14f)
        button("刷新任务",!state.busy){controller.refresh()}
        if(state.jobs.isEmpty())text("暂无任务。导入大场景后可以创建。")
        state.jobs.forEachIndexed{index,job->
            val p=job.progress
            text("任务 ${index+1} · ${stageName(p.stage)}")
            text(if(p.totalRecords>0)"已处理 ${p.processedRecords} / ${p.totalRecords} 个高斯" else "已处理 ${p.processedRecords} 个高斯；总量待确定",14f)
            if(p.peakWorkingBytes>0)text("已记录工作内存峰值 ${p.peakWorkingBytes/(1024*1024)} MiB",14f)
            if(job.stopReason.isNotEmpty()||p.error.isNotEmpty())text("任务已停止。请检查可用空间、设备温度或系统限制后手动重试；详细信息可导出报告。",14f)
            if(p.stage==BuildStage.Completed){button("打开构建结果",!state.busy&&job.publishedSceneId.isNotEmpty()){controller.refresh();controller.openScene(job.publishedSceneId);showScreen("viewer")}}
            else {
                button(if(p.stage in RETIRED)"继续 / 重试" else "开始",!state.busy&&!LodBuildService.hasActiveJob()){startJob(job.id)}
                button("暂停",LodBuildService.hasActiveJob()){showControl(LodBuildService.control(this,job.id,LodBuildService.ACTION_PAUSE))}
                button("取消并保留进度",!state.busy){confirm("取消构建？","已提交的进度会保留，可稍后继续或清理。","取消构建"){showControl(LodBuildService.control(this,job.id,LodBuildService.ACTION_CANCEL))}}
            }
            button("清理任务和临时源文件",!state.busy&&p.stage in RETIRED&&!LodBuildService.hasActiveJob()){
                confirm("清理任务？","构建进度和临时源文件将被删除；已发布场景仍保留。","清理"){controller.cleanupJob(job.id)}}
        }
        scroll.post{scroll.scrollTo(0,y)}
    }
    private fun startJob(id: String) {
        controller.prepareBuild{released->
            if(isFinishing||isDestroyed)return@prepareBuild
            if(!released.ok){toast("画面尚未释放，请稍后重试");return@prepareBuild}
            if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){
                pendingJob=id;requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),NOTIFICATION_REQUEST)
            }else launchBuild(id)
        }
    }
    private fun launchBuild(id: String){showControl(LodBuildService.startFromVisibleActivity(this,id));showScreen("builds");controller.refresh()}
    override fun onRequestPermissionsResult(requestCode: Int,permissions: Array<out String>,grantResults: IntArray){
        super.onRequestPermissionsResult(requestCode,permissions,grantResults)
        if(requestCode==NOTIFICATION_REQUEST){val id=pendingJob;pendingJob=null
            if(grantResults.firstOrNull()!=PackageManager.PERMISSION_GRANTED)toast("通知未开启，可在任务页继续查看和控制")
            if(id!=null)window.decorView.post{launchBuild(id)}}
    }
    @Deprecated("Framework document picker result")
    override fun onActivityResult(requestCode: Int,resultCode: Int,data: Intent?){
        super.onActivityResult(requestCode,resultCode,data)
        if(requestCode!=IMPORT_REQUEST||resultCode!=RESULT_OK)return
        val uri=data?.data?:return
        try{if(data.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION!=0)contentResolver.takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION)}catch(_: SecurityException){/* Immediate worker copy still uses the granted transient read. */}
        controller.importDocument(uri);showScreen("library")
    }
    private fun renderSettings() {
        panel.removeAllViews();val state=controller.state;val requested=state.requested
        text("画面设置",22f);text("请求值会保存在本机。实际生效值在渲染完成后显示。",14f)
        effectiveSettings=TextView(this).apply{text=effectiveText(state.effective);textSize=14f;setPadding(6,12,6,8);accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        panel.addView(effectiveSettings)
        text("最长渲染边（128–1280）")
        val edge=EditText(this).apply{inputType=android.text.InputType.TYPE_CLASS_NUMBER;setText(requested.maximumInternalEdge.toString())};panel.addView(edge)
        text("质量阈值（1–12；数值越小细节越多）")
        val quality=EditText(this).apply{inputType=android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL;setText(requested.runtime.getJSONObject("lod").getDouble("qualityPixels").toString())};panel.addView(quality)
        button("保存并应用",!state.busy){try {
            val longest=edge.text.toString().toInt().also{require(it in 128..1280)}
            val q=quality.text.toString().toDouble().also{require(it.isFinite()&&it in 1.0..12.0)}
            val runtime=JSONObject(requested.runtime.toString());runtime.getJSONObject("lod").put("qualityPixels",q)
            controller.applySettings(requested.copy(runtime=runtime,maximumInternalEdge=longest,
                maximumInternalPixels=minOf(921600L,longest.toLong()*longest),generation=Math.addExact(requested.generation,1),configHash=""))
        }catch(_: Exception){toast("请输入范围内的数值")}}
        text("当前采用保守移动端参数。设备能力不足时会暂停或保留上一幅完整画面；可手动重试。",14f)
    }
    private fun effectiveText(config: ViewerConfig?)="实际生效：${config?.let{"最长边 ${it.maximumInternalEdge}，质量阈值 ${it.runtime.optJSONObject("lod")?.optDouble("qualityPixels")}"}?:"尚未确认"}"
    private fun renderReport(){panel.removeAllViews();text("报告与截图",22f)
        text("报告保存在本机，仅在你选择分享后交给其他应用。默认省略场景源文件、文件名和私有路径。",14f)
        button("生成诊断报告"){controller.exportDiagnostics{offerShare(it,"诊断报告")}}
        button("截取三维画面并生成报告"){showScreen("viewer");controller.requestScreenshot{offerShare(it,"截图报告")}}
        button("查看高级状态"){AlertDialog.Builder(this).setTitle("高级状态").setMessage(controller.state.diagnostics.toString(2).take(12000)).setPositiveButton("关闭",null).show()}
    }
    private fun offerShare(file: File,label: String){if(isFinishing||isDestroyed)return
        confirm("$label 已生成","是否选择其他应用进行分享？","选择应用"){
            try {
                val uri=FileProvider.getUriForFile(this,"$packageName.files",file)
                val send=Intent(Intent.ACTION_SEND).setType(if(file.extension=="monsterlod")"application/octet-stream" else "application/zip")
                    .putExtra(Intent.EXTRA_STREAM,uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    .apply{clipData=ClipData.newUri(contentResolver,label,uri)}
                startActivity(Intent.createChooser(send,"分享$label"))
            }catch(_: RuntimeException){toast("无法打开分享应用，文件仍保存在本机")}
        }
    }
    private fun showControl(result: NativeResult){toast(if(result.ok)"操作已提交，请等待任务状态更新" else when(result.code){"build_busy"->"已有任务正在释放资源，请稍后重试";"foreground_start_rejected"->"系统暂不允许后台构建，请返回前台后重试";"visible_activity_required"->"请保持本页可见，再点击开始";else->"操作未完成，请稍后重试"});controller.refresh()}
    private fun toast(value: String)=Toast.makeText(this,value,Toast.LENGTH_LONG).show()
    private fun stageName(stage: BuildStage)=when(stage){BuildStage.Created->"等待开始";BuildStage.Copying->"复制中";BuildStage.Normalizing->"解析中";BuildStage.Partitioning->"分区中";BuildStage.BuildingLeaves->"构建细节";BuildStage.Reducing->"生成层级";BuildStage.Validating->"检查结果";BuildStage.Publishing->"保存场景";BuildStage.Completed->"已完成";BuildStage.Paused->"已暂停";BuildStage.Cancelled->"已取消，进度保留";BuildStage.Failed->"未完成";BuildStage.WaitingForResources->"等待可用资源"}
    companion object{private const val IMPORT_REQUEST=10;private const val NOTIFICATION_REQUEST=11
        private val RETIRED=setOf(BuildStage.Paused,BuildStage.Cancelled,BuildStage.Failed,BuildStage.Completed,BuildStage.WaitingForResources)}
}
