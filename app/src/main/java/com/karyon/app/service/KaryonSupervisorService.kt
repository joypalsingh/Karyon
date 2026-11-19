package com.karyon.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder

class KaryonSupervisorService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val instanceName = intent?.getStringExtra(EXTRA_VM_NAME) ?: "AOSP Container"
        ensureChannel()
        val notification = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Karyon Container Active")
            .setContentText("Supervising $instanceName")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .build()
        startForeground(NOTIFICATION_ID, notification)
        return START_NOT_STICKY
    }

    private fun ensureChannel() {
        val mgr = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (mgr.getNotificationChannel(CHANNEL_ID) == null) {
            val ch = NotificationChannel(
                CHANNEL_ID,
                "Karyon Container Supervisor",
                NotificationManager.IMPORTANCE_LOW
            )
            mgr.createNotificationChannel(ch)
        }
    }

    companion object {
        private const val CHANNEL_ID = "karyon_supervisor"
        private const val NOTIFICATION_ID = 4041
        const val EXTRA_VM_NAME = "vm_name"

        fun start(context: Context, vmName: String) {
            val intent = Intent(context, KaryonSupervisorService::class.java).apply {
                putExtra(EXTRA_VM_NAME, vmName)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, KaryonSupervisorService::class.java))
        }
    }
}
