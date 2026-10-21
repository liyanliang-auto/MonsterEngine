package com.monsterengine.viewer

import android.content.Context
import android.content.res.ColorStateList
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView

/** Task presentation consumes durable records and read-only process observations. */
class GuiTaskPanel(context: Context, private val actions: Actions) : LinearLayout(context) {
    /** Activity-owned actions keep permission, native retirement and navigation ownership.
     * @property refresh Reload durable job observations without starting work.
     * @property start Explicit renderer-safe, visible-Activity start request.
     * @property control Submit an existing service control action.
     * @property openResult Open the observed published scene ID.
     * @property cleanup Explicitly remove a retired job and its unshared temporary source. */
    data class Actions(
        val refresh: () -> Unit,
        val start: (String) -> Unit,
        val control: (String, String) -> NativeResult,
        val openResult: (String) -> Unit,
        val cleanup: (String) -> Unit
    )

    private data class Request(val action: String, val previousUpdate: Long, val dispatched: Boolean = false)
    private data class Display(val jobs: List<BuildJobRecord>, val busy: Boolean,
        val run: LodBuildService.Companion.GuiRunState?, val reserved: Boolean,
        val requests: Map<String, Request>, val notices: Map<String, String>, val notifications: Boolean)

    private val scroll = ScrollView(context).apply { isFillViewport = true }
    private val content = LinearLayout(context).apply {
        orientation = VERTICAL
        setPadding(GuiStyle.dp(context, 16), GuiStyle.dp(context, 12),
            GuiStyle.dp(context, 16), GuiStyle.dp(context, 24))
    }
    private val requests = mutableMapOf<String, Request>()
    private val notices = mutableMapOf<String, String>()
    private var latest: ViewerController.State? = null
    private var displayed: Display? = null

    init {
        orientation = VERTICAL
        setBackgroundColor(GuiStyle.BACKGROUND)
        scroll.addView(content, FrameLayout.LayoutParams(-1, -2))
        addView(scroll, LayoutParams(-1, -1))
    }

    /** Reconcile an Activity-owned asynchronous start result, including preparation failure.
     * A dispatched result only confirms submission, never that computation started. */
    fun onStartResult(jobId: String, result: NativeResult) {
        val request = requests[jobId]
        if (request?.action != LodBuildService.ACTION_START) return
        if (result.ok) {
            requests[jobId] = request.copy(dispatched = true)
            notices[jobId] = "开始请求已提交，等待服务状态确认。"
        } else {
            requests.remove(jobId)
            notices[jobId] = failureMessage(result)
        }
        latest?.let(::render)
    }

    /** Render only meaningful changes; frame callbacks preserve the scroll and controls. */
    fun render(state: ViewerController.State) {
        latest = state
        val run = LodBuildService.guiRunState()
        val reserved = LodBuildService.hasActiveJob()
        requests.keys.toList().forEach { id ->
            val job = state.jobs.firstOrNull { it.id == id }
            val request = requests.getValue(id)
            val acknowledged = when (request.action) {
                LodBuildService.ACTION_START -> run?.jobId == id ||
                    (request.dispatched && job != null && job.updatedAtMillis != request.previousUpdate && !reserved)
                LodBuildService.ACTION_PAUSE -> run?.jobId != id && job != null &&
                    job.updatedAtMillis != request.previousUpdate && job.progress.stage in RETIRED
                LodBuildService.ACTION_CANCEL -> run?.jobId != id &&
                    job != null && job.updatedAtMillis != request.previousUpdate &&
                    job.progress.stage in setOf(BuildStage.Cancelled, BuildStage.Completed, BuildStage.Failed)
                else -> false
            }
            if (job == null || acknowledged) {
                requests.remove(id)
                notices.remove(id)
            }
        }
        val next = Display(state.jobs, state.busy, run, reserved, requests.toMap(), notices.toMap(),
            BuildNotifications(context).permissionGranted())
        if (next == displayed) return
        displayed = next
        val y = scroll.scrollY
        content.removeAllViews()
        content.addView(GuiStyle.title(context, "构建任务"))
        content.addView(GuiStyle.body(context, "仅显示实际阶段和已记录处理量。打开此页不会自动继续任务。"))
        if (!next.notifications) content.addView(GuiStyle.body(context,
            "通知未开启；仍可在此查看和控制任务。系统前台服务限制可能阻止开始，请保持应用可见并手动重试。"))
        content.addView(GuiStyle.button(context, "刷新任务", !state.busy) { actions.refresh() })
        content.addView(GuiStyle.gap(context))
        if (state.jobs.isEmpty()) content.addView(GuiStyle.card(context).apply {
            addView(GuiStyle.title(context, "暂无构建任务"))
            addView(GuiStyle.body(context, "在场景页导入需要构建的大场景后，显式创建任务。构建会暂停三维浏览。"))
        })
        state.jobs.forEach { job -> content.addView(jobCard(job, next)) }
        scroll.post { scroll.scrollTo(0, y) }
    }

    private fun jobCard(job: BuildJobRecord, display: Display): LinearLayout = GuiStyle.card(context).apply {
        val progress = job.progress
        val ownRun = display.run?.jobId == job.id
        val pending = display.requests[job.id]
        val stopping = ownRun && display.run?.stopRequested == true
        val free = !display.busy && !display.reserved && display.requests.isEmpty()
        val canControl = !display.busy && pending == null && !stopping &&
            display.requests.values.none { it.action == LodBuildService.ACTION_START }
        addView(GuiStyle.title(context, "任务 ${job.id.take(8)}"))
        addView(GuiStyle.body(context, "阶段：${stageName(progress.stage)}"))
        val requestText = when {
            pending?.action == LodBuildService.ACTION_START -> if (pending.dispatched)
                "开始请求已提交，等待服务确认。" else "正在准备开始，等待画面释放或权限选择。"
            pending?.action == LodBuildService.ACTION_PAUSE -> "暂停请求已提交，等待检查点保存及原生工作线程退出；尚未确认暂停。"
            pending?.action == LodBuildService.ACTION_CANCEL -> "取消请求已提交，等待工作线程退出；已提交的进度会保留。"
            stopping -> "服务正在停止，等待原生工作线程退出；尚未确认退休。"
            ownRun -> "服务正在处理本任务。"
            progress.stage in ACTIVE && progress.stage != BuildStage.Created -> "等待服务状态同步；可刷新核实，不会自动恢复。"
            else -> ""
        }
        if (requestText.isNotEmpty()) addView(GuiStyle.body(context, requestText))
        addView(GuiStyle.body(context, buildString {
            append("已记录处理量：${progress.processedRecords}")
            append(if (progress.totalRecords > 0) " / ${progress.totalRecords} 个高斯" else " 个高斯；总量未知")
            append("\n字节：${progress.processedBytes}")
            append(if (progress.totalBytes > 0) " / ${progress.totalBytes}" else "；总量未知")
        }))
        if (ownRun && progress.stage in ACTIVE) addStageProgress(progress)
        if (progress.checkpointGeneration > 0)
            addView(GuiStyle.body(context, "已记录检查点：${progress.checkpointGeneration}"))
        if (progress.peakWorkingBytes > 0) addView(GuiStyle.body(context,
            "构建工作区峰值：${progress.peakWorkingBytes / (1024 * 1024)} MiB（非完整进程或 GPU 占用）"))
        if (job.stopReason.isNotEmpty() || progress.error.isNotEmpty() ||
            progress.stage in setOf(BuildStage.Failed, BuildStage.WaitingForResources))
            addView(GuiStyle.body(context,
                "请检查可用空间、设备温度、电量和系统前台服务限制，再手动继续或重试。检查点兼容性仍需校验；详细原因可从诊断报告查看。"))
        display.notices[job.id]?.let { addView(GuiStyle.body(context, it)) }
        if (job.publishedSceneId.isNotEmpty()) {
            addView(GuiStyle.button(context, "打开已发布结果", !display.busy) {
                actions.openResult(job.publishedSceneId)
            })
        } else if (progress.stage != BuildStage.Completed) {
            addView(GuiStyle.button(context, if (progress.stage in RETIRED) "继续 / 重试" else "开始", free) {
                if (latest?.busy != false || requests.isNotEmpty() || LodBuildService.hasActiveJob()) return@button
                requests[job.id] = Request(LodBuildService.ACTION_START, job.updatedAtMillis)
                notices.remove(job.id)
                latest?.let(::render)
                actions.start(job.id)
            })
            addView(GuiStyle.button(context, "暂停", canControl && ownRun) {
                submitControl(job, LodBuildService.ACTION_PAUSE)
            })
            addView(GuiStyle.dangerButton(context, "取消并保留进度", canControl &&
                (ownRun || (!display.reserved && progress.stage != BuildStage.Cancelled))) {
                GuiStyle.confirm(context, "取消构建？", "等待工作线程退出后才确认取消。已提交的检查点和临时源文件保留，可稍后继续或手动清理。",
                    "取消并保留进度") { submitControl(job, LodBuildService.ACTION_CANCEL) }
            })
        } else {
            addView(GuiStyle.body(context, "尚未观察到已发布场景标识，请刷新核实。"))
        }
        addView(GuiStyle.dangerButton(context, "清理任务和临时源文件", free && progress.stage in RETIRED) {
            GuiStyle.confirm(context, "清理已退休任务？",
                "将删除本任务的检查点、临时输出及未被其他任务引用的临时源文件。已发布到场景库的场景仍保留。在用任务和共享源文件受到保护；清理后不能继续本任务。",
                "清理任务") {
                if (!LodBuildService.hasActiveJob() && requests.isEmpty()) actions.cleanup(job.id)
            }
        })
    }

    private fun LinearLayout.addStageProgress(progress: BuildProgress) {
        val total: Long
        val processed: Long
        val unit: String
        if (progress.stage == BuildStage.Copying && progress.totalBytes > 0) {
            total = progress.totalBytes; processed = progress.processedBytes; unit = "字节"
        } else if (progress.totalRecords > 0) {
            total = progress.totalRecords; processed = progress.processedRecords; unit = "高斯"
        } else {
            total = progress.totalBytes; processed = progress.processedBytes; unit = "字节"
        }
        val known = total > 0 && processed <= total
        addView(ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = !known
            max = 1000
            if (known) this.progress = ((processed.toDouble() / total) * max).toInt().coerceIn(0, max)
            progressTintList = ColorStateList.valueOf(GuiStyle.ACCENT)
            indeterminateTintList = ColorStateList.valueOf(GuiStyle.ACCENT)
            contentDescription = if (known) "当前阶段已处理 $processed / $total $unit" else "当前阶段总量尚未确认"
            layoutParams = LayoutParams(-1, GuiStyle.dp(context, 12))
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        })
        addView(GuiStyle.body(context, if (known) "仅为当前阶段进度，不代表整个任务完成比例。"
            else "当前阶段总量尚未确认，显示不定进度。"))
    }

    private fun submitControl(job: BuildJobRecord, action: String) {
        val state = latest ?: return
        val run = LodBuildService.guiRunState()
        if (state.busy || (LodBuildService.hasActiveJob() && run == null) || requests.containsKey(job.id) ||
            requests.values.any { it.action == LodBuildService.ACTION_START } || run?.stopRequested == true ||
            (run != null && run.jobId != job.id) ||
            (action == LodBuildService.ACTION_PAUSE && run?.jobId != job.id)) return
        requests[job.id] = Request(action, job.updatedAtMillis, true)
        notices.remove(job.id)
        render(state)
        val result = actions.control(job.id, action)
        if (!result.ok) {
            requests.remove(job.id)
            notices[job.id] = failureMessage(result)
        }
        latest?.let(::render)
    }

    private fun failureMessage(result: NativeResult): String = when (result.code) {
        "visible_activity_required" -> "请返回前台并关闭遮挡窗口后，手动开始。任务尚未确认开始。"
        "build_busy" -> "已有工作线程仍在运行或退休，请等待并刷新后重试。"
        "foreground_start_rejected" -> "系统拒绝前台服务请求。请保持应用可见，检查系统限制后手动重试。"
        else -> "请求未成功提交。请等待画面和工作线程安全释放，刷新状态后手动重试。"
    }

    private fun stageName(stage: BuildStage): String = when (stage) {
        BuildStage.Created -> "已创建 / 等待开始"
        BuildStage.Copying -> "复制输入"
        BuildStage.Normalizing -> "规范化高斯"
        BuildStage.Partitioning -> "空间分区"
        BuildStage.BuildingLeaves -> "构建叶节点"
        BuildStage.Reducing -> "归约层级"
        BuildStage.Validating -> "校验输出"
        BuildStage.Publishing -> "发布到场景库"
        BuildStage.Completed -> "已完成"
        BuildStage.Paused -> "已记录暂停"
        BuildStage.Cancelled -> "已记录取消（进度保留）"
        BuildStage.Failed -> "构建失败"
        BuildStage.WaitingForResources -> "等待可用资源"
    }

    private companion object {
        val RETIRED = setOf(BuildStage.Paused, BuildStage.Cancelled, BuildStage.Failed,
            BuildStage.WaitingForResources, BuildStage.Completed)
        val ACTIVE = setOf(BuildStage.Created, BuildStage.Copying, BuildStage.Normalizing,
            BuildStage.Partitioning, BuildStage.BuildingLeaves, BuildStage.Reducing,
            BuildStage.Validating, BuildStage.Publishing)
    }
}
