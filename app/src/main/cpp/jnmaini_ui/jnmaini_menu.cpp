#include "jnmaini_menu.hpp"
#include "things.h"
#include "widgets.hpp"
#include "weapon_category_config.h"
#include "ui_maps.h"

#include "Icon.h"
#include "Iconcpp.h"

#include "codm_engine.hpp"
#include "ui_log.hpp"
#include "imgui.h"

#include <cfloat>
#include <cstring>

static CustomWidgets g_widgets;
static ImFont* Crozn = nullptr;
static bool g_styleDone = false;

void jnmaini_style_init_once() {
    if (g_styleDone) return;
    g_styleDone = true;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.145f, 0.145f, 0.16f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.145f, 0.145f, 0.16f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.135f, 0.135f, 0.15f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.165f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.44f, 0.65f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.44f, 0.65f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.52f, 0.54f, 0.75f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.165f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.42f, 0.44f, 0.65f, 0.4f);

    style.WindowRounding = 20.0f;
    style.ChildRounding = 20.0f;
    style.FrameRounding = 20.0f;
    style.PopupRounding = 20.0f;
    style.ScrollbarRounding = 20.0f;
    style.GrabRounding = 20.0f;
    style.TabRounding = 20.0f;
    style.FrameBorderSize = 3.58f;
    style.WindowBorderSize = 3.58f;
    style.ScrollbarSize = 24.673f;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.ConfigWindowsMoveFromTitleBarOnly = false;
    io.IniFilename = nullptr;
    style.TouchExtraPadding = ImVec2(6.f, 6.f);

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 24.0f;
    Crozn = io.Fonts->AddFontDefault(&font_cfg);
    static const ImWchar icons_ranges[] = {0xf000, 0xf8ff, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    if (font_awesome_size > 0) {
        io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 24.0f, &icons_config,
                                                icons_ranges);
    }
    io.Fonts->Build();
}

void jnmaini_style_shutdown() {
    g_styleDone = false;
    Crozn = nullptr;
}

ImFont *jnmaini_title_font() { return Crozn; }

static void draw_main_menu(bool& isToggled) {
    if (!isToggled) return;
    g_jnmaini_menu_open = &isToggled;
#include "menu_body.inc"
    g_jnmaini_menu_open = nullptr;
}

void jnmaini_draw_frame(bool menuOpen, bool& isToggled, const CodmFrame* frame) {
    (void)frame;
    if (!menuOpen) return;
    draw_main_menu(isToggled);
}