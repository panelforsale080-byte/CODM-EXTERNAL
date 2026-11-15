#include "ui_core.hpp"
#include "codm_engine.hpp"
#include "proc.hpp"
#include "ui_log.hpp"
#include "clip_android.hpp"
#include "jnmaini_ui/jnmaini_menu.hpp"

#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#include <EGL/egl.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <thread>

ImVec2 initialScreenSize{0.f, 0.f};

static int g_w = 0, g_h = 0;
static std::atomic<bool> g_menu{true};
static std::atomic<bool> g_runReader{false};
static std::atomic<bool> g_espOn{false};
static std::atomic<pid_t> g_wantPid{0};
static bool g_autoFind = true;
static bool g_patchAnogs = true;
static CodmFrame g_last{};
static bool g_jnmaini_toggled = true;

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
    jnmaini_style_init_once();
    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGui_ImplOpenGL3_CreateFontsTexture();
    ui_log("[ui] GL + jnmaini fonts ready");
}

void ui_gl_surface_changed(int w, int h) {
    g_w = w;
    g_h = h;
    if (initialScreenSize.x <= 0.f)
        initialScreenSize = ImVec2((float)w, (float)h);
}

void ui_gl_surface_destroyed() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    jnmaini_style_shutdown();
}

void ui_touch(int action, float x, float y) {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    switch (action) {
    case 0: // ACTION_DOWN
    case 5: // ACTION_POINTER_DOWN
        io.AddMouseButtonEvent(0, true);
        break;
    case 1: // ACTION_UP
    case 6: // ACTION_POINTER_UP
    case 3: // ACTION_CANCEL
        io.AddMouseButtonEvent(0, false);
        break;
    default:
        break;
    }
}

void ui_open_menu() {
    g_menu.store(true);
    g_jnmaini_toggled = true;
}
void ui_toggle_menu() {
    g_menu.store(!g_menu.load());
    if (g_menu.load()) g_jnmaini_toggled = true;
}
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

void jnmaini_draw_external_panel() {
    ImGui::Text("Target: com.garena.game.codm");
    ImGui::Checkbox("Auto find PID", &g_autoFind);
    ImGui::Checkbox("Auto-patch anogs (hooks.h)", &g_patchAnogs);
    if (ImGui::Button("Scan + attach")) {
        pid_t best = proc_find_codm_best();
        if (best > 0) attach_pid(best, "scan");
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-patch anogs")) codm_auto_patch_anogs();
    const pid_t pid = g_wantPid.load();
    ImGui::Text("PID %d | %s", (int)pid, g_last.status[0] ? g_last.status : "idle");
    ImGui::Text("unity=0x%llx anogs=0x%llx enemies=%d", (unsigned long long)g_last.unity,
                (unsigned long long)g_last.anogs, g_last.enemies);
    ui_log_imgui_panel();
}

void ui_gl_draw_frame() {
    if (!ImGui::GetCurrentContext() || g_w <= 0 || g_h <= 0) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_w, g_h);
    ImGui::NewFrame();
    if (g_menu.load()) {
        ImGuiIO &io = ImGui::GetIO();
        if (io.DeltaTime <= 0.f) io.DeltaTime = 1.f / 60.f;
        jnmaini_draw_frame(true, g_jnmaini_toggled, &g_last);
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}