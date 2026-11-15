#pragma once
#include "imgui.h"
#include <map>
#include <string>

extern bool* g_jnmaini_menu_open;

bool CustomSliderFloat(const char* label, float* v, float v_min, float v_max,
                       const char* format = "%.0f");
void DrawMacOSHeaderButtons();
void jnmaini_request_overlay_pass_through();
extern std::map<std::string, ImVec4> colorStates;

#define SLIDER(label, v, v_min, v_max, ...) CustomSliderFloat(label, v, v_min, v_max, ##__VA_ARGS__)