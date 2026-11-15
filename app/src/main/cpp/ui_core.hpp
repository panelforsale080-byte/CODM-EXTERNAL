#pragma once
#include <jni.h>
#include "imgui.h"

void ui_set_display_size(int w, int h);
void ui_gl_surface_created();
void ui_gl_set_glsl(const char *version);
void ui_gl_surface_changed(int w, int h);
void ui_gl_surface_destroyed();
void ui_gl_draw_frame();
void ui_touch(int action, float x, float y);
void ui_open_menu();
void ui_toggle_menu();
void ui_minimize_menu();
/** Hide main panel only; logo HUD stays, overlay still captures touch. */
void ui_collapse_main_panel();
/** Pass-through overlay + show FAB (deferred until after ImGui frame). */
void ui_request_overlay_pass_through();
bool ui_take_overlay_pass_through_request();
bool ui_menu_interactive();
void ui_esp_start();
void ui_esp_stop();
bool ui_esp_is_active();
bool ui_needs_continuous_gl();
void ui_sync_menu_touch_state();
void ui_finish_gl_frame();
void ui_attach_native_window(JNIEnv *env, jobject surface);
void ui_detach_native_window();