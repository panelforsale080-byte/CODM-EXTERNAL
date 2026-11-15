#include "ui_core.hpp"
#include "codm_engine.hpp"
#include "proc.hpp"
#include "ui_log.hpp"
#include "clip_android.hpp"
#include "jnmaini_ui/jnmaini_menu.hpp"
#include "jnmaini_ui/weapon_category_config.h"
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
#include <cmath>
#include <fstream>
#include <string>
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
static bool g_panelOpen = false;
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
        if (pid > 0 && g_espOn.load() && !codm_libs_ready()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastScan > std::chrono::seconds(2)) {
                lastScan = now;
                pid_t better = proc_find_codm_best();
                if (better > 0 && better != pid) {
                    int u = 0, a = 0;
                    std::ifstream maps("/proc/" + std::to_string(better) + "/maps");
                    std::string line;
                    while (std::getline(maps, line)) {
                        if (line.find("libunity.so") != std::string::npos) u = 1;
                        if (line.find("libanogs.so") != std::string::npos) a = 1;
                    }
                    if (u || a) {
                        ui_log("[auto] libs on pid=%d (unity=%d anogs=%d) — switch", (int)better, u, a);
                        attach_pid(better, "switch");
                        pid = g_wantPid.load();
                    }
                }
                if (!codm_libs_ready()) {
                    ui_log("[codm] libs not mapped yet — reread /proc/%d/maps", (int)pid);
                    codm_attach(pid);
                    if (codm_libs_ready() && g_patchAnogs) codm_auto_patch_anogs();
                }
            }
        }
        if (pid > 0 && !proc_alive(pid)) {
            ui_log("[proc] died — reset");
            codm_reset();
            g_wantPid.store(0);
        } else if (pid > 0 && g_espOn.load()) {
            codm_tick(g_last);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
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
    jnmaini_style_shutdown();
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

void ui_open_menu() {
    g_menu.store(true);
    g_panelOpen = true;
}
void ui_toggle_menu() { g_menu.store(!g_menu.load()); }
void ui_minimize_menu() { g_menu.store(false); }
void ui_collapse_main_panel() { g_menu.store(false); }

void ui_request_overlay_pass_through() {
    g_menu.store(false);
    g_passThroughReq.store(true);
}

bool ui_take_overlay_pass_through_request() { return g_passThroughReq.exchange(false); }

bool ui_menu_interactive() { return g_panelOpen; }
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

void jnmaini_draw_external_panel() {
    ImGui::Text("Target: com.garena.game.codm");
    ImGui::Checkbox("Auto find PID", &g_autoFind);
    ImGui::Checkbox("Auto-patch anogs (hooks.h)", &g_patchAnogs);
    if (ImGui::Button("Scan + attach")) {
        pid_t best = proc_find_codm_best();
        if (best > 0) attach_pid(best, "scan");
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-patch anogs (hooks.h)")) codm_auto_patch_anogs();
    ImGui::SameLine();
    if (ImGui::Button("Copy logs all")) ui_log_copy_to_clipboard();
    if (codm_anogs_hooks_patched())
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "hooks.h BYPASS OK (%d/4)",
                           codm_anogs_last_bypass_count());
    else
        ImGui::Text("hooks.h not applied yet");
    ImGui::Text("unity features on %d", codm_unity_hook_count());
    const pid_t pid = g_wantPid.load();
    ImGui::Text("PID %d | %s", (int)pid, g_last.status[0] ? g_last.status : "idle");
    ImGui::Text("unity=0x%llx anogs=0x%llx enemies=%d", (unsigned long long)g_last.unity,
                (unsigned long long)g_last.anogs, g_last.enemies);
}

static bool project_esp(const CodmFrame &f, float x, float y, float z, float &sx, float &sy) {
    if (!f.viewOk || g_w <= 0 || g_h <= 0) return false;
    const float dx = x - f.camX;
    const float dy = y - f.camY;
    const float dz = z - f.camZ;
    const float vz = dx * f.fx + dy * f.fy + dz * f.fz;
    if (vz < 0.3f) return false;
    const float vx = dx * f.rx + dy * f.ry + dz * f.rz;
    const float vy = dx * f.ux + dy * f.uy + dz * f.uz;
    const float scale = (g_h * 0.5f) / std::tan(70.f * 0.5f * 3.14159265f / 180.f);
    sx = g_w * 0.5f + (vx / vz) * scale;
    sy = g_h * 0.5f - (vy / vz) * scale;
    return std::isfinite(sx) && std::isfinite(sy);
}

static void draw_world_esp(const CodmFrame &f) {
    if (!ImGui::GetCurrentContext()) return;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    char head[48];
    std::snprintf(head, sizeof(head), "ENEMIES %d", f.enemies);
    dl->AddText(ImVec2(g_w * 0.5f - 36.f, 36.f), IM_COL32(255, 255, 255, 230), head);
    const bool master = checkboxStates["ESP ENABLE"];
    if (!master || !f.viewOk) return;
    const bool name = checkboxStates["ESP NAME"];
    const bool dist = checkboxStates["ESP DISTANCE"];
    for (int i = 0; i < f.shown; ++i) {
        const CodmEnemy &e = f.list[i];
        if (!e.alive) continue;
        if (e.x == 0.f && e.y == 0.f && e.z == 0.f) continue;
        float hx = 0, hy = 0, fx = 0, fy = 0;
        if (!project_esp(f, e.x, e.head, e.z, hx, hy)) continue;
        if (!project_esp(f, e.x, e.y, e.z, fx, fy)) continue;
        (void)fx;
        const float top = hy < fy ? hy : fy;
        const float bot = hy < fy ? fy : hy;
        const float bh = bot - top;
        if (bh < 2.f || bh > g_h * 2.f) continue;
        const float bw = bh * 0.45f;
        const ImU32 col = e.bot ? IM_COL32(80, 220, 255, 230) : IM_COL32(255, 70, 70, 230);
        dl->AddLine(ImVec2(g_w * 0.5f, 8.f), ImVec2(hx, top), col, 1.2f);
        {
            const ImVec2 a(hx - bw * 0.5f, top);
            const ImVec2 b(hx + bw * 0.5f, bot);
            dl->AddRect(a, b, col, 0.f, 0, 1.4f);
        }
        if (e.maxHp > 1.f) {
            const float t = e.hp < 0.f ? 0.f : (e.hp > e.maxHp ? 1.f : e.hp / e.maxHp);
            const float barX = hx - bw * 0.5f - 5.f;
            dl->AddRectFilled(ImVec2(barX - 3.f, top), ImVec2(barX, bot), IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(ImVec2(barX - 3.f, top + bh * (1.f - t)), ImVec2(barX, bot),
                              IM_COL32(40, 220, 80, 230));
        }
        if (name || dist) {
            char label[48];
            if (name && dist)
                std::snprintf(label, sizeof(label), "%s  %.0fm", e.name[0] ? e.name : "enemy", e.dist);
            else if (name)
                std::snprintf(label, sizeof(label), "%s", e.name[0] ? e.name : "enemy");
            else
                std::snprintf(label, sizeof(label), "%.0fm", e.dist);
            dl->AddText(ImVec2(hx - bw * 0.5f, top - 14.f), IM_COL32(255, 255, 255, 230), label);
        }
    }
}

void ui_gl_draw_frame() {
    if (!ImGui::GetCurrentContext() || g_w <= 0 || g_h <= 0) return;
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_w, g_h);
    ImGui::NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    if (io.DeltaTime <= 0.f) io.DeltaTime = 1.f / 60.f;
    draw_world_esp(g_last);
    if (g_last.enemies > 0 || g_last.unity) {
        ImGui::SetNextWindowPos(ImVec2(g_w * 0.5f, 12.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##esp", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("ESP enemies %d", g_last.enemies);
        for (int i = 0; i < g_last.shown; ++i) {
            ImGui::Text("#%d  HP %.0f/%.0f  %s", i + 1, g_last.list[i].hp, g_last.list[i].maxHp,
                        g_last.list[i].alive ? "alive" : "");
        }
        ImGui::End();
    }
    jnmaini_draw_frame(true, g_panelOpen, &g_last);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}