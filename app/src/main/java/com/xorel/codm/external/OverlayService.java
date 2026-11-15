package com.xorel.codm.external;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.WindowManager;

/** One full-screen GLES surface. The menu is ImGui only. */
public class OverlayService extends Service {
    public static final String EXTRA_CMD = "xorel_cmd";
    public static final String CMD_START_ESP = "start_esp";
    public static final String CMD_STOP_ESP = "stop_esp";

    private static final String TAG = "CodmOverlay";
    public static OverlayService instance;

    private WindowManager wm;
    private GlImGuiView glView;
    private boolean menuInteractive = true;

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            stopSelf();
            return;
        }
        try {
            NativeBridge.nativeInit();
            NativeBridge.nativeSetContext(getApplicationContext());
            startForegroundNotif();
            wm = (WindowManager) getSystemService(WINDOW_SERVICE);
            DisplayMetrics dm = new DisplayMetrics();
            wm.getDefaultDisplay().getRealMetrics(dm);
            int w = Math.max(dm.widthPixels, 1);
            int h = Math.max(dm.heightPixels, 1);
            NativeBridge.nativeSetDisplaySize(w, h);

            glView = new GlImGuiView(this);
            glView.configureOverlaySurface();
            glView.setOnGlReadyListener(
                    v -> {
                        try {
                            NativeBridge.nativeOpenMenu();
                            NativeBridge.nativeStartEsp();
                        } catch (Throwable t) {
                            Log.e(TAG, "open", t);
                        }
                    });
            int type =
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                            ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                            : WindowManager.LayoutParams.TYPE_PHONE;
            WindowManager.LayoutParams lp =
                    new WindowManager.LayoutParams(
                            w,
                            h,
                            type,
                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                    | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                                    | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                            PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.TOP | Gravity.START;
            wm.addView(glView, lp);
        } catch (Throwable t) {
            Log.e(TAG, "onCreate", t);
            stopSelf();
        }
    }

    boolean isMenuInteractive() {
        return menuInteractive;
    }

    void setMenuTouch(boolean interactive) {
        menuInteractive = interactive;
    }

    void setComboFabVisible(boolean want) {}

    void updateMenuVideoLayout(boolean visible, int x, int y, int w, int h) {}

    void applyMenuVideoMuted(boolean muted) {}

    void applyAntiCaptureSecure(boolean on) {}

    void setHideWhileRecording(boolean on) {}

    private void startForegroundNotif() {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(
                new NotificationChannel("esp", "CODM", NotificationManager.IMPORTANCE_LOW));
        Notification n =
                new Notification.Builder(this, "esp")
                        .setContentTitle("CODM")
                        .setContentText("ImGui overlay")
                        .setSmallIcon(android.R.drawable.ic_menu_compass)
                        .build();
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(7, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(7, n);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        try {
            if (glView != null) glView.releaseGl();
        } catch (Throwable ignored) {
        }
        try {
            if (wm != null && glView != null) wm.removeView(glView);
        } catch (Throwable ignored) {
        }
        instance = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}