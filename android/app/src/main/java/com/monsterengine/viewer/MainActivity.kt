package com.monsterengine.viewer

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.io.File

/** Local-only library/viewer/task/settings navigation. File and native work is
 * delegated to ViewerController; visible user actions alone start build/share. */
class MainActivity : Activity() {
    private lateinit var controller: ViewerController
    private lateinit var surface: ViewerSurfaceView
    private lateinit var status: TextView
    private lateinit var statusPanel: LinearLayout
    private lateinit var root: FrameLayout
    private lateinit var content: FrameLayout
    private lateinit var navigation: LinearLayout
    private lateinit var navigationSpacer: View
    private lateinit var navigationSettings: Button
    private lateinit var viewerChrome: FrameLayout
    private lateinit var library: GuiScenePanel
    private lateinit var tasks: GuiTaskPanel
    private lateinit var settings: GuiSettingsPanel
    private lateinit var overlay: LinearLayout
    private lateinit var retryButton: Button
    private lateinit var fpsOverlay: TextView
    private val tabs = linkedMapOf<String, Button>()
    private var screen = "viewer"
    private var temporary: String? = null
    private var pendingJob: String? = null
    private var resumed = false
    private val main = Handler(Looper.getMainLooper())
    private val taskPoll = object : Runnable {
        override fun run() {
            if (!resumed) return
            if (screen == "builds") { tasks.render(controller.state); controller.refresh() }
            main.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        screen = (savedInstanceState?.getString("screen") ?: intent.getStringExtra("screen"))
            ?.takeIf { it in listOf("viewer", "library", "builds") } ?: "viewer"
        temporary = savedInstanceState?.getString("temporary")
        pendingJob = savedInstanceState?.getString("pendingJob")?.takeIf { retainedController != null }
        controller = retainedController ?: ViewerController(applicationContext).also { retainedController = it }
        root = FrameLayout(this).apply { setBackgroundColor(GuiStyle.BACKGROUND) }
        content = FrameLayout(this)
        root.addView(content, FrameLayout.LayoutParams(-1,-1))
        surface = ViewerSurfaceView(this,controller)
        content.addView(surface,FrameLayout.LayoutParams(-1,-1))
        viewerChrome = FrameLayout(this)
        status = GuiStyle.body(this,"").apply { accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE }
        statusPanel = LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL
            background=GuiStyle.background(this@MainActivity,0xDD11171E.toInt())
            setPadding(dp(12),dp(8),dp(12),dp(4));addView(status)
            retryButton=GuiStyle.button(this@MainActivity,"重试画面") { controller.retry();surface.retrySurface() }
            addView(retryButton)
        }
        viewerChrome.addView(statusPanel,FrameLayout.LayoutParams(-1,-2).apply { topMargin=dp(12);leftMargin=dp(12);rightMargin=dp(88) })
        val tools=LinearLayout(this).apply { background=GuiStyle.background(this@MainActivity,0xEE1B252F.toInt());setPadding(dp(4),dp(2),dp(4),dp(4)) }
        for((icon,label,action) in listOf(
            Triple(R.drawable.ic_gui_reset,"复位",{surface.resetCamera()}),
            Triple(R.drawable.ic_gui_quality,"画质",{showPanel("quality")}),
            Triple(R.drawable.ic_gui_screenshot,"截图报告",{screenshot()})))
            tools.addView(GuiStyle.iconButton(this,icon,label,action),LinearLayout.LayoutParams(0,-2,1f))
        viewerChrome.addView(tools,FrameLayout.LayoutParams(dp(252),-2,Gravity.BOTTOM or Gravity.END).apply { bottomMargin=dp(16);rightMargin=dp(16) })
        fpsOverlay=GuiStyle.body(this,"FPS --").apply {
            visibility=View.GONE;textSize=14f;setTextColor(GuiStyle.TEXT)
            setPadding(dp(8),dp(4),dp(8),dp(4))
            background=GuiStyle.background(this@MainActivity,0xBB11171E.toInt())
        }
        viewerChrome.addView(fpsOverlay,FrameLayout.LayoutParams(-2,-2,Gravity.TOP or Gravity.END).apply {
            topMargin=dp(12);rightMargin=dp(12)
        })
        content.addView(viewerChrome,FrameLayout.LayoutParams(-1,-1))
        library=GuiScenePanel(this,GuiScenePanel.Actions(::pickImport,
            {controller.openBuiltin();showScreen("viewer")},{controller.openScene(it);showScreen("viewer")},
            {controller.exportScene(it){file->offerShare(file,"场景包")}},controller::deleteScene,
            {controller.createBuild{showScreen("builds");startJob(it)}},controller::discardPendingImport,controller::cancelImport)).apply { isClickable=true }
        tasks=GuiTaskPanel(this,GuiTaskPanel.Actions(controller::refresh,::startJob,
            {id,action->LodBuildService.control(this,id,action).also{controller.refresh()}},
            {controller.openScene(it);showScreen("viewer")},controller::cleanupJob)).apply { isClickable=true }
        content.addView(library,FrameLayout.LayoutParams(-1,-1));content.addView(tasks,FrameLayout.LayoutParams(-1,-1))
        settings=GuiSettingsPanel(this,GuiSettingsPanel.Actions(controller::applySettings,::applyHighQuality,
            {showPanel("report")},{controller.exportDiagnostics{offerShare(it,"诊断报告")}},::screenshot,::advanced))
        overlay=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL;setBackgroundColor(GuiStyle.BACKGROUND);isClickable=true;isFocusable=true;visibility=View.GONE
            addView(GuiStyle.outlinedButton(this@MainActivity,"关闭面板"){closePanel()})
            addView(settings,LinearLayout.LayoutParams(-1,0,1f))
        }
        content.addView(overlay,FrameLayout.LayoutParams(-1,-1))
        navigation=LinearLayout(this).apply{setBackgroundColor(GuiStyle.BACKGROUND)}
        for((key,label) in listOf("viewer" to "浏览","library" to "场景","builds" to "任务")) {
            val button=GuiStyle.outlinedButton(this,label){showScreen(key)};tabs[key]=button;navigation.addView(button)
        }
        navigationSpacer=View(this).apply { importantForAccessibility=View.IMPORTANT_FOR_ACCESSIBILITY_NO }
        navigation.addView(navigationSpacer,LinearLayout.LayoutParams(0,0))
        navigationSettings=GuiStyle.iconButton(this,R.drawable.ic_gui_settings,"设置"){showPanel("settings")}.apply {
            text="";tooltipText="设置";compoundDrawablePadding=0
            foreground=compoundDrawables[1]?.apply { setTint(GuiStyle.MUTED) }
            foregroundGravity=Gravity.CENTER
            setCompoundDrawables(null,null,null,null)
            minHeight=dp(48);minimumHeight=dp(48)
            setPadding(dp(12),dp(12),dp(12),dp(12))
            val states=StateListDrawable().apply {
                addState(intArrayOf(android.R.attr.state_focused),GuiStyle.background(this@MainActivity,GuiStyle.SURFACE))
                addState(intArrayOf(),GuiStyle.background(this@MainActivity,Color.TRANSPARENT))
            }
            background=RippleDrawable(ColorStateList.valueOf(0x3370D8FF),states,GuiStyle.background(this@MainActivity,Color.WHITE))
        }
        navigation.addView(navigationSettings,LinearLayout.LayoutParams(dp(64),dp(64)))
        root.addView(navigation)
        root.addOnLayoutChangeListener { _,_,_,_,_,_,_,_,_->layoutShell() }
        ViewCompat.setOnApplyWindowInsetsListener(root){view,insets->
            val safe=insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime())
            view.setPadding(safe.left,safe.top,safe.right,safe.bottom);insets
        }
        setContentView(root);ViewCompat.requestApplyInsets(root)
        controller.onState=::render
        showScreen(screen,false);temporary?.let(::showPanel);controller.initialize()
    }
    override fun onResume(){super.onResume();resumed=true;controller.setResumed(true);main.removeCallbacks(taskPoll);main.post(taskPoll)}
    override fun onPause(){resumed=false;main.removeCallbacks(taskPoll);controller.setResumed(false);super.onPause()}
    override fun onDestroy(){
        main.removeCallbacksAndMessages(null)
        if(::controller.isInitialized){
            if(isChangingConfigurations||!isFinishing)controller.detachActivity()
            else{retainedController=null;controller.close()}
        }
        super.onDestroy()
    }
    override fun onTrimMemory(level: Int){super.onTrimMemory(level);if(::controller.isInitialized)controller.trim(level)}
    override fun onSaveInstanceState(outState: Bundle){outState.putString("screen",screen);outState.putString("temporary",temporary);outState.putString("pendingJob",pendingJob);super.onSaveInstanceState(outState)}
    override fun onNewIntent(intent: Intent){super.onNewIntent(intent);setIntent(intent);if(intent.getStringExtra("screen")=="builds")showScreen("builds")}

    private fun dp(value: Int)=GuiStyle.dp(this,value)
    /** Only actual window/inset changes resize the permanently mounted canvas. */
    private fun layoutShell(){
        val w=root.width-root.paddingLeft-root.paddingRight;if(w<=0)return
        val wide=w>=dp(600);val rail=dp(92);val bottom=maxOf(dp(64),(dp(64)*resources.configuration.fontScale).toInt())
        navigation.orientation=if(wide)LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
        frameParams(navigation,FrameLayout.LayoutParams(if(wide)rail else -1,if(wide)-1 else bottom,if(wide)Gravity.START else Gravity.BOTTOM))
        frameParams(content,FrameLayout.LayoutParams(-1,-1).apply{if(wide)leftMargin=rail else bottomMargin=bottom})
        tabs.values.forEach{button->
            val old=button.layoutParams as LinearLayout.LayoutParams
            val width=if(wide)-1 else 0;val height=if(wide)-2 else -1;val weight=if(wide)0f else 1f
            if(old.width!=width||old.height!=height||old.weight!=weight)button.layoutParams=LinearLayout.LayoutParams(width,height,weight).apply{setMargins(dp(4),dp(4),dp(4),dp(4))}
        }
        navigationSpacer.visibility=if(wide)View.VISIBLE else View.GONE
        val spacerParams=navigationSpacer.layoutParams as LinearLayout.LayoutParams
        val spacerWeight=if(wide)1f else 0f
        if(spacerParams.weight!=spacerWeight)navigationSpacer.layoutParams=LinearLayout.LayoutParams(0,0,spacerWeight)
        val settingsParams=navigationSettings.layoutParams as LinearLayout.LayoutParams
        val settingsHeight=if(wide)dp(64) else -1
        if(settingsParams.height!=settingsHeight||settingsParams.gravity!=Gravity.CENTER_HORIZONTAL||settingsParams.leftMargin!=dp(4))
            navigationSettings.layoutParams=LinearLayout.LayoutParams(dp(64),settingsHeight).apply {
                gravity=Gravity.CENTER_HORIZONTAL;setMargins(dp(4),dp(4),dp(4),dp(4))
            }
        frameParams(overlay,FrameLayout.LayoutParams(if(wide)minOf(dp(480),w-rail) else -1,-1,Gravity.END))
    }
    private fun frameParams(view: View,next: FrameLayout.LayoutParams){
        val old=view.layoutParams as? FrameLayout.LayoutParams
        if(old==null||old.width!=next.width||old.height!=next.height||old.gravity!=next.gravity||old.leftMargin!=next.leftMargin||old.bottomMargin!=next.bottomMargin)view.layoutParams=next
    }
    private fun render(state: ViewerController.State){
        surface.refreshRequestedExtent()
        statusPanel.visibility=if(state.presentation==ViewerPresentation.READY)View.GONE else View.VISIBLE
        status.text=when(state.presentation){
            ViewerPresentation.READY->"就绪 · 画面持续更新"
            ViewerPresentation.PAUSED->"画面已暂停 · ${state.message}"
            ViewerPresentation.PREPARING->if(state.busy)state.message else "正在确认画面 · ${state.message}"
            else->state.message
        }
        retryButton.visibility=if(state.retryAvailable)View.VISIBLE else View.GONE
        retryButton.isEnabled=!state.busy&&!state.detachPending&&!state.buildBlocked
        if(BuildConfig.FPS_OVERLAY_ENABLED)updateFpsOverlay(state)
        if(screen=="library")library.render(state)
        if(screen=="builds")tasks.render(state)
        if(temporary!=null)settings.render(state)
    }
    private fun showScreen(next: String,closeTemporary: Boolean=true){
        if(closeTemporary)closePanel()
        screen=next;viewerChrome.visibility=if(next=="viewer")View.VISIBLE else View.GONE
        library.visibility=if(next=="library")View.VISIBLE else View.GONE;tasks.visibility=if(next=="builds")View.VISIBLE else View.GONE
        surface.setCameraInputEnabled(next=="viewer"&&temporary==null)
        tabs.forEach{(key,button)->val label=when(key){"library"->"场景";"builds"->"任务";else->"浏览"}
            button.isSelected=key==next;button.text=if(key==next)"$label · 当前" else label;button.contentDescription=button.text
            button.setTextColor(if(key==next)GuiStyle.ACCENT else GuiStyle.TEXT)}
        render(controller.state);if(next=="builds")controller.refresh()
    }
    private fun showPanel(kind: String){temporary=kind;overlay.visibility=View.VISIBLE
        if(kind=="report")settings.showReports() else settings.showSettings()
        settings.render(controller.state);surface.setCameraInputEnabled(false)}
    private fun closePanel(){temporary=null;overlay.visibility=View.GONE;surface.setCameraInputEnabled(screen=="viewer")
        currentFocus?.let{(getSystemService(android.content.Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager).hideSoftInputFromWindow(it.windowToken,0);it.clearFocus()}}
    @Deprecated("Framework back navigation")
    override fun onBackPressed(){when{temporary=="report"->showPanel("settings");temporary!=null->closePanel();screen!="viewer"->showScreen("viewer");else->super.onBackPressed()}}
    private fun pickImport(){startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION),IMPORT_REQUEST)}
    private fun confirm(title: String,message: String,positive: String="确认",action: ()->Unit)=GuiStyle.confirm(this,title,message,positive,action)
    private fun startJob(id: String) {
        if(pendingJob!=null||controller.state.busy){tasks.onStartResult(id,NativeResult(false,"build_busy"));return}
        controller.prepareBuild{released->
            if(isFinishing||isDestroyed||!resumed){tasks.onStartResult(id,NativeResult(false,"visible_activity_required"));return@prepareBuild}
            if(!released.ok){tasks.onStartResult(id,released);toast("画面尚未释放，请稍后重试");return@prepareBuild}
            if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){
                pendingJob=id;requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),NOTIFICATION_REQUEST)
            }else launchBuild(id)
        }
    }
    private fun launchBuild(id: String){val result=LodBuildService.startFromVisibleActivity(this,id);tasks.onStartResult(id,result);showControl(result);showScreen("builds");controller.refresh()}
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
    private fun applyHighQuality(){try {controller.applySettings(ViewerConfig.highQualityPreset(controller.state.requested))}catch(_: Exception){toast("暂时无法应用最高画质，请稍后重试")}}
    private fun updateFpsOverlay(state: ViewerController.State) {
        fpsOverlay.text=state.observedFps?.let { "FPS %.1f".format(it) } ?: "FPS --"
        fpsOverlay.visibility=View.VISIBLE
    }
    private fun screenshot(){confirm("生成三维截图报告？","将截取三维画面并生成本机报告包，完成后可选择分享或取消。","生成报告"){controller.requestScreenshot{offerShare(it,"截图报告")}}}
    private fun advanced(){AlertDialog.Builder(this,R.style.Theme_MonsterEngine_Dialog).setTitle("高级状态").setMessage(controller.state.diagnostics.toString(2).take(12000)).setPositiveButton("关闭",null).show()}
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
    companion object{private const val IMPORT_REQUEST=10;private const val NOTIFICATION_REQUEST=11
        private var retainedController: ViewerController?=null
    }
}
