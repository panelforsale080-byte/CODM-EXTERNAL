#include "ui_core.hpp"
#include "codm_engine.hpp"
#include "proc.hpp"
#include "ui_log.hpp"
#include "clip_android.hpp"

#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#include <EGL/egl.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <thread>

static int g_w = 0, g_h = 0;
static std::atomic<bool> g_menu{true};
static std::atomic<bool> g_runReader{false};
static std::atomic<bool> g_espOn{false};
static std::atomic<pid_t> g_wantPid{0};
static bool g_autoFind = true;
static bool g_patchAnogs = true;
static CodmFrame g_last{};

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
    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");
}

void ui_gl_surface_changed(int w, int h) {
    g_w = w;
    g_h = h;
}

void ui_gl_surface_destroyed() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
}

void ui_touch(int action, float x, float y) {
    ImGui_ImplAndroid_HandleInputEvent(nullptr);
    (void)action;
    (void)x;
    (void)y;
}

void ui_open_menu() { g_menu.store(true); }
void ui_toggle_menu() { g_menu.store(!g_menu.load()); }
void ui_minimize_menu() { g_menu.store(false); }
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
void ui_sync_menu_touch_state() {}

static void draw_menu() {
    ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);
    bool menuOpen = g_menu.load();
    if (!ImGui::Begin("CODM EXTERNAL (jnmaini)", &menuOpen)) {
        g_menu.store(menuOpen);
        ImGui::End();
        return;
    }
    g_menu.store(menuOpen);
    ImGui::Text("Target: com.garena.game.codm");
    ImGui::Checkbox("Auto find PID", &g_autoFind);
    ImGui::Checkbox("Auto-patch anogs (hooks.h BYPASS)", &g_patchAnogs);
    if (ImGui::Button("Scan + attach")) {
        ProcCandidate c[16];
        proc_list_codm(c, 16);
        pid_t best = proc_find_codm_best();
        if (best > 0) attach_pid(best, "scan");
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-patch anogs")) codm_auto_patch_anogs();

    const pid_t pid = g_wantPid.load();
    ImGui::SeparatorText("Status");
    ImGui::Text("PID: %d", (int)pid);
    ImGui::TextWrapped("%s", g_last.status[0] ? g_last.status : "idle");
    ImGui::Text("unity=0x%llx anogs=0x%llx enemies=%d",
                (unsigned long long)g_last.unity, (unsigned long long)g_last.anogs, g_last.enemies);

    ImGui::SeparatorText("Log");
    ui_log_imgui_panel();
    ImGui::End();
}

void ui_gl_draw_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    if (g_menu.load()) draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}