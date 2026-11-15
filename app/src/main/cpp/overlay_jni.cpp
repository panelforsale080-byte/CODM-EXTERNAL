#include "ui_core.hpp"
#include "clip_android.hpp"
#include <android/native_window_jni.h>
#include <jni.h>

static JavaVM *g_vm = nullptr;
static jclass g_bridgeCls = nullptr;
static jmethodID g_setMenuTouch = nullptr;
static bool g_menuTouchNotified = false;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

static void notify_menu_touch(bool on) {
    if (!g_vm || !g_bridgeCls || !g_setMenuTouch) return;
    JNIEnv *env = nullptr;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
    env->CallStaticVoidMethod(g_bridgeCls, g_setMenuTouch, on ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeInit(JNIEnv *env, jclass clazz) {
    if (!g_bridgeCls) g_bridgeCls = (jclass)env->NewGlobalRef(clazz);
    if (!g_setMenuTouch)
        g_setMenuTouch = env->GetStaticMethodID(clazz, "applyMenuTouch", "(Z)V");
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeSetContext(JNIEnv *env, jclass, jobject ctx) {
    android_clip_set_context(g_vm, ctx);
    (void)env;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeSetDisplaySize(JNIEnv *, jclass, jint w, jint h) {
    ui_set_display_size((int)w, (int)h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeGlSurfaceCreated(JNIEnv *, jclass) {
    g_menuTouchNotified = false;
    ui_gl_surface_created();
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeGlSurfaceChanged(JNIEnv *, jclass, jint w, jint h) {
    ui_gl_surface_changed((int)w, (int)h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeGlSurfaceDestroyed(JNIEnv *, jclass) {
    ui_gl_surface_destroyed();
}

void ui_sync_menu_touch_state() {
    const bool now = ui_menu_interactive();
    if (now == g_menuTouchNotified) return;
    g_menuTouchNotified = now;
    notify_menu_touch(now);
}

static void force_pass_through_notify() {
    g_menuTouchNotified = true;
    notify_menu_touch(false);
    g_menuTouchNotified = false;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeGlDrawFrame(JNIEnv *, jclass) {
    ui_gl_draw_frame();
    if (ui_take_overlay_pass_through_request()) {
        force_pass_through_notify();
    } else {
        ui_sync_menu_touch_state();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeTouch(JNIEnv *, jclass, jint action, jfloat x,
                                                      jfloat y) {
    ui_touch((int)action, (float)x, (float)y);
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeOpenMenu(JNIEnv *,
                                                                                           jclass) {
    ui_open_menu();
    g_menuTouchNotified = false;
    notify_menu_touch(true);
    g_menuTouchNotified = true;
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeToggleMenu(
    JNIEnv *, jclass) {
    ui_toggle_menu();
    g_menuTouchNotified = ui_menu_interactive();
    notify_menu_touch(g_menuTouchNotified);
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeMinimizeMenu(
    JNIEnv *, jclass) {
    ui_minimize_menu();
    force_pass_through_notify();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeIsMenuInteractive(JNIEnv *, jclass) {
    return ui_menu_interactive() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeNeedsContinuousGl(JNIEnv *, jclass) {
    return ui_needs_continuous_gl() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeStartEsp(JNIEnv *, jclass) {
    ui_esp_start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeStopEsp(JNIEnv *, jclass) {
    ui_esp_stop();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeIsEspActive(JNIEnv *, jclass) {
    return ui_esp_is_active() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeHeroComboPulse(JNIEnv *, jclass) {}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeSetRecordingHide(JNIEnv *, jclass, jboolean) {}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeSetMenuVideoMuted(JNIEnv *, jclass, jboolean) {}