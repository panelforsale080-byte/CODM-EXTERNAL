#include "ui_core.hpp"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <atomic>
#include <thread>

#define EGLTAG "CodmEgl"

static std::atomic<bool> g_alive{false};
static std::thread g_thread;

static void egl_loop(ANativeWindow *window) {
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        __android_log_print(ANDROID_LOG_ERROR, EGLTAG, "eglInitialize failed");
        ANativeWindow_release(window);
        return;
    }
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE,    EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                              EGL_RED_SIZE,        8,
                              EGL_GREEN_SIZE,      8,
                              EGL_BLUE_SIZE,       8,
                              EGL_ALPHA_SIZE,      8,
                              EGL_NONE};
    EGLConfig cfg = nullptr;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg) || ncfg < 1) {
        __android_log_print(ANDROID_LOG_ERROR, EGLTAG, "no GLES3 config");
        eglTerminate(dpy);
        ANativeWindow_release(window);
        return;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, window, nullptr);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        __android_log_print(ANDROID_LOG_ERROR, EGLTAG, "context/surface failed err=0x%x", eglGetError());
        if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        ANativeWindow_release(window);
        return;
    }
    eglSwapInterval(dpy, 1);
    __android_log_print(ANDROID_LOG_INFO, EGLTAG, "GLES3 context ready (jnmaini #version 300 es)");
    ui_gl_surface_created();
    while (g_alive.load()) {
        EGLint w = 0, h = 0;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        if (w > 0 && h > 0) ui_gl_surface_changed((int)w, (int)h);
        ui_gl_draw_frame();
        ui_finish_gl_frame();
        if (!eglSwapBuffers(dpy, surf)) break;
    }
    ui_gl_surface_destroyed();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    ANativeWindow_release(window);
    __android_log_print(ANDROID_LOG_INFO, EGLTAG, "GLES3 context stopped");
}

void ui_attach_native_window(JNIEnv *env, jobject surface) {
    ui_detach_native_window();
    if (!surface) return;
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window) return;
    g_alive.store(true);
    g_thread = std::thread(egl_loop, window);
}

void ui_detach_native_window() {
    g_alive.store(false);
    if (g_thread.joinable()) g_thread.join();
}