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
    private boolean attached;

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
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (attached || width <= 0 || height <= 0) return;
        attached = true;
        try {
            NativeBridge.nativeAttachWindow(holder.getSurface());
            surfaceReady = true;
            if (readyListener != null) readyListener.onGlReady(this);
        } catch (Throwable t) {
            Log.e(TAG, "surfaceChanged", t);
            attached = false;
            surfaceReady = false;
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        attached = false;
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
        return super.dispatchTouchEvent(event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!surfaceReady) return false;
        try {
            NativeBridge.nativeTouch(event.getActionMasked(), event.getX(), event.getY());
        } catch (Throwable t) {
            Log.e(TAG, "touch", t);
            return false;
        }
        return true;
    }
}