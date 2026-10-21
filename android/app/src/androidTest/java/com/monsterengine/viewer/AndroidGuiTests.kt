package com.monsterengine.viewer

import android.app.Instrumentation
import android.content.Intent
import android.graphics.Rect
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import java.io.File

/** GUI behavior through production Activity and actual Views; no renderer replacement. */
object AndroidGuiTests {
    fun run(i: Instrumentation, run: (String, () -> Unit) -> Unit, cleanupPreviousGuiJob: String? = null, pauseOnly: Boolean = false) {
        val settingsFile = File(i.targetContext.filesDir, "viewer-settings.json")
        val originalSettings = settingsFile.takeIf { it.isFile }?.readBytes()
        var selectionChanged = false
        fun ui(body: () -> Unit) {
            var failure: Throwable? = null
            i.runOnMainSync { try { body() } catch (error: Throwable) { failure = error } }
            failure?.let { throw it }
        }
        fun views(root: View): List<View> = listOf(root) +
            if (root is ViewGroup) (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
        fun click(root: View, label: String) {
            val button = views(root).filterIsInstance<Button>().first { it.isShown &&
                (it.text.toString() == label || it.contentDescription?.toString() == label) }
            check(button.isEnabled) { "Disabled action: $label" }; check(button.performClick())
        }
        fun has(root: View, text: String) = views(root).filterIsInstance<TextView>().any { it.isShown && it.text.contains(text) }
        fun capture(name: String) {
            i.waitForIdleSync()
            val drawn = java.util.concurrent.CountDownLatch(1)
            i.runOnMainSync {
                android.view.Choreographer.getInstance().postFrameCallback {
                    android.view.Choreographer.getInstance().postFrameCallback { drawn.countDown() }
                }
            }
            check(drawn.await(5, java.util.concurrent.TimeUnit.SECONDS)) { "No UI frame before screenshot" }
            Thread.sleep(100) // Allow the submitted HWUI frame to reach the compositor.
            val bitmap = checkNotNull(i.uiAutomation.takeScreenshot())
            val directory = File(i.targetContext.filesDir, "gui-verification-20261021-075317-slfq").apply { mkdirs() }
            File(directory, "$name.png").outputStream().use { check(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.recycle()
        }
        fun systemClick(label: String) {
            val deadline = System.currentTimeMillis() + 10000
            while (System.currentTimeMillis() < deadline) {
                var node = i.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(label)
                    ?.firstOrNull { it.text?.toString() == label || it.contentDescription?.toString() == label }
                repeat(6) { if (node != null && !node!!.isClickable) node = node!!.parent }
                if (node?.isEnabled == true && node!!.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK)) return
                Thread.sleep(100)
            }
            error("System/dialog action not available: $label")
        }

        run("gui_numeric_validation_retains_drafts_and_valid_boundaries_submit_copied_request") {
            ui {
                var applied: ViewerConfig? = null
                val panel = GuiSettingsPanel(i.targetContext, GuiSettingsPanel.Actions({ applied = it }, {}, {}, {}, {}, {}))
                val base = ViewerConfig()
                panel.render(ViewerController.State(busy = false, requested = base))
                val fields = views(panel).filterIsInstance<EditText>()
                val edge = fields.first { it.contentDescription == "最长渲染边" }
                val quality = fields.first { it.contentDescription == "质量阈值" }
                val save = views(panel).filterIsInstance<Button>().first { it.text == "保存并应用请求" }
                for ((a, b) in listOf("127" to "0.1", "4097" to "1", "x" to "1", "128" to "NaN", "128" to "Infinity", "128" to "0.09", "128" to "100.1")) {
                    edge.setText(a); quality.setText(b)
                    val enteredEdge = edge.text.toString(); val enteredQuality = quality.text.toString()
                    save.performClick()
                    check(applied == null && (edge.error != null || quality.error != null))
                    panel.render(ViewerController.State(busy = false, requested = base.copy(generation = 5)))
                    check(edge.text.toString() == enteredEdge && quality.text.toString() == enteredQuality)
                }
                for ((a, b) in listOf("128" to "0.1", "4096" to "100")) {
                    edge.setText(a); quality.setText(b); save.performClick()
                    val request = checkNotNull(applied)
                    check(request.maximumInternalEdge == a.toInt())
                    check(request.runtime.getJSONObject("lod").getDouble("qualityPixels") == b.toDouble())
                    check(request.generation == 6L)
                    check(request.runtime.getJSONObject("streaming").toString() == base.runtime.getJSONObject("streaming").toString())
                }
            }
        }

        var activity: MainActivity? = null
        val provider = android.net.Uri.parse("content://com.monsterengine.viewer.debug.test.documents")
        val packageName = "gui-20261021-075317-slfq.monsterlod"
        // A filtered GUI-only run must start the reinstalled test provider too;
        // the ordinary full suite already does this through its native import cases.
        i.targetContext.contentResolver.call(provider,"grant-test-document","small.ply",null)
        if(cleanupPreviousGuiJob!=null)run("gui_fixture_cleanup_only_identified_previous_attempt_retired_job") {
            val store=BuildJobStore(i.targetContext);val job=store.read(cleanupPreviousGuiJob)
            val expected=VerificationDocumentsProvider.header(160000)+"0 0 0 0 0 0 1 -3 -3 -3 1 0 0 0\n".repeat(160000)
            check(File(job.stableSource).readText()==expected && job.progress.stage==BuildStage.Paused)
            check(!LodBuildService.hasActiveJob())
            store.cleanupRetired(job.id,removeSource=true)
        }
        run("gui_prepare_real_native_validated_package_for_system_picker") {
            val repository = SceneRepository(i.targetContext)
            val source = File(repository.buildInputs, "gui-${java.util.UUID.randomUUID()}.ply")
            val job = File(i.targetContext.filesDir, "build-jobs/${java.util.UUID.randomUUID()}").apply { check(mkdirs()) }
            val output = repository.createStaging().apply { check(delete()) }
            var published: SceneDescriptor? = null
            var exported: File? = null
            try {
                source.writeText(VerificationDocumentsProvider.header(1) + "0 0 0 0 0 0 1 -3 -3 -3 1 0 0 0\n")
                val build = NativeBridge.createBuild(source.path, File(job,"journal").path, output.path, BoundedBuildConfig())
                check(build.isValid) { NativeBridge.takeBuildCreationFailure().toString() }
                try { check(NativeBridge.runBuild(build).ok) } finally { check(NativeBridge.destroyBuild(build).ok) }
                published = repository.publish(output, ViewerSceneKind.LOD, SceneRepository.sha256(File(output,"scene.mgs")))
                exported = ScenePackage(repository).exportPackage(published.sceneId)
                val result = i.targetContext.contentResolver.call(provider,"install-gui-package",packageName,
                    android.os.Bundle().apply { putByteArray("bytes",exported.readBytes()) })
                check(result?.getBoolean("ok") == true)
            } finally {
                exported?.delete(); published?.let { check(repository.requestDelete(it.sceneId)) }
                source.delete(); job.deleteRecursively(); output.deleteRecursively()
            }
        }
        run("gui_real_activity_launch_and_three_selected_destinations") {
            activity = i.startActivitySync(Intent(i.targetContext, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)) as MainActivity
            i.waitForIdleSync()
            ui {
                val root = checkNotNull(activity).window.decorView
                for (label in listOf("场景", "任务", "浏览")) {
                    val button = views(root).filterIsInstance<Button>().first { it.contentDescription?.toString()?.startsWith(label) == true && it.parent is android.widget.LinearLayout && (it.text == label || it.text == "$label · 当前") }
                    button.performClick()
                    val tabs = views(root).filterIsInstance<Button>().filter { it.text in listOf("场景", "任务", "浏览", "场景 · 当前", "任务 · 当前", "浏览 · 当前") }
                    check(tabs.size == 3 && tabs.count { it.isSelected } == 1 && button.isSelected)
                }
            }
        }
        run("gui_quality_settings_reports_back_preserves_texture_attachment_extent_and_draft") {
            val a = checkNotNull(activity)
            ui { click(a.window.decorView, "画质") }
            val deadline = System.currentTimeMillis() + 90000
            var available = false
            while (!available && System.currentTimeMillis() < deadline) {
                ui { available = views(a.window.decorView).filterIsInstance<Button>().any { it.text == "保存并应用请求" && it.isShown && it.isEnabled } }
                if (!available) Thread.sleep(250)
            }
            check(available) { "Controller remained busy: quality submission unavailable for 90s" }
            ui {
                val root = a.window.decorView
                val surface = views(root).filterIsInstance<ViewerSurfaceView>().single()
                val parent = surface.parent; val texture = surface.surfaceTexture
                val bounds = Rect(surface.left, surface.top, surface.right, surface.bottom)
                val edge = views(root).filterIsInstance<EditText>().first { it.contentDescription == "最长渲染边" }
                edge.setText("127"); click(root, "保存并应用请求")
                check(edge.error != null)
                a.onBackPressed(); check(!edge.isShown)
                click(root, "设置"); check(edge.isShown && edge.text.toString() == "127" && edge.error != null)
                click(root, "报告与高级状态"); check(has(root, "只有明确选择接收应用"))
                a.onBackPressed(); check(edge.isShown)
                a.onBackPressed(); check(!edge.isShown)
                check(surface.parent === parent && surface.surfaceTexture === texture)
                check(Rect(surface.left, surface.top, surface.right, surface.bottom) == bounds)
            }
            i.waitForIdleSync()
        }
        run("gui_task_notification_intent_selects_tasks_without_implicit_start") {
            val before = LodBuildService.guiRunState()
            i.targetContext.startActivity(Intent(i.targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP).putExtra("screen", "builds"))
            i.waitForIdleSync()
            val end=System.currentTimeMillis()+10000;var selected=false
            while(!selected&&System.currentTimeMillis()<end){ui{selected=has(checkNotNull(activity).window.decorView,"任务 · 当前")};if(!selected)Thread.sleep(100)}
            check(selected){"Notification intent did not select task destination within10s"}
            check(LodBuildService.guiRunState() == before)
            ui { checkNotNull(activity).onBackPressed() }
        }
        fun currentState(): ViewerController.State {
            var state: ViewerController.State? = null
            ui { state = (MainActivity::class.java.getDeclaredField("controller").apply { isAccessible = true }
                .get(checkNotNull(activity)) as ViewerController).state }
            return checkNotNull(state)
        }
        fun awaitState(reason: String, timeout: Long = 30000, condition: (ViewerController.State) -> Boolean): ViewerController.State {
            val end = System.currentTimeMillis() + timeout
            while (System.currentTimeMillis() < end) {
                if (i.uiAutomation.rootInActiveWindow?.packageName?.toString()?.contains("permissioncontroller") == true &&
                    i.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText("不允许")?.isNotEmpty()==true) systemClick("不允许")
                val state = currentState(); if (condition(state)) return state
                Thread.sleep(100)
            }
            val state=currentState()
            File(i.targetContext.filesDir,"gui-verification-20261021-075317-slfq/await-state-timeout.json").apply{parentFile?.mkdirs()}.writeText(
                org.json.JSONObject().put("scenario",reason).put("message",state.message).put("presentation",state.presentation.name)
                    .put("retryAvailable",state.retryAvailable).put("diagnostics",state.diagnostics).toString(2))
            error("Timeout: $reason; ${state.message}")
        }
        fun pickDocument(name: String) {
            ui { click(checkNotNull(activity).window.decorView,"导入 PLY 或场景包") }
            i.waitForIdleSync(); Thread.sleep(300)
            val files = i.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(name).orEmpty()
            if (files.none { it.text?.toString() == name }) {
                systemClick("显示根目录"); systemClick("Monster Verification")
            }
            systemClick(name)
        }
        run("gui_actual_saf_picker_cancel_malformed_copy_cancel_and_valid_ply_preserve_existing_scenes") {
            val a = checkNotNull(activity)
            ui { views(a.window.decorView).filterIsInstance<Button>().first { it.text == "场景" }.performClick() }
            val before = awaitState("library available") { !it.busy }.scenes.map { it.sceneId }.toSet()
            val repository = SceneRepository(i.targetContext)
            try {
                ui { click(a.window.decorView,"导入 PLY 或场景包") }; capture("saf-picker")
                i.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
                check(awaitState("picker cancellation") { !it.busy }.scenes.map { it.sceneId }.toSet() == before)
                pickDocument("malformed.ply")
                check(awaitState("malformed rejection") { !it.busy && it.message.startsWith("无法导入") }.scenes.map { it.sceneId }.toSet() == before)
                pickDocument("slow.ply")
                awaitState("active copy") { it.importInProgress }
                ui { click(a.window.decorView,"取消导入") }
                check(awaitState("copy cancellation") { !it.busy && !it.importInProgress && it.message.contains("取消") }.scenes.map { it.sceneId }.toSet() == before)
                pickDocument("small.ply")
                val imported = awaitState("valid PLY publication") { !it.busy && it.scenes.any { scene -> scene.sceneId !in before } }
                val scene = imported.scenes.single { it.sceneId !in before }
                check(scene.kind == ViewerSceneKind.PLY && NativeBridge.validateScene(scene.sourcePLY).ok)
                val label = "场景 ${imported.scenes.indexOf(scene)+1} 更多操作"
                ui { click(a.window.decorView,label) }; systemClick("删除场景"); systemClick("取消")
                check(repository.list().any { it.sceneId == scene.sceneId })
                ui { click(a.window.decorView,label) }; systemClick("删除场景"); systemClick("删除场景")
                awaitState("confirmed isolated scene deletion") { it.scenes.none { scene -> scene.sceneId !in before } }
                capture("scenes-after-saf")
            } finally {
                repository.list().filter { it.sceneId !in before }.forEach { check(repository.requestDelete(it.sceneId)) }
            }
        }
        run("gui_actual_saf_native_package_import_export_and_cancelled_delete") {
            val a = checkNotNull(activity); val repository = SceneRepository(i.targetContext)
            val before = currentState().scenes.map { it.sceneId }.toSet()
            val exports = File(i.targetContext.filesDir,"exports"); val previousExports = exports.listFiles()?.map { it.name }?.toSet().orEmpty()
            try {
                pickDocument(packageName)
                val imported = awaitState("valid package publication") { !it.busy && it.scenes.any { scene -> scene.sceneId !in before } }
                val scene = imported.scenes.single { it.sceneId !in before }
                check(scene.kind == ViewerSceneKind.LOD && NativeBridge.validateScene(scene.directory).ok)
                val label = "场景 ${imported.scenes.indexOf(scene)+1} 更多操作"
                ui { click(a.window.decorView,label) }; systemClick("导出场景包")
                systemClick("取消")
                val exported = checkNotNull(exports.listFiles()?.singleOrNull { it.name !in previousExports && it.extension == "monsterlod" })
                java.util.zip.ZipFile(exported).use { check(it.getEntry("package.json") != null && it.getEntry("scene.mgs") != null) }
                ui { click(a.window.decorView,label) }; systemClick("删除场景"); systemClick("取消")
                check(repository.list().any { it.sceneId == scene.sceneId })
                ui { click(a.window.decorView,label) }; systemClick("删除场景"); systemClick("删除场景")
                awaitState("isolated package deletion") { it.scenes.none { scene -> scene.sceneId !in before } }
            } finally {
                repository.list().filter { it.sceneId !in before }.forEach { check(repository.requestDelete(it.sceneId)) }
                exports.listFiles()?.filter { it.name !in previousExports }?.forEach { it.delete() }
            }
        }
        val ownedTaskScenes = mutableSetOf<String>()
        val ownedTaskJobs = mutableSetOf<String>()
        run(if(pauseOnly) "gui_large_pending_explicit_task_retained_pause_then_viewer_reopen" else "gui_large_pending_explicit_task_pause_resume_cancel_publish_and_retired_cleanup") {
            val a = checkNotNull(activity)
            ui { views(a.window.decorView).filterIsInstance<Button>().first { it.text=="场景"||it.text=="场景 · 当前" }.performClick() }
            val originalJobs = currentState().jobs.map { it.id }.toSet()
            fun taskClick(id: String, label: String) {
                val end=System.currentTimeMillis()+10000
                while(System.currentTimeMillis()<end) {
                    var ready=false
                    ui {
                        val title=views(a.window.decorView).filterIsInstance<TextView>().firstOrNull{it.text=="任务 ${id.take(8)}"}
                        ready=title?.let{views(it.parent as View).filterIsInstance<Button>().any{button->button.text==label&&button.isEnabled}}==true
                        if(ready)click(title!!.parent as View,label)
                    }
                    if(ready)return
                    Thread.sleep(100)
                }
                error("Task action remained unavailable for10s: $label")
            }
            pickDocument("slow.ply")
            val pending = awaitState("large input pending build", 45000) { !it.busy && it.pendingImport != null }
            check(pending.jobs.map { it.id }.toSet() == originalJobs && LodBuildService.guiRunState() == null)
            ui { check(has(a.window.decorView,"构建期间暂停浏览")) }; capture("large-input-pending-explicit-build")
            ui { click(a.window.decorView,"创建并开始构建") }; systemClick("开始构建")
            val job = awaitState("explicit job creation", 30000) { it.jobs.any { job -> job.id !in originalJobs } }.jobs.single { it.id !in originalJobs }
            ownedTaskJobs += job.id
            // Permission denial must not disable foreground task control. Only answer
            // a real system permission dialog; preserve any already-granted permission.
            val permissionEnd = System.currentTimeMillis()+5000
            while (System.currentTimeMillis()<permissionEnd && LodBuildService.guiRunState()==null) {
                val denied = i.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText("不允许").orEmpty()
                if (denied.isNotEmpty()) { systemClick("不允许"); break }; Thread.sleep(100)
            }
            awaitState("real build owner running",30000) { LodBuildService.guiRunState()?.jobId == job.id }
            taskClick(job.id,"暂停")
            ui { check(has(a.window.decorView,"尚未确认暂停") || has(a.window.decorView,"正在停止")) }
            val paused = awaitState("acknowledged pause",60000) { state -> state.jobs.any { it.id==job.id && it.progress.stage==BuildStage.Paused } && LodBuildService.guiRunState()==null }.jobs.first { it.id==job.id }
            check(File(paused.stableSource).isFile)
            capture("task-paused-acknowledged")
            if(pauseOnly){ui{views(a.window.decorView).filterIsInstance<Button>().first{it.text=="场景"}.performClick()};return@run}
            taskClick(job.id,"继续 / 重试")
            awaitState("explicit resumed worker",30000) { LodBuildService.guiRunState()?.jobId == job.id }
            taskClick(job.id,"取消并保留进度"); systemClick("取消并保留进度")
            val cancelled = awaitState("acknowledged cancellation",60000) { state -> state.jobs.any { it.id==job.id && it.progress.stage==BuildStage.Cancelled } && LodBuildService.guiRunState()==null }.jobs.first { it.id==job.id }
            check(File(cancelled.stableSource).isFile && cancelled.progress.checkpointGeneration >= paused.progress.checkpointGeneration)
            taskClick(job.id,"继续 / 重试")
            val completed = awaitState("explicit retry to published result",120000) { state -> state.jobs.any { it.id==job.id && it.progress.stage==BuildStage.Completed && it.publishedSceneId.isNotEmpty() } && LodBuildService.guiRunState()==null }.jobs.first { it.id==job.id }
            ownedTaskScenes += completed.publishedSceneId
            val repository = SceneRepository(i.targetContext)
            check(repository.list().any { it.sceneId==completed.publishedSceneId })
            selectionChanged = true
            taskClick(job.id,"打开已发布结果")
            awaitState("published task result renders",90000) { it.selectedSceneId==completed.publishedSceneId && it.presentation==ViewerPresentation.READY }
            capture("task-published-result-viewer")
            ui { views(a.window.decorView).filterIsInstance<Button>().first { it.text=="任务" }.performClick() }
            taskClick(job.id,"清理任务和临时源文件"); capture("task-cleanup-impact-confirmation"); systemClick("清理任务")
            awaitState("retired task cleanup",30000) { it.jobs.none { job -> job.id==completed.id } }
            check(repository.list().any { it.sceneId==completed.publishedSceneId })
            ui { views(a.window.decorView).filterIsInstance<Button>().first { it.text=="场景" }.performClick() }
        }
        run("gui_live_vulkan_progress_without_fps_overlay") {
            check(!BuildConfig.FPS_OVERLAY_ENABLED)
            // Existing unavailable historical selection must remain unchanged on launch.
            // Explicitly open the current builtin for this rendering scenario only.
            ui {
                val a = checkNotNull(activity)
                views(a.window.decorView).filterIsInstance<Button>().first { it.text == "场景" || it.text == "场景 · 当前" }.performClick()
                click(a.window.decorView, "打开内置示例")
                selectionChanged = true
            }
            // Read the actual controller's public observation, never the intentionally
            // size-truncated private diagnostic log. Reflection only locates the
            // Activity-owned controller; it does not change its state or native owner.
            val controller = MainActivity::class.java.getDeclaredField("controller").apply { isAccessible = true }
                .get(checkNotNull(activity)) as ViewerController
            val end = System.currentTimeMillis() + 90000
            var firstFrame = 0L; var firstSurface = 0L; var firstScene = 0L; var progressed = false
            while (System.currentTimeMillis() < end && !progressed) {
                var latest: org.json.JSONObject? = null
                ui { latest = controller.state.diagnostics.optJSONObject("diagnostics")?.optJSONObject("latest") }
                if (latest != null) {
                    val sample = checkNotNull(latest)
                    val frame = sample.optLong("frameId"); val scene = sample.optLong("sceneGeneration"); val surface = sample.optLong("surfaceGeneration")
                    if (firstFrame > 0 && frame > firstFrame && scene == firstScene && surface == firstSurface) progressed = true
                    if (frame > 0) { firstFrame = frame; firstScene = scene; firstSurface = surface }
                }
                if (!progressed) Thread.sleep(500)
            }
            check(progressed) { "No advancing actual Vulkan submissions in current scene/Surface within 90s" }
            ui {
                check(controller.state.presentation == ViewerPresentation.READY)
                val root = checkNotNull(activity).window.decorView
                check(!has(root, "就绪"))
                check(!has(root, "内置示例"))
            }
        }
        run("gui_actual_one_two_finger_double_tap_reset_and_panel_input_preserve_camera") {
            val a=checkNotNull(activity);var area=Rect()
            ui {val surface=views(a.window.decorView).filterIsInstance<ViewerSurfaceView>().single();check(surface.getGlobalVisibleRect(area))}
            val x=area.exactCenterX();val y=area.exactCenterY()
            fun touch(action:Int,points:List<Pair<Float,Float>>,down:Long){
                val properties=Array(points.size){index->android.view.MotionEvent.PointerProperties().apply{id=index;toolType=android.view.MotionEvent.TOOL_TYPE_FINGER}}
                val coordinates=Array(points.size){index->android.view.MotionEvent.PointerCoords().apply{this.x=points[index].first;this.y=points[index].second;pressure=1f;size=1f}}
                val event=android.view.MotionEvent.obtain(down,android.os.SystemClock.uptimeMillis(),action,points.size,properties,coordinates,0,0,1f,1f,0,0,android.view.InputDevice.SOURCE_TOUCHSCREEN,0)
                try{i.sendPointerSync(event)}finally{event.recycle()}
            }
            fun tap(px:Float,py:Float){val down=android.os.SystemClock.uptimeMillis();touch(android.view.MotionEvent.ACTION_DOWN,listOf(px to py),down);Thread.sleep(25);touch(android.view.MotionEvent.ACTION_UP,listOf(px to py),down)}
            fun tool(label:String){var rect=Rect();ui{val v=views(a.window.decorView).first{it.contentDescription?.toString()==label&&it.isShown};check(v.getGlobalVisibleRect(rect))};tap(rect.exactCenterX(),rect.exactCenterY())}
            fun sample():IntArray{
                i.waitForIdleSync();Thread.sleep(1000);val bitmap=checkNotNull(i.uiAutomation.takeScreenshot())
                try{return (0 until 32*24).map{index->bitmap.getPixel(area.left+area.width()/4+(index%32)*area.width()/64,area.top+area.height()/4+(index/32)*area.height()/48)}.toIntArray()}
                finally{bitmap.recycle()}
            }
            fun difference(a:IntArray,b:IntArray)=a.indices.map{index->listOf(16,8,0).sumOf{shift->kotlin.math.abs((a[index] shr shift and 255)-(b[index] shr shift and 255))}.toDouble()/3}.average()
            tool("复位");val initial=sample()
            val down=android.os.SystemClock.uptimeMillis();touch(0,listOf(x to y),down);Thread.sleep(50);touch(2,listOf((x+area.width()*.18f) to (y+area.height()*.08f)),down);touch(1,listOf((x+area.width()*.18f) to (y+area.height()*.08f)),down)
            val orbit=sample();check(difference(initial,orbit)>2){"One-finger orbit produced no visible scene change"}
            tap(x,y);Thread.sleep(100);tap(x,y);val reset=sample();check(difference(initial,reset)<difference(initial,orbit)){"Double-tap did not restore initial view"}
            val twoDown=android.os.SystemClock.uptimeMillis();val p1=listOf((x-80) to y,(x+80) to y);touch(0,p1.take(1),twoDown);touch(5 or (1 shl 8),p1,twoDown)
            val p2=listOf((x-30) to (y+40),(x+210) to (y+40));touch(2,p2,twoDown);touch(6 or (1 shl 8),p2,twoDown);touch(1,p2.take(1),twoDown)
            val panPinch=sample();check(difference(reset,panPinch)>2){"Two-finger pan/pinch produced no visible scene change"}
            tool("复位");val restored=sample();check(difference(initial,restored)<difference(initial,panPinch))
            tool("画质");ui{click(a.window.decorView,"关闭面板")};val afterPanel=sample()
            check(difference(restored,afterPanel)<difference(reset,panPinch)){"Quality tool/panel changed the camera"}
            capture("camera-restored-after-real-gestures")
        }
        run("gui_actual_valid_existing_parameters_and_highest_quality_save_without_quality_budget_change") {
            val a=checkNotNull(activity);val original=currentState().requested
            val high=ViewerConfig.highQualityPreset(original)
            check(high.runtime.toString()==original.runtime.toString()&&high.maximumInternalEdge==original.maximumInternalEdge&&high.maximumInternalPixels==original.maximumInternalPixels){"This fixture requires an already-high-quality saved request; refusing to change user quality"}
            ui{click(a.window.decorView,"设置");val root=a.window.decorView
                views(root).filterIsInstance<EditText>().first{it.contentDescription=="最长渲染边"}.setText(original.maximumInternalEdge.toString())
                views(root).filterIsInstance<EditText>().first{it.contentDescription=="质量阈值"}.setText(original.runtime.getJSONObject("lod").get("qualityPixels").toString())
                click(root,"保存并应用请求")}
            selectionChanged=true
            val saved=awaitState("same-value valid settings save and actual confirmation",60000){it.requested.generation==original.generation+1&&it.effective?.generation==it.requested.generation&&it.presentation==ViewerPresentation.READY}
            check(ViewerSettingsStore(i.targetContext).load().requested.generation==saved.requested.generation)
            ui{click(a.window.decorView,"最高画质")}
            val highest=awaitState("already-highest request actual confirmation",60000){it.requested.generation==saved.requested.generation+1&&it.effective?.generation==it.requested.generation&&it.presentation==ViewerPresentation.READY}
            check(highest.requested.runtime.toString()==original.runtime.toString())
            ui{click(a.window.decorView,"关闭面板")}
        }
        run("gui_actual_background_return_keeps_owner_scene_settings_and_advancing_frames_without_task_resume") {
            val before=currentState();val frame=before.diagnostics.getJSONObject("diagnostics").getJSONObject("latest").getLong("frameId")
            val ownerField=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}
            val controller=MainActivity::class.java.getDeclaredField("controller").apply{isAccessible=true}.get(checkNotNull(activity)) as ViewerController
            val owner=(ownerField.get(controller) as java.util.concurrent.atomic.AtomicReference<*>).get()
            i.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_HOME)
            awaitState("actual background pause",10000){it.presentation==ViewerPresentation.PAUSED}
            i.targetContext.startActivity(Intent(i.targetContext,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP))
            val after=awaitState("actual foreground advancing frame recovery",30000){it.presentation==ViewerPresentation.READY&&it.diagnostics.optJSONObject("diagnostics")?.optJSONObject("latest")?.optLong("frameId",0)?.let{f->f>frame}==true}
            check(after.selectedSceneId==before.selectedSceneId&&after.requested.toJson().toString()==before.requested.toJson().toString())
            check((ownerField.get(controller) as java.util.concurrent.atomic.AtomicReference<*>).get()==owner)
            check(LodBuildService.guiRunState()==null)
        }
        run("gui_current_source_main_pages_panels_screenshot_evidence") {
            val a = checkNotNull(activity)
            capture("viewer")
            for (label in listOf("场景", "任务", "浏览")) {
                ui {
                    val root = a.window.decorView
                    views(root).filterIsInstance<Button>().first { it.text == label || it.text == "$label · 当前" }.performClick()
                }
                capture(when(label) { "场景" -> "scenes"; "任务" -> "tasks"; else -> "viewer-return" })
            }
            ui { click(a.window.decorView, "设置") }; capture("settings-invalid-draft")
            ui { click(a.window.decorView, "重新载入当前请求值") }; systemClick("重新载入")
            capture("settings")
            ui { click(a.window.decorView, "报告与高级状态"); check(has(a.window.decorView, "截取三维画面并生成报告")) }; capture("reports")
            ui { a.onBackPressed(); a.onBackPressed() }
        }
        run("gui_real_diagnostics_screenshot_success_or_truthful_bounded_refusal_and_share_cancellation") {
            val a = checkNotNull(activity)
            val exports = File(i.targetContext.filesDir, "exports")
            val before = exports.listFiles()?.map { it.name }?.toSet().orEmpty()
            val evidence = File(i.targetContext.filesDir, "gui-verification-20261021-075317-slfq")
            fun awaitBundle(known: Set<String>, screenshot: Boolean): File? {
                val end = System.currentTimeMillis() + 90000
                while (System.currentTimeMillis() < end) {
                    val file = exports.listFiles()?.firstOrNull { it.name !in known && it.extension == "zip" }
                    if (file != null && runCatching {
                        java.util.zip.ZipFile(file).use { zip ->
                            val report = org.json.JSONObject(zip.getInputStream(zip.getEntry("report.json")).bufferedReader().readText())
                            check(report.getString("privacy") == "paths_and_source_omitted")
                            check(zip.getEntry("manifest.json") != null)
                            if (screenshot) check(zip.getEntry("screenshot.png") != null && zip.getEntry("screenshot.json") != null)
                            else check(zip.getEntry("screenshot.png") == null)
                        }
                    }.isSuccess) return file
                    if(screenshot&&currentState().message.contains("截图失败（screenshot_rejected）")) {
                        val state=currentState();val budget=state.diagnostics.getJSONObject("budget")
                        val latest=state.diagnostics.getJSONObject("diagnostics").getJSONObject("latest")
                        val width=latest.getLong("renderWidth");val height=latest.getLong("renderHeight")
                        val free=budget.getLong("limitBytes")-budget.getLong("reservedBytes")
                        val minimum=width*height*4*7+(1L shl 20)
                        check(free<minimum){"Unexpected screenshot rejection with apparent capture budget headroom"}
                        File(evidence,"screenshot-resource-refusal.json").writeText(org.json.JSONObject().put("operationMessage",state.message)
                            .put("remainingBytes",free).put("minimumCaptureBytes",minimum).put("renderWidth",width).put("renderHeight",height)
                            .put("budget",budget).toString(2))
                        return null
                    }
                    Thread.sleep(200)
                }
                val state=currentState()
                val controller=MainActivity::class.java.getDeclaredField("controller").apply{isAccessible=true}.get(a) as ViewerController
                val owner=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}.get(controller) as java.util.concurrent.atomic.AtomicReference<*>
                val status=NativeBridge.snapshot(owner.get() as NativeHandle,org.json.JSONObject().put("schemaVersion",1).put("kind","screenshotStatus").put("includePrivatePaths",false))
                File(evidence,"report-failure-native-status.json").writeText(org.json.JSONObject().put("operationMessage",state.message).put("screenshotStatus",status.toJson()).put("diagnostics",state.diagnostics).toString(2))
                error("No completed ${if (screenshot) "GPU screenshot" else "diagnostic"} report within90s; ${state.message}; native=${status.code}")
            }
            try {
                ui { click(a.window.decorView, "设置"); click(a.window.decorView, "报告与高级状态"); click(a.window.decorView, "生成诊断报告") }
                val diagnostics = checkNotNull(awaitBundle(before, false))
                systemClick("取消")
                diagnostics.copyTo(File(evidence, "actual-diagnostic-report.zip"), overwrite = true)
                ui { check(has(a.window.decorView, "报告与高级状态")); click(a.window.decorView, "截取三维画面并生成报告") }
                systemClick("生成报告")
                val screenshot = awaitBundle(before + diagnostics.name, true)
                if(screenshot!=null){
                    capture("screenshot-generated-share-confirmation")
                    systemClick("取消")
                    screenshot.copyTo(File(evidence, "actual-screenshot-report.zip"), overwrite = true)
                }else{
                    ui {
                        val status=views(a.window.decorView).filterIsInstance<TextView>().first{it.text.contains("screenshot_rejected")}
                        status.requestRectangleOnScreen(Rect(0,0,status.width,status.height),true)
                    }
                    capture("screenshot-budget-refusal-visible")
                    ui {check(views(a.window.decorView).filterIsInstance<TextView>().any{it.text.contains("可导出普通报告")&&it.getGlobalVisibleRect(Rect())})}
                    ui {click(a.window.decorView,"生成诊断报告")}
                    checkNotNull(awaitBundle(before+diagnostics.name,false));systemClick("取消")
                }
                ui { a.onBackPressed(); a.onBackPressed() }
                awaitState("usable progressing viewer after report dismissal",30000){it.presentation==ViewerPresentation.READY}
            } finally {
                exports.listFiles()?.filter { it.name !in before }?.forEach { check(it.delete()) }
            }
        }
        run("gui_recorded_narrow_wide_rotation_fonts_ime_and_recreation_keep_live_scene") {
            fun shell(command: String): String = i.uiAutomation.executeShellCommand(command).use { descriptor ->
                android.os.ParcelFileDescriptor.AutoCloseInputStream(descriptor).bufferedReader().readText()
            }
            val originalSize = shell("wm size")
            val originalFont = shell("settings get system font_scale").trim()
            val originalIme = shell("settings get secure show_ime_with_hard_keyboard").trim()
            var originalOrientation = -1
            ui { originalOrientation = checkNotNull(activity).requestedOrientation }
            val selected = currentState().selectedSceneId
            val request = currentState().requested.toJson().toString()
            val observations = org.json.JSONArray()
            fun change(action: () -> Unit) {
                val monitor = i.addMonitor(MainActivity::class.java.name,null,false)
                try {
                    action()
                    val next = monitor.waitForActivityWithTimeout(10000)
                    if (next != null) activity = next as MainActivity
                    i.waitForIdleSync()
                } finally { i.removeMonitor(monitor) }
            }
            fun verify(name: String, narrow: Boolean, ime: Boolean = false) {
                val state = awaitState("$name real frame recovery",90000) { it.presentation==ViewerPresentation.READY }
                check(state.selectedSceneId==selected && state.requested.toJson().toString()==request)
                ui {
                    val a=checkNotNull(activity); val root=a.window.decorView
                    val nav=views(root).filterIsInstance<Button>().filter { it.text in listOf("浏览 · 当前","场景","任务") }
                    check(nav.size==3 && nav.count{it.isSelected}==1)
                    val navigation=nav.first().parent as android.widget.LinearLayout
                    check(navigation.orientation==if(narrow) android.widget.LinearLayout.HORIZONTAL else android.widget.LinearLayout.VERTICAL)
                    nav.forEach { check(it.getGlobalVisibleRect(Rect()) && it.isEnabled) }
                    observations.put(org.json.JSONObject().put("scenario",name).put("width",root.width).put("height",root.height)
                        .put("density",a.resources.displayMetrics.density).put("fontScale",a.resources.configuration.fontScale)
                        .put("frameId",state.diagnostics.optJSONObject("diagnostics")?.optJSONObject("latest")?.optLong("frameId")))
                }
                capture(name)
                if (ime) {
                    ui {
                        val a=checkNotNull(activity);click(a.window.decorView,"设置")
                        val field=views(a.window.decorView).filterIsInstance<EditText>().first{it.contentDescription=="最长渲染边"}
                        field.requestFocus()
                        field.requestRectangleOnScreen(Rect(0,0,field.width,field.height),true)
                        (a.getSystemService(android.content.Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager).showSoftInput(field,android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)
                    }
                    i.waitForIdleSync();Thread.sleep(200)
                    var fieldRect=Rect()
                    ui{check(views(checkNotNull(activity).window.decorView).filterIsInstance<EditText>().first{it.contentDescription=="最长渲染边"}.getGlobalVisibleRect(fieldRect))}
                    val down=android.os.SystemClock.uptimeMillis()
                    for(action in listOf(android.view.MotionEvent.ACTION_DOWN,android.view.MotionEvent.ACTION_UP)){
                        val event=android.view.MotionEvent.obtain(down,android.os.SystemClock.uptimeMillis(),action,fieldRect.exactCenterX(),fieldRect.exactCenterY(),0)
                        event.source=android.view.InputDevice.SOURCE_TOUCHSCREEN;try{i.sendPointerSync(event)}finally{event.recycle()}
                    }
                    ui{val a=checkNotNull(activity);val field=views(a.window.decorView).filterIsInstance<EditText>().first{it.contentDescription=="最长渲染边"};val manager=a.getSystemService(android.content.Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager;manager.restartInput(field);manager.showSoftInput(field,android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)}
                    val end=System.currentTimeMillis()+10000;var shown=false;var logical=false;var imeHeight=0
                    while(!shown&&System.currentTimeMillis()<end) {
                        ui { val insets=androidx.core.view.ViewCompat.getRootWindowInsets(checkNotNull(activity).window.decorView);logical=insets?.isVisible(androidx.core.view.WindowInsetsCompat.Type.ime())==true;imeHeight=insets?.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime())?.bottom?:0;shown=logical&&imeHeight>GuiStyle.dp(checkNotNull(activity),100) }
                        if(!shown)Thread.sleep(100)
                    }
                    observations.put(org.json.JSONObject().put("scenario","$name-ime").put("logicalVisible",logical).put("imeHeight",imeHeight))
                    capture("$name-ime")
                    check(shown){"No actual usable keyboard geometry after real tap+IMM: logical=$logical, imeHeight=$imeHeight"}
                    ui {
                        val a=checkNotNull(activity);val root=a.window.decorView
                        val save=views(root).filterIsInstance<Button>().first{it.text=="保存并应用请求"}
                        save.requestRectangleOnScreen(Rect(0,0,save.width,save.height),true)
                    }
                    i.waitForIdleSync();Thread.sleep(100)
                    ui {
                        val root=checkNotNull(activity).window.decorView
                        val save=views(root).filterIsInstance<Button>().first{it.text=="保存并应用请求"}
                        val rect=Rect();check(save.getGlobalVisibleRect(rect)&&rect.height()>=GuiStyle.dp(checkNotNull(activity),40))
                        click(root,"关闭面板")
                    }
                }
            }
            try {
                shell("settings put secure show_ime_with_hard_keyboard 1")
                change { shell("wm size 1080x1920") }
                change { ui{checkNotNull(activity).requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT} }
                verify("narrow-portrait",true,ime=true)
                change { shell("settings put system font_scale 1.5") }
                verify("narrow-portrait-font150",true,ime=true)
                change { ui{checkNotNull(activity).requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE} }
                verify("narrow-device-landscape-font150",false)
                change { shell("wm size 1440x2560") }
                change { ui{checkNotNull(activity).requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT} }
                verify("wide-portrait-font150",false,ime=true)
                change { shell("settings put system font_scale 1.3") }
                change { ui{checkNotNull(activity).requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE} }
                verify("wide-landscape-font130",false)
            } finally {
                val size=Regex("Override size: ([0-9]+x[0-9]+)").find(originalSize)?.groupValues?.get(1)
                change { shell(if(size==null) "wm size reset" else "wm size $size") }
                change { shell("settings put system font_scale $originalFont") }
                change { ui{checkNotNull(activity).requestedOrientation=originalOrientation} }
                shell(if(originalIme=="null")"settings delete secure show_ime_with_hard_keyboard" else "settings put secure show_ime_with_hard_keyboard $originalIme")
                File(i.targetContext.filesDir,"gui-verification-20261021-075317-slfq/layout-observations.json").writeText(observations.toString(2))
            }
        }
        // Finish only this test-owned Activity; production owner retirement remains asynchronous.
        val finishingAt = android.os.SystemClock.elapsedRealtime()
        activity?.let { a -> ui { a.finish() }; i.waitForIdleSync() }
        if (selectionChanged) run("gui_fixture_restores_exact_saved_settings_after_acknowledged_owner_retirement") {
            val end = System.currentTimeMillis() + 30000
            var retired = false
            while (!retired && System.currentTimeMillis() < end) {
                retired = runCatching {
                    val record = org.json.JSONObject(File(i.targetContext.filesDir, "viewer-owner-lifecycle.json").readText())
                    record.getString("phase") == "retired" && record.getJSONObject("result").getBoolean("ok") &&
                        record.getLong("elapsedRealtimeMillis") >= finishingAt
                }.getOrDefault(false)
                if (!retired) Thread.sleep(100)
            }
            check(retired) { "Owner retirement not acknowledged; refusing to overwrite saved settings" }
            if (originalSettings != null) {
                val atomic = android.util.AtomicFile(settingsFile)
                val stream = atomic.startWrite()
                try { stream.write(originalSettings); atomic.finishWrite(stream) }
                catch (error: Throwable) { atomic.failWrite(stream); throw error }
                check(settingsFile.readBytes().contentEquals(originalSettings))
            } else check(!settingsFile.exists() || settingsFile.delete())
        }
        i.targetContext.contentResolver.call(provider,"remove-gui-package",packageName,null)
        ownedTaskScenes.forEach { check(SceneRepository(i.targetContext).requestDelete(it)) }
        run("gui_fixture_retired_task_cleanup_preserves_all_preexisting_jobs") { ownedTaskJobs.forEach { id ->
            if (LodBuildService.guiRunState()?.jobId==id) {
                LodBuildService.control(i.targetContext,id,LodBuildService.ACTION_CANCEL)
                val end=System.currentTimeMillis()+60000
                while(LodBuildService.hasActiveJob()&&System.currentTimeMillis()<end)Thread.sleep(100)
            }
            check(!LodBuildService.hasActiveJob()){ "Refusing cleanup while native worker remains active" }
            val store=BuildJobStore(i.targetContext)
            if(store.list().any{it.id==id})store.cleanupRetired(id,removeSource=true)
        } }
    }
}
