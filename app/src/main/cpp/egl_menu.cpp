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

static bool make_gles(EGLDisplay dpy, ANativeWindow *window, int version, EGLConfig *outCfg, EGLContext *outCtx,
                       EGLSurface *outSurf) {
    const EGLint renderable = version >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              renderable,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_NONE};
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgAttr, outCfg, 1, &ncfg) || ncfg < 1) return false;
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
    *outCtx = eglCreateContext(dpy, *outCfg, EGL_NO_CONTEXT, ctxAttr);
    *outSurf = eglCreateWindowSurface(dpy, *outCfg, window, nullptr);
    if (*outCtx == EGL_NO_CONTEXT || *outSurf == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(dpy, *outSurf, *outSurf, *outCtx)) return false;
    return true;
}

static void egl_loop(ANativeWindow *window) {
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        __android_log_print(ANDROID_LOG_ERROR, EGLTAG, "eglInitialize failed");
        ANativeWindow_release(window);
        return;
    }
    EGLConfig cfg = nullptr;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
    if (!make_gles(dpy, window, 3, &cfg, &ctx, &surf)) {
        __android_log_print(ANDROID_LOG_ERROR, EGLTAG, "GLES3 failed err=0x%x", eglGetError());
        if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        ANativeWindow_release(window);
        return;
    }
    eglSwapInterval(dpy, 1);
    ui_gl_set_glsl("#version 300 es");
    __android_log_print(ANDROID_LOG_INFO, EGLTAG, "menu GLES3 ready");
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