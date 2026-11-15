#include "ui_core.hpp"
#include "clip_android.hpp"
#include <android/native_window_jni.h>
#include <jni.h>

static JavaVM *g_vm = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeInit(JNIEnv *, jclass) {}

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

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeGlDrawFrame(JNIEnv *, jclass) {
    ui_gl_draw_frame();
}

extern "C" JNIEXPORT void JNICALL
Java_com_xorel_codm_external_NativeBridge_nativeTouch(JNIEnv *, jclass, jint action, jfloat x,
                                                      jfloat y) {
    ui_touch((int)action, (float)x, (float)y);
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeOpenMenu(JNIEnv *,
                                                                                           jclass) {
    ui_open_menu();
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeToggleMenu(
    JNIEnv *, jclass) {
    ui_toggle_menu();
}

extern "C" JNIEXPORT void JNICALL Java_com_xorel_codm_external_NativeBridge_nativeMinimizeMenu(
    JNIEnv *, jclass) {
    ui_minimize_menu();
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