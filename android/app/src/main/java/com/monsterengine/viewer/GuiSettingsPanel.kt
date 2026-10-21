package com.monsterengine.viewer

import android.content.Context
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject

/** Temporary settings/report content; the Activity retains this panel and owns navigation/sharing. */
class GuiSettingsPanel(context: Context, private val actions: Actions) : LinearLayout(context) {
    /** User-initiated commands. Report callbacks generate locally; sharing remains an Activity decision.
     * @property applySettings Submit a validated, copied request through the existing controller.
     * @property highQuality Explicitly choose the existing highest-quality preset.
     * @property showReports Navigate to the secondary report content.
     * @property diagnostics Generate the existing diagnostic report.
     * @property screenshot Generate the existing three-dimensional screenshot report.
     * @property advanced Show the existing advanced diagnostic state.
     */
    data class Actions(val applySettings: (ViewerConfig) -> Unit, val highQuality: () -> Unit,
        val showReports: () -> Unit, val diagnostics: () -> Unit, val screenshot: () -> Unit,
        val advanced: () -> Unit)

    private var state = ViewerController.State()
    private var initializedDraft = false
    private var writingDraft = false
    private var draftDirty = false
    private val settingsContent = content()
    private val reportContent = content()
    private val requestedText = GuiStyle.body(context, "")
    private val effectiveText = GuiStyle.body(context, "")
    private val operationText = GuiStyle.body(context, "")
    private val editStatus = GuiStyle.body(context, "")
    private val reportStatus = GuiStyle.body(context, "")
    private val edge = input("最长渲染边", InputType.TYPE_CLASS_NUMBER)
    private val quality = input("质量阈值", InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL)
    private lateinit var applyButton: Button
    private lateinit var highestButton: Button
    private lateinit var diagnosticsButton: Button
    private lateinit var screenshotButton: Button
    private val settingsScroll: ScrollView
    private val reportScroll: ScrollView

    init {
        orientation = VERTICAL
        setBackgroundColor(GuiStyle.BACKGROUND)
        isClickable = true
        settingsContent.addView(GuiStyle.title(context, "画质与设置"))
        settingsContent.addView(GuiStyle.body(context,
            "请求保存在本机。保存、提交与实际生效分别确认；允许输入的范围不代表设备一定能够运行。"))
        settingsContent.addView(GuiStyle.card(context).apply {
            addView(requestedText)
            addView(effectiveText)
            addView(operationText)
        })
        settingsContent.addView(GuiStyle.card(context).apply {
            addView(GuiStyle.title(context, "画面参数"))
            addView(label("最长渲染边（128–4096）", edge))
            addView(edge, LayoutParams(-1, -2))
            addView(GuiStyle.gap(context))
            addView(label("质量阈值（0.1–100）", quality))
            addView(quality, LayoutParams(-1, -2))
            addView(GuiStyle.body(context, "质量阈值越小，细节越多，资源需求也可能增加。"))
            addView(editStatus)
            applyButton = GuiStyle.button(context, "保存并应用请求") { submitDraft() }
            addView(applyButton)
            addView(GuiStyle.outlinedButton(context, "重新载入当前请求值") {
                if (draftDirty) GuiStyle.confirm(context, "放弃未保存的编辑？",
                    "将使用当前请求值替换这两个输入框，已保存设置不变。", "重新载入") {
                    loadDraft(state.requested)
                    updateText(editStatus, "已重新载入当前请求值。")
                } else {
                    loadDraft(state.requested)
                    updateText(editStatus, "已重新载入当前请求值。")
                }
            })
        })
        settingsContent.addView(GuiStyle.card(context).apply {
            addView(GuiStyle.title(context, "细节优先"))
            addView(GuiStyle.body(context,
                "最高画质使用已有完整 SH、细分 LOD 和高清渲染请求，可能明显降低帧率。资源不足时可能暂停或保留上一幅完整画面。"))
            highestButton = GuiStyle.button(context, "最高画质") {
                if (!state.busy) {
                    try {
                        actions.highQuality()
                        updateText(editStatus, if (draftDirty)
                            "已请求最高画质；未保存的输入仍保留，之后保存会提交输入框中的参数。"
                            else "已请求最高画质，请查看操作状态与实际生效信息。")
                    } catch (_: Exception) {
                        updateText(editStatus, "暂时无法提交最高画质，请稍后重试；输入已保留。")
                    }
                }
            }
            addView(highestButton)
        })
        settingsContent.addView(GuiStyle.outlinedButton(context, "报告与高级状态") { actions.showReports() })

        reportContent.addView(GuiStyle.title(context, "报告与高级状态"))
        reportContent.addView(GuiStyle.body(context,
            "报告先保存在本机。生成完成后，你可以选择分享或取消；只有明确选择接收应用后才会授予临时访问权限。默认报告省略场景源文件、文件名和私有路径。"))
        reportContent.addView(GuiStyle.card(context).apply {
            addView(GuiStyle.title(context, "三维截图报告"))
            addView(GuiStyle.body(context,
                "截取当前三维渲染画面并生成既有截图报告包。生成后可自行决定是否分享。"))
            screenshotButton = GuiStyle.button(context, "截取三维画面并生成报告") { actions.screenshot() }
            addView(screenshotButton)
        })
        reportContent.addView(GuiStyle.card(context).apply {
            addView(GuiStyle.title(context, "诊断"))
            addView(GuiStyle.body(context, "遇到初始化或渲染问题时，可生成诊断报告，或查看当前高级状态。"))
            diagnosticsButton = GuiStyle.button(context, "生成诊断报告") { actions.diagnostics() }
            addView(diagnosticsButton)
            addView(GuiStyle.outlinedButton(context, "查看高级状态") { actions.advanced() })
            addView(reportStatus)
        })
        settingsScroll = scroll(settingsContent)
        reportScroll = scroll(reportContent)
        addView(settingsScroll, LayoutParams(-1, -1))
        addView(reportScroll, LayoutParams(-1, -1))
        showSettings()
    }

    /** Show quality/settings without recreating fields or discarding unsaved input. */
    fun showSettings() {
        settingsScroll.visibility = View.VISIBLE
        reportScroll.visibility = View.GONE
    }

    /** Show secondary report actions; this never generates or sends a report. */
    fun showReports() {
        settingsScroll.visibility = View.GONE
        reportScroll.visibility = View.VISIBLE
    }

    /** Refresh observation labels and availability while preserving focused fields and drafts. */
    fun render(state: ViewerController.State) {
        this.state = state
        if (!initializedDraft || (!draftDirty && !edge.hasFocus() && !quality.hasFocus())) {
            loadDraft(state.requested)
        }
        updateText(requestedText, "当前请求（版本 ${state.requested.generation}）：${describe(state.requested)}")
        updateText(effectiveText, state.effective?.let {
            "已报告的实际配置（版本 ${it.generation}）：${describe(it)}" +
                if (it.generation != state.requested.generation) "\n尚未确认当前请求生效。" else
                    "\n此配置报告不代表画面正在持续更新，请同时查看浏览状态。"
        } ?: "实际生效：尚未确认。保存请求不会自动确认实际生效。")
        updateText(operationText, "操作状态：${state.message}")
        updateText(reportStatus, "操作状态：${state.message}")
        applyButton.isEnabled = !state.busy
        highestButton.isEnabled = !state.busy
        diagnosticsButton.isEnabled = !state.busy
        screenshotButton.isEnabled = !state.busy && state.nativeAvailable && !state.detachPending && !state.buildBlocked
    }

    private fun submitDraft() {
        if (state.busy) return
        val longest = edge.text.toString().trim().toIntOrNull()
        val threshold = quality.text.toString().trim().toDoubleOrNull()
        edge.error = if (longest == null || longest !in 128..4096) "请输入 128–4096 之间的整数" else null
        quality.error = if (threshold == null || !threshold.isFinite() || threshold !in 0.1..100.0)
            "请输入 0.1–100 之间的有限数值" else null
        val invalid = if (edge.error != null) edge else if (quality.error != null) quality else null
        if (invalid != null) {
            updateText(editStatus, "参数尚未提交，请修正标出的输入；编辑内容已保留。")
            invalid.requestFocus()
            return
        }
        try {
            // Copy the latest request, retaining unrelated runtime keys and the existing pixel clamp.
            val base = state.requested
            val runtime = JSONObject(base.runtime.toString())
            runtime.getJSONObject("lod").put("qualityPixels", requireNotNull(threshold))
            val validEdge = requireNotNull(longest)
            val request = base.copy(runtime = runtime, maximumInternalEdge = validEdge,
                maximumInternalPixels = minOf(base.maximumInternalPixels, validEdge.toLong() * validEdge),
                generation = Math.addExact(base.generation, 1), configHash = "")
            actions.applySettings(request)
            updateText(editStatus, "已提交保存和应用请求，请查看操作状态；实际生效仍需单独确认。输入继续保留。")
        } catch (_: ArithmeticException) {
            updateText(editStatus, "设置版本无法继续递增，未提交请求；请查看高级状态，编辑内容已保留。")
        } catch (_: Exception) {
            updateText(editStatus, "无法提交当前设置，请查看操作状态或高级状态；编辑内容已保留。")
        }
    }

    private fun loadDraft(config: ViewerConfig) {
        writingDraft = true
        try {
            val edgeValue = config.maximumInternalEdge.toString()
            val qualityValue = config.runtime.optJSONObject("lod")?.opt("qualityPixels")?.toString().orEmpty()
            if (edge.text.toString() != edgeValue) edge.setText(edgeValue)
            if (quality.text.toString() != qualityValue) quality.setText(qualityValue)
            edge.error = null
            quality.error = null
            draftDirty = false
            initializedDraft = true
        } finally { writingDraft = false }
    }

    private fun describe(config: ViewerConfig): String {
        val threshold = config.runtime.optJSONObject("lod")?.optDouble("qualityPixels", Double.NaN)
        val qualityValue = threshold?.takeIf { it.isFinite() }?.toString() ?: "尚未确认"
        return "最长边 ${config.maximumInternalEdge}，像素上限 ${config.maximumInternalPixels}，质量阈值 $qualityValue"
    }

    private fun input(name: String, kind: Int): EditText = EditText(context).apply {
        id = View.generateViewId()
        contentDescription = name
        inputType = kind
        setSingleLine(true)
        GuiStyle.styleInput(this)
        addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) {
                if (!writingDraft) {
                    draftDirty = true
                    error = null
                    updateText(editStatus, "输入尚未保存，关闭面板后仍保留本次编辑。")
                }
            }
        })
    }

    private fun label(value: String, field: EditText): TextView = GuiStyle.body(context, value).apply {
        labelFor = field.id
        setTextColor(GuiStyle.TEXT)
    }

    private fun content(): LinearLayout = LinearLayout(context).apply {
        orientation = VERTICAL
        setPadding(GuiStyle.dp(context, 16), GuiStyle.dp(context, 16),
            GuiStyle.dp(context, 16), GuiStyle.dp(context, 24))
    }

    private fun scroll(content: LinearLayout): ScrollView = ScrollView(context).apply {
        isFillViewport = true
        isClickable = true
        addView(content, LayoutParams(-1, -2))
    }

    private fun updateText(view: TextView, value: String) {
        if (view.text.toString() != value) view.text = value
    }
}
