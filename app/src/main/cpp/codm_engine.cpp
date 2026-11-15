#include "codm_engine.hpp"
#include "anogs_patch.hpp"
#include "mem.hpp"
#include "codm_offsets.hpp"
#include "mod_util.hpp"
#include "proc.hpp"
#include "remote_call.hpp"
#include "ui_log.hpp"
#include "jnmaini_ui/weapon_category_config.h"

#include <atomic>
#include <cmath>
#include <cstring>

static std::atomic<pid_t> g_pid{0};
static uintptr_t g_unity = 0;
static uintptr_t g_anogs = 0;
static bool g_anogsPatched = false;
static int g_anogsBypassN = 0;

static uintptr_t g_match = 0;
static uintptr_t g_matchSlot = 0;
static bool g_slotLocked = false;

static bool arm64_adrp(uint32_t insn, uintptr_t pc, uintptr_t &page) {
    if ((insn & 0x9F000000u) != 0x90000000u) return false;
    int64_t imm = ((int64_t)((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3);
    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1);
    page = (pc & ~uintptr_t(0xFFF)) + (uintptr_t)(imm << 12);
    return true;
}

static bool pawn_list_ok(pid_t pid, uintptr_t list, int &size) {
    size = -1;
    if (!mem_ptr_ok(list)) return false;
    if (!mem_read(pid, list + 0x18, size) || size < 0 || size > 64) return false;
    if (size == 0) return true;
    uintptr_t items = 0;
    uintptr_t pawn = 0;
    uint8_t alive = 0;
    if (!mem_read(pid, list + 0x10, items) || !mem_ptr_ok(items)) return false;
    if (!mem_read(pid, items + 0x20, pawn) || !mem_ptr_ok(pawn)) return false;
    if (!mem_read(pid, pawn + codm_off::kPawn_m_IsAlive, alive) || alive > 1) return false;
    return true;
}

static bool looks_like_match(pid_t pid, uintptr_t obj, int *outSize = nullptr) {
    if (outSize) *outSize = -1;
    if (!mem_ptr_ok(obj)) return false;
    uintptr_t klass = 0;
    if (!mem_read(pid, obj, klass) || !mem_ptr_ok(klass)) return false;
    uintptr_t list = 0;
    int size = -1;
    if (!mem_read(pid, obj + codm_off::kEnemyPawns, list)) return false;
    if (!pawn_list_ok(pid, list, size)) return false;
    if (outSize) *outSize = size;
    return true;
}

/** Address of the IL2CPP static slot get_MatchGame reads (s_MatchGame at static_fields+0x8). */
static bool resolve_match_slot(pid_t pid, uintptr_t func, uintptr_t &slot) {
    uint32_t ins[24] = {};
    if (!mem_read(pid, func, ins, sizeof(ins))) return false;
    uintptr_t reg[32] = {};
    bool known[32] = {};
    uintptr_t best = 0;
    int bestScore = 0;
    auto consider = [&](uintptr_t src, int bonus) {
        uintptr_t val = 0;
        if (!mem_read(pid, src, val)) return;
        int score = bonus;
        int sz = -1;
        if (looks_like_match(pid, val, &sz)) score += (sz > 0 ? 200 : 20);
        else if (val == 0) score += 1;
        else return;
        if (score > bestScore) {
            bestScore = score;
            best = src;
        }
    };
    for (int i = 0; i < 24; ++i) {
        const uint32_t insn = ins[i];
        const int rd = insn & 31;
        const int rn = (insn >> 5) & 31;
        uintptr_t page = 0;
        if (arm64_adrp(insn, func + (uintptr_t)i * 4, page)) {
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
        if ((insn & 0xFFE0FFE0u) == 0xAA0003E0u) {
            const int rm = (insn >> 16) & 31;
            if (known[rm]) {
                reg[rd] = reg[rm];
                known[rd] = true;
            }
            continue;
        }
        const bool isLdr = (insn & 0xFFC00000u) == 0xF9400000u;
        const bool isLdur = (insn & 0xFFC00000u) == 0xF8400000u;
        if ((!isLdr && !isLdur) || !known[rn]) continue;
        uintptr_t src = 0;
        if (isLdr) src = reg[rn] + (((insn >> 10) & 0xFFF) * 8u);
        else {
            int32_t imm9 = (insn >> 12) & 0x1FF;
            if (imm9 & 0x100) imm9 |= ~0x1FF;
            src = reg[rn] + (uintptr_t)imm9;
        }
        uintptr_t val = 0;
        if (!mem_read(pid, src, val) || !mem_ptr_ok(val)) {
            if (rd == 0) consider(src, 2);
            continue;
        }
        reg[rd] = val;
        known[rd] = true;
        if (rd == 0) consider(src, 5);
        for (uintptr_t sfOff : {codm_off::kStaticFieldsA, codm_off::kStaticFieldsB}) {
            uintptr_t sf = 0;
            if (mem_read(pid, val + sfOff, sf) && mem_ptr_ok(sf))
                consider(sf + codm_off::kMatchGameField, 40);
        }
    }
    if (!best || bestScore < 40) return false;
    slot = best;
    return true;
}

static bool read_vec3(pid_t pid, uintptr_t addr, float &x, float &y, float &z) {
    float v[3] = {};
    if (!mem_read(pid, addr, v, sizeof(v))) return false;
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) return false;
    if (std::fabs(v[0]) > 200000.f || std::fabs(v[1]) > 200000.f || std::fabs(v[2]) > 200000.f) return false;
    x = v[0];
    y = v[1];
    z = v[2];
    return true;
}

static void read_nick(pid_t pid, uintptr_t pawn, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    uintptr_t info = 0;
    uintptr_t str = 0;
    if (!mem_read(pid, pawn + codm_off::kPawn_m_PlayerInfo, info) || !mem_ptr_ok(info)) return;
    if (!mem_read(pid, info + codm_off::kPlayerInfo_Nick, str) || !mem_ptr_ok(str)) return;
    int32_t len = 0;
    if (!mem_read(pid, str + 0x10, len) || len <= 0 || len > 20) return;
    uint16_t chars[20] = {};
    if (!mem_read(pid, str + 0x14, chars, (size_t)len * 2)) return;
    size_t n = 0;
    for (int i = 0; i < len && n + 1 < cap; ++i) {
        const uint16_t c = chars[i];
        out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[n] = 0;
}

static void basis_from_quat(float x, float y, float z, float w, float &fx, float &fy, float &fz, float &rx,
                            float &ry, float &rz, float &ux, float &uy, float &uz) {
    fx = 2.f * (x * z + w * y);
    fy = 2.f * (y * z - w * x);
    fz = 1.f - 2.f * (x * x + y * y);
    rx = 1.f - 2.f * (y * y + z * z);
    ry = 2.f * (x * y + w * z);
    rz = 2.f * (x * z - w * y);
    ux = 2.f * (x * y - w * z);
    uy = 1.f - 2.f * (x * x + z * z);
    uz = 2.f * (y * z + w * x);
}

struct FeatPatch {
    const char *name;
    uintptr_t rva;
    uint8_t bytes[8];
    bool slider;
    const char *key;
    float sliderMin;
    uint8_t orig[8];
    bool haveOrig;
    bool applied;
};

static FeatPatch g_feats[] = {
    {"uav", 0x6AE9A70, {0x20, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6}, false, "ADVANCE::UAV", 0.f, {}, false, false},
    {"shake", 0x669780C, {0xC0, 0x03, 0x5F, 0xD6, 0xC0, 0x03, 0x5F, 0xD6}, false, "NO::SHAKE", 0.f, {}, false, false},
    {"recoil", 0xD535A6C, {0xE0, 0x03, 0x27, 0x1E, 0xC0, 0x03, 0x5F, 0xD6}, true, "NO::RECOIL", 1.f, {}, false, false},
    {"spread", 0xD53408C, {0xE0, 0x03, 0x27, 0x1E, 0xC0, 0x03, 0x5F, 0xD6}, true, "NO::SPREAD", 1.f, {}, false, false},
    {"reload", 0xC2B5B58, {0xE0, 0x03, 0x27, 0x1E, 0xC0, 0x03, 0x5F, 0xD6}, true, "NO::RELOAD", 1.f, {}, false, false},
    {"reload2", 0xC2B5D88, {0xE0, 0x03, 0x27, 0x1E, 0xC0, 0x03, 0x5F, 0xD6}, true, "NO::RELOAD", 1.f, {}, false, false},
    {"reload3", 0xC2B5F90, {0xE0, 0x03, 0x27, 0x1E, 0xC0, 0x03, 0x5F, 0xD6}, true, "NO::RELOAD", 1.f, {}, false, false},
    {"chute", 0x5A88C48, {0xC0, 0x03, 0x5F, 0xD6, 0xC0, 0x03, 0x5F, 0xD6}, false, "NO::PARACHUTE", 0.f, {}, false, false},
    {"crouch", 0x66B882C, {0xC0, 0x03, 0x5F, 0xD6, 0xC0, 0x03, 0x5F, 0xD6}, false, "NO CROUCH", 0.f, {}, false, false},
};

static bool feat_wanted(const FeatPatch &f) {
    if (f.slider) {
        const auto it = seekbarValues.raw().find(f.key);
        return it != seekbarValues.raw().end() && it->second >= f.sliderMin;
    }
    return checkboxStates[f.key];
}

static void sync_feature_patches(pid_t pid, uintptr_t unity) {
    if (!pid || !unity) return;
    for (auto &f : g_feats) {
        const bool want = feat_wanted(f);
        const uintptr_t addr = unity + f.rva;
        if (want == f.applied) continue;
        if (want) {
            if (!f.haveOrig) {
                if (!mem_read(pid, addr, f.orig, sizeof(f.orig))) continue;
                f.haveOrig = true;
            }
            if (mem_write(pid, addr, f.bytes, sizeof(f.bytes))) {
                f.applied = true;
                ui_log("[feat] %s ON @ unity+0x%llx", f.name, (unsigned long long)f.rva);
            }
        } else if (f.haveOrig && mem_write(pid, addr, f.orig, sizeof(f.orig))) {
            f.applied = false;
            ui_log("[feat] %s OFF", f.name);
        }
    }
}

void codm_reset() {
    g_pid.store(0);
    g_unity = g_anogs = 0;
    g_match = 0;
    g_matchSlot = 0;
    g_slotLocked = false;
    for (auto &f : g_feats) {
        f.haveOrig = false;
        f.applied = false;
    }
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
    sync_feature_patches(pid, g_unity);
    if (!g_matchSlot || !g_slotLocked) {
        uintptr_t slot = 0;
        if (resolve_match_slot(pid, g_unity + codm_off::kGameplay_get_MatchGame, slot)) {
            if (slot != g_matchSlot) {
                g_matchSlot = slot;
                ui_log("[esp] s_MatchGame slot=0x%llx", (unsigned long long)g_matchSlot);
            }
        } else if (!g_matchSlot) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                uint32_t head[6] = {};
                mem_read(pid, g_unity + codm_off::kGameplay_get_MatchGame, head, sizeof(head));
                ui_log("[esp] s_MatchGame slot not found insn %08x %08x %08x %08x %08x %08x", head[0],
                       head[1], head[2], head[3], head[4], head[5]);
            }
            std::snprintf(out.status, sizeof(out.status), "libs OK — s_MatchGame not found");
            return;
        }
    }
    uintptr_t match = 0;
    mem_read(pid, g_matchSlot, match);
    if (!looks_like_match(pid, match)) {
        g_match = 0;
        std::snprintf(out.status, sizeof(out.status), "no match (lobby or slot empty)");
        return;
    }
    if (g_match != match) {
        g_match = match;
        ui_log("[esp] MatchGame=0x%llx", (unsigned long long)match);
    }
    out.match = match;
    uintptr_t listPtr = 0;
    int size = 0;
    const char *listName = "enemy";
    mem_read(pid, match + codm_off::kEnemyPawns, listPtr);
    pawn_list_ok(pid, listPtr, size);
    if (size <= 0) {
        uintptr_t all = 0;
        int allSize = 0;
        if (mem_read(pid, match + codm_off::kAllPawns, all) && pawn_list_ok(pid, all, allSize) && allSize > 0) {
            listPtr = all;
            size = allSize;
            listName = "all";
        }
    }
    out.enemies = size < 0 ? 0 : size;
    if (out.enemies > 0) g_slotLocked = true;
    uintptr_t items = 0;
    mem_read(pid, listPtr + 0x10, items);
    uintptr_t local = 0;
    float localX = 0, localY = 0, localZ = 0;
    const int n = size < 24 ? size : 24;
    for (int i = 0; i < n; ++i) {
        uintptr_t pawn = 0;
        if (!items || !mem_read(pid, items + 0x20 + (uintptr_t)i * 8, pawn) || !mem_ptr_ok(pawn)) continue;
        uint8_t controlled = 0;
        mem_read(pid, pawn + codm_off::kPawn_m_IsControlled, controlled);
        if (controlled == 1 && !local) local = pawn;
    }
    if (!local) {
        uintptr_t all = 0;
        int allSize = 0;
        if (mem_read(pid, match + codm_off::kAllPawns, all) && pawn_list_ok(pid, all, allSize)) {
            uintptr_t allItems = 0;
            mem_read(pid, all + 0x10, allItems);
            const int an = allSize < 64 ? allSize : 64;
            for (int i = 0; i < an && !local; ++i) {
                uintptr_t pawn = 0;
                uint8_t controlled = 0;
                if (!allItems || !mem_read(pid, allItems + 0x20 + (uintptr_t)i * 8, pawn) || !mem_ptr_ok(pawn))
                    continue;
                mem_read(pid, pawn + codm_off::kPawn_m_IsControlled, controlled);
                if (controlled == 1) local = pawn;
            }
        }
    }
    if (local && read_vec3(pid, local + codm_off::kPawn_m_Location, localX, localY, localZ)) {
        float eye = 0.f;
        mem_read(pid, local + codm_off::kPawn_m_EyeHeight, eye);
        if (!(eye > 0.4f && eye < 3.f)) eye = 1.6f;
        float q[4] = {};
        if (mem_read(pid, local + codm_off::kPawn_m_AimRotation, q, sizeof(q))) {
            const float len2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
            if (len2 > 0.5f && len2 < 1.5f) {
                out.camX = localX;
                out.camY = localY + eye;
                out.camZ = localZ;
                basis_from_quat(q[0], q[1], q[2], q[3], out.fx, out.fy, out.fz, out.rx, out.ry, out.rz, out.ux,
                                out.uy, out.uz);
                out.viewOk = 1;
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        uintptr_t pawn = 0;
        if (!items || !mem_read(pid, items + 0x20 + (uintptr_t)i * 8, pawn) || !mem_ptr_ok(pawn)) continue;
        if (pawn == local) continue;
        uint8_t alive = 0, bot = 0;
        mem_read(pid, pawn + codm_off::kPawn_m_IsAlive, alive);
        mem_read(pid, pawn + codm_off::kPawn_m_IsBot, bot);
        float x = 0, y = 0, z = 0;
        if (!read_vec3(pid, pawn + codm_off::kPawn_m_Location, x, y, z))
            read_vec3(pid, pawn + codm_off::kPawn_m_LastPawnPos, x, y, z);
        float eye = 0.f;
        mem_read(pid, pawn + codm_off::kPawn_m_EyeHeight, eye);
        if (!(eye > 0.4f && eye < 3.f)) eye = 1.6f;
        uintptr_t info = 0;
        float hp = 0.f, maxHp = 0.f;
        if (mem_read(pid, pawn + codm_off::kAttackableInfo, info) && mem_ptr_ok(info)) {
            mem_read(pid, info + codm_off::kHealth, hp);
            mem_read(pid, info + codm_off::kMaxHealth, maxHp);
        }
        CodmEnemy &e = out.list[out.shown];
        e.hp = hp;
        e.maxHp = maxHp;
        e.x = x;
        e.y = y;
        e.z = z;
        e.head = y + eye;
        e.alive = alive;
        e.bot = bot;
        if (local) {
            const float dx = x - localX, dy = y - localY, dz = z - localZ;
            e.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        read_nick(pid, pawn, e.name, sizeof(e.name));
        ++out.shown;
        if (out.shown >= 24) break;
    }
    static int loggedN = -1;
    if (loggedN != size) {
        loggedN = size;
        ui_log("[esp] %s=%d shown=%d local=%s view=%d", listName, size, out.shown, local ? "yes" : "no",
               out.viewOk);
    }
    std::snprintf(out.status, sizeof(out.status), "%s=%d shown=%d view=%s anogs=%s", listName, size, out.shown,
                  out.viewOk ? "ok" : "no-aim", g_anogsPatched ? "patched" : "raw");
}

bool codm_anogs_hooks_patched() { return g_anogsPatched; }

int codm_anogs_last_bypass_count() { return g_anogsBypassN; }