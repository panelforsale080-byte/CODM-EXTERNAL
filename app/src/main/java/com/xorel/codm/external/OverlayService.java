package com.xorel.codm.external;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.ImageButton;

/**
 * Overlay is one GLES surface (the ImGui menu) plus a logo button.
 * Video, music, and recording-hide windows were removed — they never drew the menu.
 */
public class OverlayService extends Service {
    public static final String EXTRA_CMD = "xorel_cmd";
    public static final String CMD_START_ESP = "start_esp";
    public static final String CMD_STOP_ESP = "stop_esp";

    private static final String TAG = "XorelOverlay";
    public static OverlayService instance;

    private final Handler main = new Handler(Looper.getMainLooper());
    private WindowManager wm;
    private GlImGuiView glView;
    private ImageButton fabLogo;
    private WindowManager.LayoutParams surfaceLp;
    private WindowManager.LayoutParams fabLp;
    private boolean menuInteractive = true;
    private boolean glReady;
    private String pendingCmd;

    private static final int SURFACE_FLAGS =
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED;

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
            attach(w, h);
        } catch (Throwable t) {
            Log.e(TAG, "onCreate", t);
            stopSelf();
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private void attach(int w, int h) {
        final int type =
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                        ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                        : WindowManager.LayoutParams.TYPE_PHONE;

        glView = new GlImGuiView(this);
        glView.configureOverlaySurface();
        glView.setOnGlReadyListener(
                v ->
                        main.post(
                                () -> {
                                    glReady = true;
                                    menuInteractive = true;
                                    try {
                                        NativeBridge.nativeOpenMenu();
                                        NativeBridge.nativeStartEsp();
                                    } catch (Throwable t) {
                                        Log.e(TAG, "open", t);
                                    }
                                    Log.i(TAG, "GLES menu surface ready");
                                }));

        surfaceLp =
                new WindowManager.LayoutParams(
                        w, h, type, SURFACE_FLAGS, PixelFormat.TRANSLUCENT);
        surfaceLp.gravity = Gravity.TOP | Gravity.START;
        surfaceLp.x = 0;
        surfaceLp.y = 0;
        wm.addView(glView, surfaceLp);

        int fabSize = dp(56);
        fabLogo = new ImageButton(this);
        fabLogo.setBackgroundResource(R.drawable.bg_fab_xorel);
        fabLogo.setImageResource(R.drawable.ic_xorel_logo);
        fabLogo.setScaleType(android.widget.ImageView.ScaleType.CENTER_INSIDE);
        fabLogo.setPadding(dp(8), dp(8), dp(8), dp(8));
        fabLp =
                new WindowManager.LayoutParams(
                        fabSize,
                        fabSize,
                        type,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
        fabLp.gravity = Gravity.TOP | Gravity.START;
        fabLp.x = Math.max(0, w - fabSize - dp(12));
        fabLp.y = dp(48);
        fabLogo.setOnClickListener(
                v -> {
                    menuInteractive = true;
                    NativeBridge.nativeOpenMenu();
                });
        wm.addView(fabLogo, fabLp);
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
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(
                    new NotificationChannel(
                            "esp", getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW));
            Notification n =
                    new Notification.Builder(this, "esp")
                            .setContentTitle(getString(R.string.notif_title))
                            .setContentText(getString(R.string.notif_text))
                            .setSmallIcon(android.R.drawable.ic_menu_compass)
                            .build();
            try {
                if (Build.VERSION.SDK_INT >= 34) {
                    startForeground(7, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
                } else {
                    startForeground(7, n);
                }
            } catch (Throwable t) {
                startForeground(7, n);
            }
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && glReady) {
            String cmd = intent.getStringExtra(EXTRA_CMD);
            if (CMD_START_ESP.equals(cmd)) NativeBridge.nativeStartEsp();
            else if (CMD_STOP_ESP.equals(cmd)) NativeBridge.nativeStopEsp();
        } else if (intent != null) {
            pendingCmd = intent.getStringExtra(EXTRA_CMD);
        }
        if (glReady && pendingCmd != null) {
            if (CMD_START_ESP.equals(pendingCmd)) NativeBridge.nativeStartEsp();
            else if (CMD_STOP_ESP.equals(pendingCmd)) NativeBridge.nativeStopEsp();
            pendingCmd = null;
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        try {
            if (glView != null) glView.releaseGl();
        } catch (Throwable ignored) {
        }
        try {
            if (wm != null && fabLogo != null) wm.removeView(fabLogo);
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