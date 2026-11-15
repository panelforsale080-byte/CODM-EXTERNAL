#pragma once
#include <cstddef>

void ui_log(const char *fmt, ...);
void ui_log_clear();
void ui_log_copy_all(char *dst, size_t cap);
/** Copy every log line to the Android clipboard. Returns false if the clipboard call failed. */
bool ui_log_copy_to_clipboard();
void ui_log_imgui_panel();
/** height < 0 uses remaining content height */
void ui_log_imgui_panel_ex(float height = 180.f);
