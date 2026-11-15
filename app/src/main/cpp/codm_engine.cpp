#include "codm_engine.hpp"
#include "anogs_patch.hpp"
#include "codm_offsets.hpp"
#include "mod_util.hpp"
#include "proc.hpp"
#include "remote_call.hpp"
#include "ui_log.hpp"

#include <atomic>
#include <cstring>

static std::atomic<pid_t> g_pid{0};
static uintptr_t g_unity = 0;
static uintptr_t g_anogs = 0;
static bool g_anogsPatched = false;

void codm_reset() {
    g_pid.store(0);
    g_unity = g_anogs = 0;
    g_anogsPatched = false;
}

void codm_attach(pid_t pid) {
    g_pid.store(pid);
    char path[256] = {};
    g_unity = mod_base(pid, "libunity.so", path, sizeof(path));
    g_anogs = mod_base(pid, "libanogs.so", path, sizeof(path));
    ui_log("[codm] pid=%d unity=0x%llx anogs=0x%llx", (int)pid, (unsigned long long)g_unity,
           (unsigned long long)g_anogs);
}

bool codm_auto_patch_anogs() {
    const pid_t pid = g_pid.load();
    if (!pid) {
        ui_log("[anogs] auto-patch: no CODM pid");
        return false;
    }
    if (!g_anogs) {
        ui_log("[anogs] auto-patch: libanogs.so not mapped");
        return false;
    }
    char msg[120] = {};
    const int n = anogs_apply_bypass(pid, g_anogs, msg, sizeof(msg));
    if (n > 0) {
        g_anogsPatched = true;
        ui_log("[anogs] auto-patch SUCCESS %d slot(s) from hooks.h (%s)", n, msg);
        return true;
    }
    ui_log("[anogs] auto-patch FAILED (hooks.h BYPASS)");
    return false;
}

void codm_tick(CodmFrame &out) {
    out = {};
    const pid_t pid = g_pid.load();
    out.pid = pid;
    out.unity = g_unity;
    out.anogs = g_anogs;
    if (!pid || !g_unity) {
        std::snprintf(out.status, sizeof(out.status), "waiting com.garena.game.codm");
        return;
    }
    uintptr_t match = 0;
    uintptr_t ret = 0;
    if (remote_call(pid, g_unity + codm_off::kGameplay_get_MatchGame, {}, ret, out.status,
                    sizeof(out.status))) {
        match = ret;
    }
    if (!match) {
        std::snprintf(out.status, sizeof(out.status), "lobby / no MatchGame");
        return;
    }
    uintptr_t listPtr = 0;
    if (!mem_read(pid, match + codm_off::kEnemyPawns, listPtr) || !mem_ptr_ok(listPtr)) {
        std::snprintf(out.status, sizeof(out.status), "no enemy list");
        return;
    }
    int size = 0;
    mem_read(pid, listPtr + 0x18, size);
    out.enemies = size;
    std::snprintf(out.status, sizeof(out.status), "match OK enemies=%d anogs=%s", size,
                  g_anogsPatched ? "patched" : "raw");
}