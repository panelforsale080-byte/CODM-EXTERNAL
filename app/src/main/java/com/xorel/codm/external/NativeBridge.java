package com.xorel.codm.external;

public final class NativeBridge {
    static {
        System.loadLibrary("CodmExternal");
    }

    public static native void nativeInit();
    public static native void nativeSetContext(Object context);
    public static native void nativeSetDisplaySize(int width, int height);

    public static native void nativeGlSurfaceCreated();
    public static native void nativeGlSurfaceChanged(int w, int h);
    public static native void nativeGlSurfaceDestroyed();
    public static native void nativeGlDrawFrame();
    /** Native GLES3 ImGui thread (same GLSL as jnmaini eglSwapBuffers). */
    public static native void nativeAttachWindow(Object surface);
    public static native void nativeDetachWindow();

    public static native void nativeTouch(int action, float x, float y);
    public static native void nativeOpenMenu();
    public static native void nativeToggleMenu();
    /** ImGui menu off + overlay pass-through (FABs visible). */
    public static native void nativeMinimizeMenu();
    public static native boolean nativeIsMenuInteractive();
    /** True while the menu or ESP boxes need frames. Retri does not. */
    public static native boolean nativeNeedsContinuousGl();
    public static native void nativeStartEsp();
    public static native void nativeStopEsp();
    /** One tap = one Ling sword / Gusion combo (when enabled in menu). */
    public static native void nativeHeroComboPulse();

    public static void applyMenuTouch(boolean menuInteractive) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.setMenuTouch(menuInteractive);
    }

    public static void setComboFabVisible(boolean visible) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.setComboFabVisible(visible);
    }

    public static void updateMenuVideoLayout(boolean visible, int x, int y, int w, int h) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.updateMenuVideoLayout(visible, x, y, w, h);
    }

    public static void applyMenuVideoMuted(boolean muted) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.applyMenuVideoMuted(muted);
    }

    /** FLAG_SECURE on overlay (blocks screenshots / screen record when ON). */
    public static void applyAntiCaptureSecure(boolean on) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.applyAntiCaptureSecure(on);
    }

    /** Menu toggle: hide ESP drawings and the logo while a screen recording is active. */
    public static void setHideWhileRecording(boolean on) {
        OverlayService svc = OverlayService.instance;
        if (svc != null) svc.setHideWhileRecording(on);
    }

    public static native void nativeSetRecordingHide(boolean hide);

    public static native void nativeSetMenuVideoMuted(boolean muted);

    public static void musicRefresh() {}

    public static void musicPlay(int index) {}

    public static void musicSetVolume(int percent) {}

    public static void musicSetMuted(boolean muted) {}

    public static void musicStop() {}

    public static String musicSnapshot() {
        return "";
    }
}
