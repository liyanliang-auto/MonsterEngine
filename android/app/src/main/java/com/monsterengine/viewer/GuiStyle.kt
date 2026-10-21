package com.monsterengine.viewer

import android.app.AlertDialog
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import kotlin.math.roundToInt

/** Shared graphite surfaces and ice-blue controls for the framework Views UI. */
object GuiStyle {
    const val BACKGROUND: Int = 0xFF11171E.toInt()
    const val SURFACE: Int = 0xFF1B252F.toInt()
    const val TEXT: Int = 0xFFF1F5F9.toInt()
    const val MUTED: Int = 0xFFB4C2D0.toInt()
    const val ACCENT: Int = 0xFF70D8FF.toInt()
    const val DANGER: Int = 0xFFFFB5BA.toInt()
    private const val OUTLINE: Int = 0xFF394958.toInt()
    private const val DISABLED: Int = 0xFF8999A8.toInt()

    /** Convert density-independent layout dimensions with the current window density. */
    fun dp(context: Context, value: Int): Int =
        (context.resources.displayMetrics.density * value).roundToInt()

    /** Section heading, wrapping at the user's selected font scale. */
    fun title(context: Context, value: String): TextView = TextView(context).apply {
        text = value
        textSize = 23f
        setTextColor(TEXT)
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(context, 4), 0, dp(context, 12))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        if (android.os.Build.VERSION.SDK_INT >= 28) isAccessibilityHeading = true
    }

    /** Body copy uses a readable secondary color and expands for long messages. */
    fun body(context: Context, value: String): TextView = TextView(context).apply {
        text = value
        textSize = 14f
        setTextColor(MUTED)
        setLineSpacing(dp(context, 3).toFloat(), 1f)
        setPadding(0, dp(context, 3), 0, dp(context, 9))
    }

    /** Named, keyboard-focusable action with a minimum 48dp touch target. */
    fun button(context: Context, label: String, enabled: Boolean = true, action: () -> Unit): Button =
        actionButton(context, label, enabled, ACCENT, 0xFF183648.toInt(), action)

    /** Secondary action retaining the same target size and focus indication. */
    fun outlinedButton(context: Context, label: String, enabled: Boolean = true, action: () -> Unit): Button =
        actionButton(context, label, enabled, TEXT, SURFACE, action)

    /** Destructive actions keep explicit text in addition to their warning color. */
    fun dangerButton(context: Context, label: String, enabled: Boolean = true, action: () -> Unit): Button =
        actionButton(context, label, enabled, DANGER, 0xFF35262E.toInt(), action)

    /** Compact tool with a visible label and matching accessible name. */
    fun iconButton(context: Context, iconRes: Int, label: String, action: () -> Unit): Button =
        outlinedButton(context, label, action = action).apply {
            val icon = context.getDrawable(iconRes)?.mutate()
            icon?.setTintList(textColors)
            icon?.setBounds(0, 0, dp(context, 22), dp(context, 22))
            setCompoundDrawables(null, icon, null, null)
            compoundDrawablePadding = dp(context, 4)
            textSize = 12f
            minHeight = dp(context, 64)
            setPadding(dp(context, 10), dp(context, 8), dp(context, 10), dp(context, 8))
        }

    /** Vertical content card; children can grow without clipping enlarged text. */
    fun card(context: Context): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        background = background(context, SURFACE, 14, OUTLINE)
        setPadding(dp(context, 16), dp(context, 16), dp(context, 16), dp(context, 14))
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(context, 12) }
    }

    /** Consistent app-owned confirmation; callbacks run only on explicit acceptance. */
    fun confirm(context: Context, title: String, message: String, positive: String, action: () -> Unit) {
        val dialog = AlertDialog.Builder(context, R.style.Theme_MonsterEngine_Dialog)
            .setTitle(title)
            .setMessage(message)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(positive) { _, _ -> action() }
            .create()
        dialog.setOnShowListener {
            for (which in listOf(AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE)) {
                dialog.getButton(which)?.apply {
                    minHeight = dp(context, 48)
                    isAllCaps = false
                    setTextColor(ACCENT)
                }
            }
        }
        dialog.show()
    }

    /** Rounded solid panel background with an optional fine outline. */
    fun background(context: Context, color: Int = SURFACE, radius: Int = 12, stroke: Int? = null): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dp(context, radius).toFloat()
            stroke?.let { setStroke(dp(context, 1).coerceAtLeast(1), it) }
        }

    /** Input styling preserves Android selection, cursor and field-error behavior. */
    fun styleInput(input: EditText) {
        input.apply {
            setTextColor(TEXT)
            setHintTextColor(MUTED)
            textSize = 16f
            minHeight = dp(context, 52)
            setPadding(dp(context, 12), dp(context, 10), dp(context, 12), dp(context, 10))
            background = StateListDrawable().apply {
                addState(intArrayOf(android.R.attr.state_focused), background(context, BACKGROUND, 10, ACCENT))
                addState(intArrayOf(), background(context, BACKGROUND, 10, OUTLINE))
            }
            backgroundTintList = null
        }
    }

    /** Explicit spacing between groups without empty spoken text. */
    fun gap(context: Context, height: Int = 12): View = View(context).apply {
        layoutParams = LinearLayout.LayoutParams(1, dp(context, height))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    private fun actionButton(context: Context, label: String, enabled: Boolean, tint: Int,
                             fill: Int, action: () -> Unit): Button = Button(context).apply {
        text = label
        contentDescription = label
        textSize = 14f
        isAllCaps = false
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
        gravity = Gravity.CENTER
        minWidth = 0
        minimumWidth = 0
        minHeight = dp(context, 48)
        minimumHeight = dp(context, 48)
        setPadding(dp(context, 14), dp(context, 10), dp(context, 14), dp(context, 10))
        setTextColor(ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()),
            intArrayOf(DISABLED, tint)))
        val surfaces = StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled), background(context, 0xFF202932.toInt(), 10, OUTLINE))
            addState(intArrayOf(android.R.attr.state_focused), background(context, fill, 10, ACCENT))
            addState(intArrayOf(), background(context, fill, 10, if (tint == TEXT) OUTLINE else tint))
        }
        background = RippleDrawable(ColorStateList.valueOf(0x3370D8FF), surfaces,
            background(context, android.graphics.Color.WHITE, 10))
        backgroundTintList = null
        elevation = 0f
        stateListAnimator = null
        isEnabled = enabled
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(context, 6) }
        setOnClickListener { action() }
    }
}
