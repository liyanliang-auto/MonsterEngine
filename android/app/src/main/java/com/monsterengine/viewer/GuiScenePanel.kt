package com.monsterengine.viewer

import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView

/** Scene library presentation. All storage, SAF and navigation remain Activity-owned. */
class GuiScenePanel(context: Context, private val actions: Actions) : LinearLayout(context) {
    /** Explicit user actions; rendering never opens, imports or starts a build. */
    data class Actions(
        val pickImport: () -> Unit,
        val openBuiltin: () -> Unit,
        val openScene: (String) -> Unit,
        val exportScene: (String) -> Unit,
        val deleteScene: (String) -> Unit,
        val createBuild: () -> Unit,
        val discardImport: () -> Unit,
        val cancelImport: () -> Unit
    )

    private data class ContentKey(
        val scenes: List<SceneDescriptor>, val builtin: SceneDescriptor?, val selected: String,
        val busy: Boolean, val nativeAvailable: Boolean, val copying: Boolean,
        val pending: ImportOutcome.BuildRequired?, val columns: Int
    )

    private val content = LinearLayout(context).apply { orientation = VERTICAL }
    private val scroll = ScrollView(context).apply {
        isFillViewport = true
        clipToPadding = false
        addView(content, FrameLayout.LayoutParams(-1, -2))
    }
    private val status = GuiStyle.body(context, "").apply {
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    }
    private var latest: ViewerController.State? = null
    private var lastKey: ContentKey? = null

    init {
        orientation = VERTICAL
        setBackgroundColor(GuiStyle.BACKGROUND)
        setPadding(dp(16), dp(12), dp(16), 0)
        addView(scroll, LayoutParams(-1, -1))
    }

    /** Refresh only visible library dependencies, retaining scroll across native polling. */
    fun render(state: ViewerController.State) {
        latest = state
        status.text = state.message
        val key = ContentKey(state.scenes.toList(), state.builtin, state.selectedSceneId,
            state.busy, state.nativeAvailable, state.importInProgress,
            state.pendingImport, columnCount())
        if (key == lastKey) return
        lastKey = key
        val y = scroll.scrollY
        content.removeAllViews()
        content.addView(GuiStyle.title(context, "场景库"))
        content.addView(status)
        content.addView(GuiStyle.button(context, "导入 PLY 或场景包",
            !state.busy && state.pendingImport == null, actions.pickImport))
        content.addView(GuiStyle.body(context, "通过系统文件选择器导入；取消选择会保留已有场景。"))
        if (key.copying) {
            content.addView(GuiStyle.outlinedButton(context, "取消导入", action = actions.cancelImport))
        }
        state.pendingImport?.let { pending ->
            content.addView(GuiStyle.card(context).apply {
                addView(GuiStyle.title(context, "需要先构建"))
                addView(GuiStyle.body(context,
                    "已导入 %.1f MiB。大场景需要在设备上构建 LOD，构建期间暂停浏览。完成后可从任务页打开结果；暂停或取消会保留可恢复进度。"
                        .format(pending.bytes / (1024.0 * 1024.0))))
                addView(GuiStyle.button(context, "创建并开始构建", !state.busy) {
                    GuiStyle.confirm(context, "开始构建？",
                        "任务将在设备上运行，并暂停当前画面。可在任务页暂停或取消并保留进度。", "开始构建", actions.createBuild)
                })
                addView(GuiStyle.dangerButton(context, "放弃这次导入", !state.busy) {
                    GuiStyle.confirm(context, "清理本次导入？",
                        "仅清理尚未创建任务的临时源文件；已有场景和任务将保留。", "清理导入", actions.discardImport)
                })
            })
        }
        content.addView(GuiStyle.gap(context))
        val cards = mutableListOf<View>()
        cards += sceneCard(state.builtin, "内置示例", true, state)
        state.scenes.forEachIndexed { index, scene ->
            cards += sceneCard(scene, "场景 ${index + 1}", false, state)
        }
        cards.chunked(key.columns).forEach { group ->
            val row = LinearLayout(context).apply { orientation = HORIZONTAL; gravity = Gravity.TOP }
            group.forEachIndexed { index, card ->
                row.addView(card, LayoutParams(0, -2, 1f).apply {
                    if (index > 0) leftMargin = dp(12)
                    bottomMargin = dp(12)
                })
            }
            repeat(key.columns - group.size) {
                row.addView(View(context), LayoutParams(0, 1, 1f).apply { leftMargin = dp(12) })
            }
            content.addView(row)
        }
        if (state.scenes.isEmpty()) {
            content.addView(GuiStyle.card(context).apply {
                addView(GuiStyle.title(context, "还没有导入的场景"))
                addView(GuiStyle.body(context, "可以先打开内置示例体验相机操作，或导入高斯 PLY、已构建的场景包。普通点云 PLY 不一定是可用的高斯场景。"))
            })
        }
        content.addView(GuiStyle.body(context, "浏览手势：单指旋转，双指平移或缩放，双击复位。"))
        scroll.post { scroll.scrollTo(0, y) }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w != oldw) post { latest?.let(::render) }
    }

    private fun columnCount(): Int {
        val available = (width - paddingLeft - paddingRight).coerceAtLeast(0)
        val minimum = (dp(264) * resources.configuration.fontScale.coerceAtLeast(1f)).toInt()
        return ((available + dp(12)) / (minimum + dp(12))).coerceIn(1, 3)
    }

    private fun sceneCard(scene: SceneDescriptor?, name: String, builtin: Boolean,
                          state: ViewerController.State): LinearLayout = GuiStyle.card(context).apply {
        addView(GenericCover(context), LayoutParams(-1, dp(104)))
        addView(GuiStyle.body(context, "通用占位封面 · 非场景预览").apply { textSize = 12f })
        addView(GuiStyle.title(context, name).apply { textSize = 19f })
        addView(GuiStyle.body(context, when {
            builtin -> "内置场景"
            scene?.kind == ViewerSceneKind.LOD -> "流式场景"
            else -> "小场景"
        }))
        if (scene != null && scene.sceneId == state.selectedSceneId) {
            addView(GuiStyle.body(context, "当前选择").apply { setTextColor(GuiStyle.ACCENT) })
        }
        if (scene == null) {
            addView(GuiStyle.body(context, "内置资源暂不可用。可导入场景，或到设置生成诊断报告。"))
        }
        addView(GuiStyle.button(context, "打开$name",
            scene != null && !state.busy && state.nativeAvailable) {
            if (builtin) actions.openBuiltin() else scene?.let { actions.openScene(it.sceneId) }
        })
        if (builtin) {
            addView(GuiStyle.outlinedButton(context, "示例说明") {
                AlertDialog.Builder(context, R.style.Theme_MonsterEngine_Dialog)
                    .setTitle("内置示例")
                    .setMessage("示例由外部 PLY 完整派生为离线 LOD。模型来源与授权仍待核实。封面是统一示意图，不是模型截图。")
                    .setPositiveButton("关闭", null).show()
            })
        } else if (scene != null) {
            addView(GuiStyle.outlinedButton(context, "$name 更多操作", !state.busy) {
                showSceneActions(scene, name)
            })
        }
    }

    private fun showSceneActions(scene: SceneDescriptor, name: String) {
        val labels = if (scene.kind == ViewerSceneKind.LOD) arrayOf("导出场景包", "删除场景") else arrayOf("删除场景")
        AlertDialog.Builder(context, R.style.Theme_MonsterEngine_Dialog)
            .setTitle(name)
            .setItems(labels) { _, index ->
                if (labels[index] == "导出场景包") actions.exportScene(scene.sceneId)
                else GuiStyle.confirm(context, "删除$name？",
                    "场景将在使用结束后删除。已导出的场景包不受影响。", "删除场景") {
                    actions.deleteScene(scene.sceneId)
                }
            }
            .setNegativeButton("返回", null).show()
    }

    private fun dp(value: Int): Int = GuiStyle.dp(context, value)

    /** Deliberately abstract geometry; never represents actual model content. */
    private class GenericCover(context: Context) : View(context) {
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        private val path = Path()
        init {
            background = GuiStyle.background(context, GuiStyle.BACKGROUND, 10)
            importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val x = width / 2f
            val y = height / 2f
            val s = minOf(width.toFloat(), height.toFloat()) * 0.28f
            fun face(color: Int, vararg points: Float) {
                path.reset(); path.moveTo(points[0], points[1])
                for (i in 2 until points.size step 2) path.lineTo(points[i], points[i + 1])
                path.close(); paint.color = color; canvas.drawPath(path, paint)
            }
            face(0xFF94B3C5.toInt(), x, y-s, x+s, y-s/2, x, y, x-s, y-s/2)
            face(0xFF496779.toInt(), x-s, y-s/2, x, y, x, y+s, x-s, y+s/2)
            face(0xFF67899D.toInt(), x, y, x+s, y-s/2, x+s, y+s/2, x, y+s)
        }
    }
}
