package com.xorel.codm.external;

import android.content.Context;
import android.graphics.PixelFormat;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/** Overlay surface only. GLES 3 + ImGui run on a native thread (jnmaini egl path). */
public class GlImGuiView extends SurfaceView implements SurfaceHolder.Callback {
    private static final String TAG = "GlImGuiView";
    private volatile boolean surfaceReady = false;

    public interface OnGlReadyListener {
        void onGlReady(GlImGuiView view);
    }

    private OnGlReadyListener readyListener;

    public GlImGuiView(Context context) {
        super(context);
        initSurface();
    }

    public GlImGuiView(Context context, AttributeSet attrs) {
        super(context, attrs);
        initSurface();
    }

    public void setOnGlReadyListener(OnGlReadyListener listener) {
        readyListener = listener;
    }

    public void configureOverlaySurface() {
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
    }

    private void initSurface() {
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        getHolder().addCallback(this);
    }

    public void releaseGl() {
        surfaceReady = false;
        try {
            NativeBridge.nativeDetachWindow();
        } catch (Throwable t) {
            Log.e(TAG, "detach", t);
        }
    }

    public void requestRender() {}

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        try {
            NativeBridge.nativeAttachWindow(holder.getSurface());
            surfaceReady = true;
            if (readyListener != null) readyListener.onGlReady(this);
        } catch (Throwable t) {
            Log.e(TAG, "surfaceCreated", t);
            surfaceReady = false;
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        try {
            NativeBridge.nativeDetachWindow();
        } catch (Throwable t) {
            Log.e(TAG, "surfaceDestroyed", t);
        }
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (!surfaceReady) return false;
        OverlayService svc = OverlayService.instance;
        if (svc == null || !svc.isMenuInteractive()) return false;
        return super.dispatchTouchEvent(event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!surfaceReady || !NativeBridge.nativeIsMenuInteractive()) return false;
        try {
            NativeBridge.nativeTouch(event.getActionMasked(), event.getX(), event.getY());
        } catch (Throwable t) {
            Log.e(TAG, "touch", t);
            return false;
        }
        return true;
    }
}