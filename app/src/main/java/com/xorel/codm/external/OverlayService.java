package com.xorel.codm.external;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ServiceInfo;
import android.content.res.Configuration;
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

/** One full-screen GLES surface. The menu is ImGui only. */
public class OverlayService extends Service {
    public static final String EXTRA_CMD = "xorel_cmd";
    public static final String CMD_START_ESP = "start_esp";
    public static final String CMD_STOP_ESP = "stop_esp";

    private static final String TAG = "CodmOverlay";
    public static OverlayService instance;

    private final Handler main = new Handler(Looper.getMainLooper());
    private WindowManager wm;
    private GlImGuiView glView;
    private ImageButton logoBtn;
    private WindowManager.LayoutParams surfaceLp;
    private WindowManager.LayoutParams logoLp;
    private int screenW = 1;
    private int screenH = 1;
    private boolean menuExpanded = true;

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
            readScreen();
            NativeBridge.nativeSetDisplaySize(screenW, screenH);

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
            surfaceLp =
                    new WindowManager.LayoutParams(
                            WindowManager.LayoutParams.MATCH_PARENT,
                            WindowManager.LayoutParams.MATCH_PARENT,
                            type,
                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                    | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                                    | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                            PixelFormat.TRANSLUCENT);
            surfaceLp.gravity = Gravity.TOP | Gravity.START;
            surfaceLp.screenOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                surfaceLp.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
            wm.addView(glView, surfaceLp);

            int box = dp(64);
            logoBtn = new ImageButton(this);
            logoBtn.setBackgroundResource(R.drawable.bg_fab_xorel);
            logoBtn.setImageResource(R.drawable.ic_xorel_logo);
            logoBtn.setScaleType(android.widget.ImageView.ScaleType.CENTER_INSIDE);
            logoBtn.setPadding(dp(8), dp(8), dp(8), dp(8));
            logoLp =
                    new WindowManager.LayoutParams(
                            box,
                            box,
                            type,
                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                    | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                            PixelFormat.TRANSLUCENT);
            logoLp.gravity = Gravity.TOP | Gravity.START;
            logoLp.x = dp(12);
            logoLp.y = dp(48);
            logoBtn.setOnClickListener(
                    v -> {
                        try {
                            NativeBridge.nativeOpenMenu();
                        } catch (Throwable t) {
                            Log.w(TAG, "logo", t);
                        }
                    });
            logoBtn.setVisibility(android.view.View.GONE);
            wm.addView(logoBtn, logoLp);
        } catch (Throwable t) {
            Log.e(TAG, "onCreate", t);
            stopSelf();
        }
    }

    private void readScreen() {
        if (wm == null) return;
        DisplayMetrics dm = new DisplayMetrics();
        wm.getDefaultDisplay().getRealMetrics(dm);
        screenW = Math.max(dm.widthPixels, 1);
        screenH = Math.max(dm.heightPixels, 1);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        readScreen();
        try {
            NativeBridge.nativeSetDisplaySize(screenW, screenH);
        } catch (Throwable ignored) {
        }
        if (surfaceLp != null && wm != null && glView != null) {
            surfaceLp.screenOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
            try {
                wm.updateViewLayout(glView, surfaceLp);
            } catch (Throwable ignored) {
            }
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    boolean isMenuInteractive() {
        return menuExpanded;
    }

    /** Full screen. Not touchable when the menu is closed so the game receives taps. */
    void setMenuTouch(boolean interactive) {
        Runnable apply =
                () -> {
                    if (surfaceLp == null || wm == null || glView == null) return;
                    if (interactive == menuExpanded) return;
                    menuExpanded = interactive;
                    readScreen();
                    surfaceLp.width = WindowManager.LayoutParams.MATCH_PARENT;
                    surfaceLp.height = WindowManager.LayoutParams.MATCH_PARENT;
                    surfaceLp.x = 0;
                    surfaceLp.y = 0;
                    int flags =
                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                    | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                                    | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED;
                    if (!interactive) flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
                    surfaceLp.flags = flags;
                    try {
                        wm.updateViewLayout(glView, surfaceLp);
                    } catch (Throwable t) {
                        Log.w(TAG, "overlay flags", t);
                    }
                    if (logoBtn != null) {
                        logoBtn.setVisibility(interactive ? android.view.View.GONE : android.view.View.VISIBLE);
                    }
                };
        if (Looper.myLooper() == Looper.getMainLooper()) apply.run();
        else main.post(apply);
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
            if (wm != null && logoBtn != null) wm.removeView(logoBtn);
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