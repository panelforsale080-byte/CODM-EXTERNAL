#include "ui_core.hpp"
#include "codm_engine.hpp"
#include "proc.hpp"
#include "ui_log.hpp"
#include "clip_android.hpp"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <algorithm>
#include <thread>

ImVec2 initialScreenSize{0.f, 0.f};

static int g_w = 0, g_h = 0;
static std::atomic<bool> g_menu{true};
static std::atomic<bool> g_glReady{false};
static std::atomic<bool> g_runReader{false};
static std::atomic<bool> g_espOn{false};
static std::atomic<pid_t> g_wantPid{0};
static bool g_autoFind = true;
static bool g_patchAnogs = true;
static CodmFrame g_last{};
static std::atomic<bool> g_passThroughReq{false};
static const char *g_glsl = "#version 300 es";

void ui_gl_set_glsl(const char *version) {
    if (version && version[0]) g_glsl = version;
}

static void attach_pid(pid_t p, const char *why) {
    char cmd[256];
    proc_format_cmdline(p, cmd, sizeof(cmd));
    const bool probe = proc_probe_read(p);
    ui_log("[%s] pid=%d probe=%s", why, (int)p, probe ? "OK" : "FAIL");
    ui_log("  cmd: %s", cmd);
    if (!probe) return;
    codm_attach(p);
    g_wantPid.store(p);
    if (g_patchAnogs) codm_auto_patch_anogs();
}

static void reader_loop() {
    auto lastScan = std::chrono::steady_clock::now();
    while (g_runReader.load()) {
        pid_t pid = g_wantPid.load();
        if (g_autoFind && pid <= 0) {
            pid_t p = proc_find_codm_best();
            if (p > 0) attach_pid(p, "auto");
            else {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastScan > std::chrono::seconds(4)) {
                    lastScan = now;
                    ui_log("[auto] scanning com.garena.game.codm…");
                    ProcCandidate c[16];
                    const size_t n = proc_list_codm(c, 16);
                    for (size_t i = 0; i < n; ++i)
                        ui_log("[hit] pid=%d score=%d u=%d a=%d %s", (int)c[i].pid, c[i].score,
                               c[i].unity, c[i].anogs, c[i].cmdline);
                }
            }
            pid = g_wantPid.load();
        }
        if (pid > 0 && !proc_alive(pid)) {
            ui_log("[proc] died — reset");
            codm_reset();
            g_wantPid.store(0);
        } else if (pid > 0 && g_espOn.load()) {
            codm_tick(g_last);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void ui_set_display_size(int w, int h) {
    g_w = w;
    g_h = h;
}

void ui_gl_surface_created() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.IniFilename = nullptr;
    io.FontGlobalScale = 2.1f;
    ImGui::GetStyle().WindowRounding = 8.f;
    ImGui::GetStyle().FrameRounding = 6.f;
    ImGui::GetStyle().TouchExtraPadding = ImVec2(8.f, 8.f);
    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init(g_glsl);
    ui_log("[ui] GLES3 ImGui ready");
}

void ui_gl_surface_changed(int w, int h) {
    g_w = w;
    g_h = h;
    g_glReady.store(true);
    if (initialScreenSize.x <= 0.f)
        initialScreenSize = ImVec2((float)w, (float)h);
}

void ui_gl_surface_destroyed() {
    g_glReady.store(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
}

void ui_touch(int action, float x, float y) {
    if (!g_glReady.load() || !g_menu.load() || !ImGui::GetCurrentContext()) return;
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        io.AddMouseButtonEvent(0, true);
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
        io.AddMouseButtonEvent(0, false);
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        break;
    default:
        break;
    }
}

void ui_open_menu() { g_menu.store(true); }
void ui_toggle_menu() { g_menu.store(!g_menu.load()); }
void ui_minimize_menu() { g_menu.store(false); }
void ui_collapse_main_panel() { g_menu.store(false); }

void ui_request_overlay_pass_through() {
    g_menu.store(false);
    g_passThroughReq.store(true);
}

bool ui_take_overlay_pass_through_request() { return g_passThroughReq.exchange(false); }

bool ui_menu_interactive() { return g_menu.load(); }
bool ui_needs_continuous_gl() { return g_menu.load() || g_espOn.load(); }

void ui_esp_start() {
    g_espOn.store(true);
    g_runReader.store(true);
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(reader_loop).detach();
    });
}

void ui_esp_stop() {
    g_espOn.store(false);
    g_runReader.store(false);
}

bool ui_esp_is_active() { return g_espOn.load(); }

static void draw_menu() {
    const float mw = g_w > 0 ? std::min(560.f, g_w * 0.86f) : 520.f;
    const float mh = g_h > 0 ? std::min(720.f, g_h * 0.8f) : 640.f;
    ImGui::SetNextWindowPos(ImVec2((g_w - mw) * 0.5f, (g_h - mh) * 0.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(mw, mh), ImGuiCond_FirstUseEver);
    ImGui::Begin("CODM", nullptr);
    ImGui::Text("com.garena.game.codm");
    ImGui::Checkbox("Auto find PID", &g_autoFind);
    ImGui::Checkbox("Auto-patch anogs (hooks.h)", &g_patchAnogs);
    if (ImGui::Button("Scan + attach", ImVec2(-1, 0))) {
        pid_t best = proc_find_codm_best();
        if (best > 0) attach_pid(best, "scan");
    }
    if (ImGui::Button("Re-patch anogs (hooks.h)", ImVec2(-1, 0))) codm_auto_patch_anogs();
    if (codm_anogs_hooks_patched())
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "hooks.h BYPASS OK (%d/4)",
                           codm_anogs_last_bypass_count());
    else
        ImGui::Text("hooks.h not applied yet");
    const pid_t pid = g_wantPid.load();
    ImGui::Text("PID %d | %s", (int)pid, g_last.status[0] ? g_last.status : "idle");
    ImGui::Text("unity=0x%llx", (unsigned long long)g_last.unity);
    ImGui::Text("anogs=0x%llx  enemies=%d", (unsigned long long)g_last.anogs, g_last.enemies);
    if (ImGui::Button("Hide menu")) g_menu.store(false);
    ImGui::Separator();
    ui_log_imgui_panel_ex(-1.f);
    ImGui::End();
}

void ui_gl_draw_frame() {
    if (!ImGui::GetCurrentContext() || g_w <= 0 || g_h <= 0) return;
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.f, 0.f, 0.f, 0.45f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_w, g_h);
    ImGui::NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    if (io.DeltaTime <= 0.f) io.DeltaTime = 1.f / 60.f;
    if (g_menu.load()) draw_menu();
    else {
        ImGui::SetNextWindowPos(ImVec2(24.f, 24.f), ImGuiCond_Always);
        ImGui::Begin("##open", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("Open menu")) g_menu.store(true);
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}