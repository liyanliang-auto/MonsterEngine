package com.monsterengine.viewer

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build

/** Framework-only dataSync notifications. Denied API33 permission does not block
 * legal foreground service startup; the app's persistent job screen stays usable. */
class BuildNotifications(context: Context) {
    private val app = context.applicationContext
    private val manager = app.getSystemService(NotificationManager::class.java)

    fun createChannel() {
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "LOD construction", NotificationManager.IMPORTANCE_LOW)
            .apply { description = "Local Gaussian scene construction, progress and pause/cancel controls" })
    }
    fun permissionGranted(): Boolean = (Build.VERSION.SDK_INT < 33 ||
        app.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) &&
        manager.areNotificationsEnabled()

    fun notification(jobId: String, progress: BuildProgress = BuildProgress(), retired: Boolean = false): Notification {
        val text = when {
            progress.error.isNotBlank() -> progress.error.take(160)
            progress.totalRecords > 0 -> "${progress.stage}: ${progress.processedRecords}/${progress.totalRecords} records"
            progress.processedRecords > 0 -> "${progress.stage}: ${progress.processedRecords} records"
            else -> progress.status.ifBlank { "Preparing local scene construction" }.take(160)
        }
        val launch = app.packageManager.getLaunchIntentForPackage(app.packageName)?.apply {
            putExtra(EXTRA_JOB_ID, jobId); putExtra("screen", "builds")
        }
        val builder = Notification.Builder(app, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("MonsterEngine scene build")
            .setContentText(text).setStyle(Notification.BigTextStyle().bigText(text))
            .setOnlyAlertOnce(true).setOngoing(!retired).setCategory(Notification.CATEGORY_PROGRESS)
        if (launch != null) builder.setContentIntent(PendingIntent.getActivity(app, 0, launch,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE))
        if (!retired) {
            builder.addAction(Notification.Action.Builder(null, "Pause", action(jobId, LodBuildService.ACTION_PAUSE)).build())
            builder.addAction(Notification.Action.Builder(null, "Cancel", action(jobId, LodBuildService.ACTION_CANCEL)).build())
        } else if (progress.stage in setOf(BuildStage.Paused, BuildStage.Cancelled, BuildStage.WaitingForResources, BuildStage.Failed)) {
            builder.addAction(Notification.Action.Builder(null, "Resume", action(jobId, LodBuildService.ACTION_RESUME)).build())
        }
        if (progress.totalRecords > 0 && progress.processedRecords <= progress.totalRecords) {
            val percent = (100.0 * progress.processedRecords / progress.totalRecords).toInt().coerceIn(0, 100)
            builder.setProgress(100, percent, false)
        } else if (!retired) builder.setProgress(0, 0, true)
        return builder.build()
    }

    /** A notification button is explicit user interaction. No timer/background
     * receiver starts/resumes a service or tries to evade platform dataSync quotas. */
    private fun action(jobId: String, action: String): PendingIntent {
        val intent = Intent(app, LodBuildService::class.java).setAction(action)
            .setData(Uri.parse("monster-build://job/$jobId/$action")).putExtra(EXTRA_JOB_ID, jobId)
        return PendingIntent.getForegroundService(app, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    }
    fun post(jobId: String, progress: BuildProgress, retired: Boolean) {
        if (!permissionGranted()) return
        try { manager.notify(NOTIFICATION_ID, notification(jobId, progress, retired)) }
        catch (_: SecurityException) { /* App controls and durable job state remain available. */ }
    }

    companion object {
        const val CHANNEL = "monster-lod-build"
        const val NOTIFICATION_ID = 4101
        const val EXTRA_JOB_ID = "buildJobId"
    }
}
