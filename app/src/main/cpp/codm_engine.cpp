#include "codm_engine.hpp"
#include "anogs_patch.hpp"
#include "mem.hpp"
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
static int g_anogsBypassN = 0;

static uintptr_t g_match = 0;

static bool arm64_adrp(uint32_t insn, uintptr_t pc, uintptr_t &page) {
    if ((insn & 0x9F000000u) != 0x90000000u) return false;
    int64_t imm = ((int64_t)((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3);
    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1);
    page = (pc & ~uintptr_t(0xFFF)) + (uintptr_t)(imm << 12);
    return true;
}

/** Follow the ADRP/ADD/LDR chain in an IL2CPP static getter. Reads only — does not run the function. */
static uintptr_t read_static_getter(pid_t pid, uintptr_t func) {
    uint32_t ins[16] = {};
    if (!mem_read(pid, func, ins, sizeof(ins))) return 0;
    uintptr_t reg[32] = {};
    bool known[32] = {};
    uintptr_t result = 0;
    for (int i = 0; i < 16; ++i) {
        const uint32_t insn = ins[i];
        const uintptr_t pc = func + (uintptr_t)i * 4;
        const int rd = insn & 31;
        const int rn = (insn >> 5) & 31;
        if (insn == 0xD65F03C0u) break;
        uintptr_t page = 0;
        if (arm64_adrp(insn, pc, page)) {
            reg[rd] = page;
            known[rd] = true;
            continue;
        }
        if ((insn & 0xFF800000u) == 0x91000000u && known[rn]) {
            uint32_t imm = (insn >> 10) & 0xFFF;
            if (((insn >> 22) & 3) == 1) imm <<= 12;
            reg[rd] = reg[rn] + imm;
            known[rd] = true;
            continue;
        }
        if ((insn & 0xFFC00000u) == 0xF9400000u && known[rn]) {
            const uintptr_t src = reg[rn] + (((insn >> 10) & 0xFFF) * 8);
            uintptr_t val = 0;
            if (mem_read(pid, src, val) && mem_ptr_ok(val)) {
                reg[rd] = val;
                known[rd] = true;
                result = val;
            }
        }
    }
    return mem_ptr_ok(result) ? result : 0;
}

void codm_reset() {
    g_pid.store(0);
    g_unity = g_anogs = 0;
    g_match = 0;
    g_anogsPatched = false;
    g_anogsBypassN = 0;
}

void codm_attach(pid_t pid) {
    g_pid.store(pid);
    mem_invalidate_maps_cache(pid);
    char upath[256] = {};
    char apath[256] = {};
    g_unity = mod_base(pid, "libunity.so", upath, sizeof(upath));
    g_anogs = mod_base(pid, "libanogs.so", apath, sizeof(apath));
    std::vector<MapsReg> maps;
    int so = 0;
    int unityLine = 0;
    int anogsLine = 0;
    if (mem_maps(pid, maps)) {
        for (const auto &r : maps) {
            if (r.path.find(".so") != std::string::npos) ++so;
            if (r.path.find("libunity") != std::string::npos) ++unityLine;
            if (r.path.find("libanogs") != std::string::npos) ++anogsLine;
        }
    }
    ui_log("[codm] pid=%d maps=%zu so=%d unity_lines=%d anogs_lines=%d", (int)pid, maps.size(), so,
           unityLine, anogsLine);
    ui_log("[codm] unity=0x%llx %s", (unsigned long long)g_unity, upath[0] ? upath : "(missing)");
    ui_log("[codm] anogs=0x%llx %s", (unsigned long long)g_anogs, apath[0] ? apath : "(missing)");
    if (!g_unity || !g_anogs) {
        int logged = 0;
        for (const auto &r : maps) {
            if (r.path.find(".so") == std::string::npos) continue;
            const auto slash = r.path.find_last_of('/');
            const char *name = slash == std::string::npos ? r.path.c_str() : r.path.c_str() + slash + 1;
            ui_log("[maps] %s %s", r.perms, name);
            if (++logged >= 8) break;
        }
    }
}

bool codm_libs_ready() { return g_pid.load() > 0 && g_unity && g_anogs; }

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
    g_anogsBypassN = n;
    if (n > 0) {
        g_anogsPatched = (n >= 4);
        ui_log("[anogs] auto-patch SUCCESS %d slot(s) from hooks.h (%s)", n, msg);
        return true;
    }
    g_anogsPatched = false;
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
    /* Same pointer the original GamePlay::get_MatchGame() returns, read from its static field. */
    const uintptr_t matchNow = read_static_getter(pid, g_unity + codm_off::kGameplay_get_MatchGame);
    if (mem_ptr_ok(matchNow)) g_match = matchNow;
    if (!g_match) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            uint32_t head[4] = {};
            mem_read(pid, g_unity + codm_off::kGameplay_get_MatchGame, head, sizeof(head));
            ui_log("[esp] MatchGame static decode failed insn %08x %08x %08x %08x", head[0], head[1],
                   head[2], head[3]);
        }
        std::snprintf(out.status, sizeof(out.status), "libs OK — MatchGame not read");
        return;
    }
    static uintptr_t loggedMatch = 0;
    if (loggedMatch != g_match) {
        loggedMatch = g_match;
        ui_log("[esp] MatchGame=0x%llx (static read, no call)", (unsigned long long)g_match);
    }
    uintptr_t match = g_match;
    uintptr_t listPtr = 0;
    if (!mem_read(pid, match + codm_off::kEnemyPawns, listPtr) || !mem_ptr_ok(listPtr)) {
        std::snprintf(out.status, sizeof(out.status), "no enemy list");
        return;
    }
    int size = 0;
    mem_read(pid, listPtr + 0x18, size);
    if (size < 0) size = 0;
    if (size > 64) size = 64;
    out.enemies = size;
    uintptr_t items = 0;
    mem_read(pid, listPtr + 0x10, items);
    const int n = size < 12 ? size : 12;
    for (int i = 0; i < n; ++i) {
        uintptr_t pawn = 0;
        if (!items || !mem_read(pid, items + 0x20 + (uintptr_t)i * 8, pawn) || !mem_ptr_ok(pawn)) continue;
        int alive = 0;
        mem_read(pid, pawn + codm_off::kPawn_m_IsAlive, alive);
        uintptr_t info = 0;
        float hp = 0.f, maxHp = 0.f;
        if (mem_read(pid, pawn + codm_off::kAttackableInfo, info) && mem_ptr_ok(info)) {
            mem_read(pid, info + codm_off::kHealth, hp);
            mem_read(pid, info + codm_off::kMaxHealth, maxHp);
        }
        out.list[out.shown].hp = hp;
        out.list[out.shown].maxHp = maxHp;
        out.list[out.shown].alive = alive;
        ++out.shown;
    }
    std::snprintf(out.status, sizeof(out.status), "match enemies=%d read=%d anogs=%s", size, out.shown,
                  g_anogsPatched ? "patched" : "raw");
}

bool codm_anogs_hooks_patched() { return g_anogsPatched; }

int codm_anogs_last_bypass_count() { return g_anogsBypassN; }