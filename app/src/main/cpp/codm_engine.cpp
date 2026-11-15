#include "codm_engine.hpp"
#include "anogs_patch.hpp"
#include "mem.hpp"
#include "codm_offsets.hpp"
#include "mod_util.hpp"
#include "proc.hpp"
#include "remote_call.hpp"
#include "ui_log.hpp"
#include "jnmaini_ui/weapon_category_config.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

static std::atomic<pid_t> g_pid{0};
static uintptr_t g_unity = 0;
static uintptr_t g_anogs = 0;
static bool g_anogsPatched = false;
static int g_anogsBypassN = 0;

static uintptr_t g_match = 0;
static uintptr_t g_matchSlot = 0;
static bool g_slotLocked = false;
static bool g_slotHot = false;
static bool g_nameScanDone = false;
static uintptr_t g_gameplayKlass = 0;
static uintptr_t g_blob = 0;

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
    if (!mem_read(pid, list + 0x18, size) || size < 1 || size > 64) return false;
    uintptr_t items = 0;
    if (!mem_read(pid, list + 0x10, items) || !mem_ptr_ok(items)) return false;
    const int n = size < 4 ? size : 4;
    for (int i = 0; i < n; ++i) {
        uintptr_t pawn = 0;
        if (mem_read(pid, items + 0x20 + (uintptr_t)i * 8, pawn) && mem_ptr_ok(pawn)) return true;
    }
    return false;
}

static bool __attribute__((unused)) looks_like_match(pid_t pid, uintptr_t obj, int *outSize = nullptr) {
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

static bool cstr_is(pid_t pid, uintptr_t p, const char *s) {
    char buf[24] = {};
    if (!mem_ptr_ok(p) || !mem_read(pid, p, buf, sizeof(buf) - 1)) return false;
    return std::strcmp(buf, s) == 0;
}

static bool named_klass(pid_t pid, uintptr_t obj, const char *s) {
    if (!mem_ptr_ok(obj)) return false;
    for (uintptr_t off = 0; off <= 0x48; off += 8) {
        uintptr_t p = 0;
        if (mem_read(pid, obj + off, p) && cstr_is(pid, p, s)) return true;
    }
    return false;
}

struct WalkRegs {
    uintptr_t reg[32] = {};
    bool known[32] = {};
    bool gameplay[32] = {};
    bool fields[32] = {};
    uintptr_t x0Slot = 0;
};

/** Walk get_MatchGame. TBNZ is taken: that path is the inited getter. Cold path is the class init call. */
static bool __attribute__((unused)) walk_getter(pid_t pid, uintptr_t func, const uint32_t *ins, int n, int i, WalkRegs st, uintptr_t &slot,
                        int depth) {
    if (depth > 6) return false;
    for (; i < n; ++i) {
        const uint32_t insn = ins[i];
        const int rd = insn & 31;
        const int rn = (insn >> 5) & 31;
        if (insn == 0xD65F03C0u) {
            if (!st.x0Slot) return false;
            slot = st.x0Slot;
            return true;
        }
        uintptr_t page = 0;
        if (arm64_adrp(insn, func + (uintptr_t)i * 4, page)) {
            st.reg[rd] = page;
            st.known[rd] = true;
            st.gameplay[rd] = false;
            st.fields[rd] = false;
            continue;
        }
        if ((insn & 0xFF800000u) == 0x91000000u && st.known[rn]) {
            uint32_t imm = (insn >> 10) & 0xFFF;
            if (((insn >> 22) & 3) == 1) imm <<= 12;
            st.reg[rd] = st.reg[rn] + imm;
            st.known[rd] = true;
            st.gameplay[rd] = false;
            st.fields[rd] = false;
            continue;
        }
        if ((insn & 0xFFE0FFE0u) == 0xAA0003E0u) {
            const int rm = (insn >> 16) & 31;
            if (st.known[rm]) {
                st.reg[rd] = st.reg[rm];
                st.known[rd] = true;
                st.gameplay[rd] = st.gameplay[rm];
                st.fields[rd] = st.fields[rm];
            }
            continue;
        }
        const bool isLdr = (insn & 0xFFC00000u) == 0xF9400000u;
        const bool isLdur = (insn & 0xFFC00000u) == 0xF8400000u;
        if ((isLdr || isLdur) && st.known[rn]) {
            uintptr_t src = 0;
            uint32_t byteOff = 0;
            if (isLdr) {
                byteOff = ((insn >> 10) & 0xFFF) * 8u;
                src = st.reg[rn] + byteOff;
            } else {
                int32_t imm9 = (insn >> 12) & 0x1FF;
                if (imm9 & 0x100) imm9 |= ~0x1FF;
                src = st.reg[rn] + (uintptr_t)imm9;
                byteOff = (uint32_t)imm9;
            }
            uintptr_t val = 0;
            const bool ok = mem_read(pid, src, val);
            if (rd == 0) st.x0Slot = src;
            if (st.fields[rn] && byteOff == codm_off::kMatchGameField) {
                slot = src;
                return true;
            }
            if (!ok || !mem_ptr_ok(val)) {
                st.known[rd] = false;
                st.gameplay[rd] = false;
                st.fields[rd] = false;
                continue;
            }
            st.reg[rd] = val;
            st.known[rd] = true;
            st.gameplay[rd] = named_klass(pid, val, "GamePlay");
            st.fields[rd] = st.gameplay[rn];
            continue;
        }
        if ((insn & 0xFF000000u) == 0x58000000u) {
            int32_t imm19 = (insn >> 5) & 0x7FFFF;
            if (imm19 & 0x40000) imm19 |= ~0x7FFFF;
            const uintptr_t src = (func + (uintptr_t)i * 4) + (uintptr_t)(imm19 * 4);
            uintptr_t val = 0;
            if (rd == 0) st.x0Slot = src;
            if (mem_read(pid, src, val) && mem_ptr_ok(val)) {
                st.reg[rd] = val;
                st.known[rd] = true;
                st.gameplay[rd] = named_klass(pid, val, "GamePlay");
                st.fields[rd] = false;
            }
            continue;
        }
        const bool tb = (insn & 0x7F000000u) == 0x36000000u || (insn & 0x7F000000u) == 0x37000000u ||
                        (insn & 0x7F000000u) == 0xB6000000u || (insn & 0x7F000000u) == 0xB7000000u;
        const bool cb = (insn & 0x7F000000u) == 0x34000000u || (insn & 0x7F000000u) == 0x35000000u ||
                        (insn & 0x7F000000u) == 0xB4000000u || (insn & 0x7F000000u) == 0xB5000000u;
        if (tb || cb) {
            int32_t imm = tb ? (int32_t)((insn >> 5) & 0x3FFF) : (int32_t)((insn >> 5) & 0x7FFFF);
            const int signbit = tb ? 13 : 18;
            if (imm & (1 << signbit)) imm |= ~((1 << (signbit + 1)) - 1);
            const int target = i + (imm * 4) / 4;
            if (target > i && target < n && walk_getter(pid, func, ins, n, target, st, slot, depth + 1)) return true;
            continue;
        }
        if ((insn & 0xFC000000u) == 0x14000000u) {
            int32_t imm = insn & 0x03FFFFFF;
            if (imm & 0x02000000) imm |= ~0x03FFFFFF;
            const int target = i + imm;
            if (target > i && target < n) {
                i = target - 1;
                continue;
            }
            return false;
        }
        if ((insn & 0xFC000000u) == 0x94000000u || (insn & 0xFFFFFC1Fu) == 0xD63F0000u) {
            for (int r = 0; r <= 18; ++r) {
                st.known[r] = false;
                st.gameplay[r] = false;
                st.fields[r] = false;
            }
        }
    }
    if (!st.x0Slot) return false;
    slot = st.x0Slot;
    return true;
}

static bool slot_from_gameplay(pid_t pid, uintptr_t klass, uintptr_t &slot) {
    if (!named_klass(pid, klass, "GamePlay")) return false;
    const uintptr_t offs[] = {0xB8, 0xA0, 0xC8, 0x80, 0xF8};
    for (uintptr_t off : offs) {
        uintptr_t sf = 0;
        if (!mem_read(pid, klass + off, sf) || !mem_ptr_ok(sf)) continue;
        slot = sf + codm_off::kMatchGameField;
        ui_log("[esp] GamePlay klass=0x%llx static+0x%llx slot=0x%llx", (unsigned long long)klass,
               (unsigned long long)off, (unsigned long long)slot);
        return true;
    }
    return false;
}

static bool klass_from_getter_globals(pid_t pid, uintptr_t func, uintptr_t &klass) {
    uint32_t ins[32] = {};
    if (!mem_read(pid, func, ins, sizeof(ins))) return false;
    uintptr_t reg[32] = {};
    bool known[32] = {};
    for (int i = 0; i < 32; ++i) {
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
        if ((insn & 0xFFC00000u) != 0xF9400000u || !known[rn]) continue;
        const uintptr_t src = reg[rn] + (((insn >> 10) & 0xFFF) * 8u);
        uintptr_t val = 0;
        if (!mem_read(pid, src, val) || !mem_ptr_ok(val)) {
            known[rd] = false;
            continue;
        }
        if (named_klass(pid, val, "GamePlay")) {
            klass = val;
            return true;
        }
        uintptr_t inner = 0;
        if (mem_read(pid, val, inner) && named_klass(pid, inner, "GamePlay")) {
            klass = inner;
            return true;
        }
        reg[rd] = val;
        known[rd] = true;
    }
    return false;
}

static bool klass_from_name_scan(pid_t pid, uintptr_t &klass) {
    std::vector<MapsReg> maps;
    if (!mem_get_maps(pid, maps)) return false;
    const char sig[] = {'G', 'a', 'm', 'e', 'P', 'l', 'a', 'y', 0};
    std::vector<uintptr_t> strings;
    std::vector<uint8_t> buf(512 * 1024);
    for (const auto &r : maps) {
        if (r.perms[0] != 'r') continue;
        const bool meta = r.path.find("metadata") != std::string::npos || r.path.find("libil2cpp") != std::string::npos;
        if (!meta) continue;
        const uintptr_t limit = r.start + 48ull * 1024ull * 1024ull;
        const uintptr_t end = r.end < limit ? r.end : limit;
        for (uintptr_t a = r.start; a + sizeof(sig) < end && strings.size() < 8;) {
            const size_t n = std::min(buf.size(), (size_t)(end - a));
            if (!mem_read(pid, a, buf.data(), n)) {
                a += n ? n : 4096;
                if (a >= end) break;
                continue;
            }
            for (size_t i = 0; i + sizeof(sig) <= n && strings.size() < 8; ++i) {
                if (std::memcmp(buf.data() + i, sig, sizeof(sig)) != 0) continue;
                if (i > 0 && buf[i - 1] != 0) continue;
                strings.push_back(a + i);
            }
            a += n > sizeof(sig) ? n - sizeof(sig) : n;
            if (a >= end) break;
        }
    }
    if (strings.empty()) return false;
    ui_log("[esp] GamePlay string hits=%zu first=0x%llx", strings.size(), (unsigned long long)strings[0]);
    for (uintptr_t str : strings) {
        uint8_t needle[8];
        std::memcpy(needle, &str, 8);
        for (const auto &r : maps) {
            if (r.perms[0] != 'r') continue;
            if (r.path.find("libunity.so") == std::string::npos && r.path.find("libil2cpp") == std::string::npos)
                continue;
            for (uintptr_t a = r.start; a + 8 < r.end;) {
                const size_t n = std::min(buf.size(), (size_t)(r.end - a));
                if (!mem_read(pid, a, buf.data(), n)) {
                    a += n ? n : 4096;
                    continue;
                }
                for (size_t i = 0; i + 8 <= n; i += 8) {
                    if (std::memcmp(buf.data() + i, needle, 8) != 0) continue;
                    const uintptr_t site = a + i;
                    for (uintptr_t off = 0x10; off <= 0x48; off += 8) {
                        if (site < off) continue;
                        const uintptr_t cand = site - off;
                        if (!named_klass(pid, cand, "GamePlay")) continue;
                        g_gameplayKlass = cand;
                        if (slot_from_gameplay(pid, cand, klass)) return true;
                    }
                }
                a += n > 8 ? n - 8 : n;
            }
        }
    }
    return false;
}

static bool resolve_match_slot(pid_t pid, uintptr_t func, uintptr_t &slot) {
    if (g_gameplayKlass && slot_from_gameplay(pid, g_gameplayKlass, slot)) return true;
    uint32_t ins[32] = {};
    if (!mem_read(pid, func, ins, sizeof(ins))) return false;
    int start = 0;
    for (int i = 0; i < 32; ++i) {
        const uint32_t insn = ins[i];
        const bool tb = (insn & 0x7F000000u) == 0x36000000u || (insn & 0x7F000000u) == 0x37000000u;
        if (!tb) continue;
        int32_t imm = (int32_t)((insn >> 5) & 0x3FFF);
        if (imm & (1 << 13)) imm |= ~((1 << 14) - 1);
        const int target = i + imm;
        if (target > i && target < 32) start = target;
        break;
    }
    uintptr_t reg[32] = {};
    bool known[32] = {};
    uintptr_t x0Slot = 0;
    uintptr_t staticBlob = 0;
    bool loggedPtr = false;
    for (int i = start; i < 32; ++i) {
        const uint32_t insn = ins[i];
        const int rd = insn & 31;
        const int rn = (insn >> 5) & 31;
        if (insn == 0xD65F03C0u) break;
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
        if ((insn & 0xFFC00000u) == 0xF9400000u && known[rn]) {
            const uint32_t byteOff = ((insn >> 10) & 0xFFF) * 8u;
            const uintptr_t src = reg[rn] + byteOff;
            uintptr_t val = 0;
            if (rd == 0) {
                if (byteOff == codm_off::kMatchGameField) x0Slot = src;
                else if (byteOff == 0 && staticBlob) x0Slot = staticBlob + codm_off::kMatchGameField;
                else x0Slot = src + codm_off::kMatchGameField;
            }
            if (!mem_read(pid, src, val) || !mem_ptr_ok(val)) {
                known[rd] = false;
                continue;
            }
            if (rd != 0 && !named_klass(pid, val, "GamePlay")) {
                staticBlob = val;
                g_blob = val;
            }
            if (!loggedPtr) {
                loggedPtr = true;
                char txt[24] = {};
                uintptr_t np = 0;
                mem_read(pid, val + 0x10, np);
                if (mem_ptr_ok(np)) mem_read(pid, np, txt, sizeof(txt) - 1);
                ui_log("[esp] hot ptr=0x%llx +0x10=\"%.16s\"", (unsigned long long)val, txt);
            }
            if (named_klass(pid, val, "GamePlay")) {
                g_gameplayKlass = val;
                if (slot_from_gameplay(pid, val, slot)) return true;
            }
            reg[rd] = val;
            known[rd] = true;
            continue;
        }
        if ((insn & 0xFC000000u) == 0x14000000u) {
            int32_t imm = insn & 0x03FFFFFF;
            if (imm & 0x02000000) imm |= ~0x03FFFFFF;
            const int target = i + imm;
            if (target > i && target < 32) {
                i = target - 1;
                continue;
            }
            break;
        }
    }
    uintptr_t klass = 0;
    if (klass_from_getter_globals(pid, func, klass)) {
        g_gameplayKlass = klass;
        if (slot_from_gameplay(pid, klass, slot)) return true;
    }
    if (g_blob && slot_from_blob(pid, g_blob, slot)) {
        g_slotHot = false;
        return true;
    }
    if (x0Slot) {
        slot = x0Slot;
        g_slotHot = true;
        uintptr_t now = 0;
        mem_read(pid, x0Slot, now);
        ui_log("[esp] hot s_MatchGame slot=0x%llx now=0x%llx", (unsigned long long)x0Slot, (unsigned long long)now);
        return true;
    }
    if (!g_nameScanDone) {
        g_nameScanDone = true;
        ui_log("[esp] scanning metadata for GamePlay");
        if (klass_from_name_scan(pid, slot)) return true;
    }
    return false;
}

static bool read_il2cpp_name(pid_t pid, uintptr_t klass, char *buf, size_t cap) {
    if (!buf || cap < 2) return false;
    buf[0] = 0;
    for (uintptr_t off = 0x10; off <= 0x18; off += 8) {
        uintptr_t p = 0;
        if (!mem_read(pid, klass + off, p) || !mem_ptr_ok(p)) continue;
        char tmp[32] = {};
        if (!mem_read(pid, p, tmp, sizeof(tmp) - 1)) continue;
        bool ok = tmp[0] >= 'A' && tmp[0] <= 'Z';
        size_t n = 0;
        for (; ok && tmp[n] && n < sizeof(tmp) - 1; ++n) {
            const unsigned char c = (unsigned char)tmp[n];
            ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }
        if (!ok || n < 4 || n >= cap) continue;
        std::memcpy(buf, tmp, n + 1);
        return true;
    }
    return false;
}

static bool instance_is_match(pid_t pid, uintptr_t obj, char *out, size_t cap) {
    uintptr_t klass = 0;
    if (!mem_read(pid, obj, klass)) return false;
    for (int depth = 0; depth < 8 && mem_ptr_ok(klass); ++depth) {
        char name[32] = {};
        if (read_il2cpp_name(pid, klass, name, sizeof(name))) {
            const bool match = std::strcmp(name, "MatchGame") == 0 || std::strcmp(name, "PVPGame") == 0 ||
                               std::strcmp(name, "PVEGame") == 0 || std::strcmp(name, "OpenWorldGame") == 0 ||
                               std::strcmp(name, "BarGame") == 0 || std::strcmp(name, "MPVEGame") == 0 ||
                               std::strcmp(name, "BaseRecreationGame") == 0 || std::strcmp(name, "WinnerCircleGame") == 0;
            if (match) {
                if (out && cap) std::snprintf(out, cap, "%s", name);
                return true;
            }
        }
        uintptr_t parent = 0;
        if (!mem_read(pid, klass + 0x48, parent)) break;
        klass = parent;
    }
    return false;
}

static bool slot_from_blob(pid_t pid, uintptr_t blob, uintptr_t &slot);

static bool find_pawn_list(pid_t pid, uintptr_t obj, uintptr_t &list, int &size) {
    const uintptr_t prefer[] = {0x178, 0x160, 0x170, 0x168, 0x180, 0x188, 0x198, 0x1A0};
    for (uintptr_t off : prefer) {
        uintptr_t p = 0;
        int sz = 0;
        if (mem_read(pid, obj + off, p) && pawn_list_ok(pid, p, sz) && sz > 0) {
            list = p;
            size = sz;
            return true;
        }
    }
    for (uintptr_t off = 0x100; off <= 0x240; off += 8) {
        uintptr_t p = 0;
        int sz = 0;
        if (mem_read(pid, obj + off, p) && pawn_list_ok(pid, p, sz) && sz > 0) {
            list = p;
            size = sz;
            ui_log("[esp] pawn list at match+0x%llx size=%d", (unsigned long long)off, sz);
            return true;
        }
    }
    return false;
}

static bool slot_from_blob(pid_t pid, uintptr_t blob, uintptr_t &slot) {
    const uintptr_t offs[] = {0xB8, 0xA0, 0xC8, 0x80, 0xF8, 0xD8, 0xE0, 0x100, 0x0};
    for (uintptr_t off : offs) {
        uintptr_t base = blob;
        if (off != 0 && (!mem_read(pid, blob + off, base) || !mem_ptr_ok(base))) continue;
        for (uintptr_t field : {codm_off::kMatchGameField, uintptr_t{0}}) {
            uintptr_t obj = 0;
            if (!mem_read(pid, base + field, obj) || !mem_ptr_ok(obj)) continue;
            char name[32] = {};
            uintptr_t list = 0;
            int sz = 0;
            const bool named = instance_is_match(pid, obj, name, sizeof(name));
            const bool pawns = find_pawn_list(pid, obj, list, sz);
            if (!named && !pawns) continue;
            slot = base + field;
            static uintptr_t logged = 0;
            if (logged != slot) {
                logged = slot;
                ui_log("[esp] %s blob+0x%llx field+0x%llx pawns=%d", named ? name : "list", (unsigned long long)off,
                       (unsigned long long)field, sz);
            }
            return true;
        }
    }
    return false;
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

enum HookMode { M_TRUE = 1, M_RET, M_SCALE, M_VALUE, M_TIME, M_CONST, M_FPS, M_VEC0, M_SPEED };

struct UnityHook {
    const char *name;
    const char *key;
    HookMode mode;
    uintptr_t rva;
    float constF;
    uint8_t orig[16];
    bool haveOrig;
    bool applied;
    uint32_t stamp;
};

static UnityHook g_unityHooksTable[] = {
        {"uav", "ADVANCE::UAV", M_TRUE, 0x6AE9A70, 0, {}, false, false, 0},
        {"no_shake", "NO::SHAKE", M_CONST, 0x669780C, 0.f, {}, false, false, 0},
        {"no_parachute", "NO::PARACHUTE", M_RET, 0x5A88C48, 0, {}, false, false, 0},
        {"no_crouch", "NO CROUCH", M_RET, 0x66B882C, 0, {}, false, false, 0},
        {"fast_swim", "FAST::SWIM", M_CONST, 0x5597B14, 3.f, {}, false, false, 0},
        {"hitbox", "HITBOX", M_TRUE, 0xC2BE0C8, 0, {}, false, false, 0},
        {"no_recoil", "NO::RECOIL", M_SCALE, 0xD535A6C, 0, {}, false, false, 0},
        {"no_punch", "NO::RECOIL", M_VEC0, 0xD5359E4, 0, {}, false, false, 0},
        {"no_spread", "NO::SPREAD", M_SCALE, 0xD53408C, 0, {}, false, false, 0},
        {"no_shot_spread", "NO::SPREAD", M_VEC0, 0xC2BDF40, 0, {}, false, false, 0},
        {"no_reload", "NO::RELOAD", M_TIME, 0xC2B5B58, 0, {}, false, false, 0},
        {"no_reload_start", "NO::RELOAD", M_TIME, 0xC2B5D88, 0, {}, false, false, 0},
        {"no_reload_end", "NO::RELOAD", M_TIME, 0xC2B5F90, 0, {}, false, false, 0},
        {"fast_scope", "FAST::SCOPE", M_TIME, 0x5474160, 0, {}, false, false, 0},
        {"fov_br", "FOV", M_VALUE, 0xC0B472C, 0, {}, false, false, 0},
        {"fov_mp", "FOV1", M_VALUE, 0xC0B4458, 0, {}, false, false, 0},
        {"snow", "SizeSnow", M_VALUE, 0x53FE7FC, 0, {}, false, false, 0},
        {"aim_assist", "AIM::ASSIST", M_VALUE, 0x54F0E64, 0, {}, false, false, 0},
        {"aim_assist_2", "AIM::ASSIST", M_VALUE, 0x66BD5E8, 0, {}, false, false, 0},
        {"fps", "FRAME120", M_FPS, 0xAFAC73C, 0, {}, false, false, 0},
        {"speed", "SPEED_HACK", M_SPEED, 0x530EF10, 0.016f, {}, false, false, 0},
        {"speed_fixed", "SPEED_HACK", M_SPEED, 0x534D8E4, 0.02f, {}, false, false, 0},
};

static int g_unityHooks = -1;

void codm_reset() {
    g_pid.store(0);
    g_unity = g_anogs = 0;
    g_match = 0;
    g_matchSlot = 0;
    g_slotLocked = false;
    g_slotHot = false;
    g_unityHooks = 0;
    g_nameScanDone = false;
    g_gameplayKlass = 0;
    g_blob = 0;
    for (auto &h : g_unityHooksTable) {
        h.haveOrig = false;
        h.applied = false;
        h.stamp = 0;
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

static float menu_slider(const char *key) {
    float v = 0.f;
    const auto it = seekbarValues.raw().find(key);
    if (it != seekbarValues.raw().end() && it->second > v) v = it->second;
    for (const auto &c : WeaponCfg::Store()) {
        float s = 0.f;
        if (std::strcmp(key, "NO::RECOIL") == 0) s = c.no_recoil;
        else if (std::strcmp(key, "NO::RELOAD") == 0) s = c.no_reload;
        else if (std::strcmp(key, "NO::SPREAD") == 0) s = c.no_spread;
        else if (std::strcmp(key, "FAST::SCOPE") == 0) s = c.fast_scope;
        else if (std::strcmp(key, "AIM::ASSIST") == 0) s = c.aim_assist;
        if (s > v) v = s;
    }
    return v;
}

static bool menu_on(const char *key) {
    if (checkboxStates[key]) return true;
    for (const auto &c : WeaponCfg::Store()) {
        if (std::strcmp(key, "HITBOX") == 0 && c.hitbox) return true;
        if (std::strcmp(key, "NO::SHAKE") == 0 && c.no_shake) return true;
    }
    return false;
}

static void put_u32(uint8_t *p, uint32_t w) { std::memcpy(p, &w, 4); }

static void put_f32_ret(uint8_t out[16], float f, uint32_t &stamp) {
    std::memset(out, 0, 16);
    put_u32(out, 0x1C000040u);
    put_u32(out + 4, 0xD65F03C0u);
    std::memcpy(out + 8, &f, 4);
    std::memcpy(&stamp, &f, 4);
}

static bool hook_payload(const UnityHook &h, uint8_t out[16], uint32_t &stamp) {
    std::memset(out, 0, 16);
    stamp = 0;
    switch (h.mode) {
        case M_TRUE:
            if (!menu_on(h.key)) return false;
            put_u32(out, 0xD2800020u);
            put_u32(out + 4, 0xD65F03C0u);
            stamp = 1;
            return true;
        case M_RET:
            if (!menu_on(h.key)) return false;
            put_u32(out, 0xD65F03C0u);
            put_u32(out + 4, 0xD65F03C0u);
            stamp = 1;
            return true;
        case M_CONST:
            if (!menu_on(h.key)) return false;
            put_f32_ret(out, h.constF, stamp);
            return true;
        case M_SCALE: {
            const float s = menu_slider(h.key);
            if (s <= 0.f) return false;
            float t = s / 100.f;
            if (t > 1.f) t = 1.f;
            put_f32_ret(out, 1.f - t, stamp);
            return true;
        }
        case M_VALUE: {
            const float s = menu_slider(h.key);
            if (s <= 0.f) return false;
            put_f32_ret(out, s, stamp);
            return true;
        }
        case M_TIME: {
            const float s = menu_slider(h.key);
            if (s <= 0.f) return false;
            float t = s / 100.f;
            if (t > 1.f) t = 1.f;
            const float sec = t >= 0.99f ? 0.01f : (1.f - t);
            put_f32_ret(out, sec, stamp);
            return true;
        }
        case M_VEC0: {
            if (menu_slider(h.key) <= 0.f) return false;
            put_u32(out, 0x1E2703E0u);
            put_u32(out + 4, 0x1E2703E1u);
            put_u32(out + 8, 0xD65F03C0u);
            stamp = 1;
            return true;
        }
        case M_SPEED: {
            float s = menu_slider(h.key);
            if (s <= 0.f) return false;
            if (s > 100.f) s = 100.f;
            const float mult = 1.f + (s / 100.f) * 10.f;
            put_f32_ret(out, h.constF * mult, stamp);
            return true;
        }
        case M_FPS: {
            int fps = 0;
            if (checkboxStates["FRAME120"]) fps = 120;
            else if (checkboxStates["FRAME40"]) fps = 40;
            if (!fps) return false;
            put_u32(out, 0xD2800000u | (uint32_t)((unsigned)fps << 5));
            put_u32(out + 4, 0xD65F03C0u);
            stamp = (uint32_t)fps;
            return true;
        }
    }
    return false;
}

int codm_auto_patch_unity() {
    const pid_t pid = g_pid.load();
    if (!pid || !g_unity) return 0;
    int applied = 0;
    for (auto &h : g_unityHooksTable) {
        uint8_t bytes[16] = {};
        uint32_t stamp = 0;
        const bool want = hook_payload(h, bytes, stamp);
        const uintptr_t addr = g_unity + h.rva;
        if (!want) {
            if (h.applied && h.haveOrig && mem_write(pid, addr, h.orig, sizeof(h.orig))) {
                h.applied = false;
                h.stamp = 0;
                ui_log("[unity] %s OFF — original restored", h.name);
            }
            continue;
        }
        if (h.applied && h.stamp == stamp) {
            ++applied;
            continue;
        }
        if (!h.haveOrig) {
            if (!mem_read(pid, addr, h.orig, sizeof(h.orig))) continue;
            h.haveOrig = true;
        }
        if (!mem_write(pid, addr, bytes, sizeof(bytes))) {
            ui_log("[unity] %s FAIL @ 0x%llx (%s)", h.name, (unsigned long long)addr, mem_write_error());
            continue;
        }
        h.applied = true;
        h.stamp = stamp;
        ++applied;
        if (h.mode == M_SCALE || h.mode == M_VALUE || h.mode == M_TIME || h.mode == M_CONST || h.mode == M_SPEED) {
            float shown = 0.f;
            std::memcpy(&shown, &stamp, 4);
            ui_log("[unity] %s = %.3f @ 0x%llx", h.name, shown, (unsigned long long)addr);
        } else if (h.mode == M_FPS) {
            ui_log("[unity] fps = %u @ 0x%llx", stamp, (unsigned long long)addr);
        } else {
            ui_log("[unity] %s ON @ 0x%llx", h.name, (unsigned long long)addr);
        }
    }
    g_unityHooks = applied;
    return applied;
}

int codm_unity_hook_count() { return g_unityHooks < 0 ? 0 : g_unityHooks; }

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
    codm_auto_patch_unity();
    if (!g_matchSlot) {
        uintptr_t slot = 0;
        if (resolve_match_slot(pid, g_unity + codm_off::kGameplay_get_MatchGame, slot)) {
            g_matchSlot = slot;
            g_slotLocked = true;
            uintptr_t now = 0;
            mem_read(pid, g_matchSlot, now);
            ui_log("[esp] s_MatchGame slot=0x%llx now=0x%llx", (unsigned long long)g_matchSlot,
                   (unsigned long long)now);
        } else {
            static bool logged = false;
            if (!logged) {
                logged = true;
                uint32_t head[12] = {};
                mem_read(pid, g_unity + codm_off::kGameplay_get_MatchGame, head, sizeof(head));
                ui_log("[esp] getter %08x %08x %08x %08x %08x %08x", head[0], head[1], head[2], head[3],
                       head[4], head[5]);
                ui_log("[esp] getter %08x %08x %08x %08x %08x %08x", head[6], head[7], head[8], head[9],
                       head[10], head[11]);
            }
            std::snprintf(out.status, sizeof(out.status), "libs OK — s_MatchGame not found");
            return;
        }
    }
    uintptr_t match = 0;
    mem_read(pid, g_matchSlot, match);
    if (!mem_ptr_ok(match)) {
        uintptr_t better = 0;
        uintptr_t obj = 0;
        if (g_blob && slot_from_blob(pid, g_blob, better) && mem_read(pid, better, obj) && mem_ptr_ok(obj)) {
            g_matchSlot = better;
            match = obj;
        } else {
            g_match = 0;
            static bool loggedEmpty = false;
            if (!loggedEmpty) {
                loggedEmpty = true;
                ui_log("[esp] s_MatchGame empty (lobby)");
            }
            std::snprintf(out.status, sizeof(out.status), "lobby — enter a match");
            return;
        }
    }
    uintptr_t listPtr = 0;
    int size = 0;
    if (!find_pawn_list(pid, match, listPtr, size)) {
        uintptr_t alt = 0;
        if (mem_read(pid, g_matchSlot + codm_off::kMatchGameField, alt) && mem_ptr_ok(alt) &&
            find_pawn_list(pid, alt, listPtr, size)) {
            g_matchSlot += codm_off::kMatchGameField;
            match = alt;
            ui_log("[esp] s_MatchGame is static+0x8 slot=0x%llx", (unsigned long long)g_matchSlot);
        }
    }
    if (!listPtr || size <= 0) {
        uintptr_t better = 0;
        if (g_blob && slot_from_blob(pid, g_blob, better)) {
            uintptr_t obj = 0;
            uintptr_t lp = 0;
            int sz = 0;
            if (mem_read(pid, better, obj) && find_pawn_list(pid, obj, lp, sz)) {
                g_matchSlot = better;
                match = obj;
                listPtr = lp;
                size = sz;
            }
        }
    }
    if (!listPtr || size <= 0) {
        g_match = 0;
        static uintptr_t loggedBad = 0;
        if (loggedBad != match) {
            loggedBad = match;
            char name[32] = {};
            instance_is_match(pid, match, name, sizeof(name));
            ui_log("[esp] vm_read ptr=0x%llx klass=%s has no pawn list", (unsigned long long)match,
                   name[0] ? name : "?");
        }
        if (!g_gameplayKlass && !g_slotHot) {
            g_matchSlot = 0;
            g_slotLocked = false;
        }
        std::snprintf(out.status, sizeof(out.status), "vm_read — no pawn list yet");
        return;
    }
    if (g_match != match) {
        g_match = match;
        ui_log("[esp] vm_read MatchGame=0x%llx pawns=%d", (unsigned long long)match, size);
    }
    out.match = match;
    const char *listName = "pawns";
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
    if (local && (read_vec3(pid, local + codm_off::kPawn_m_LastPawnPos, localX, localY, localZ) ||
                  read_vec3(pid, local + codm_off::kPawn_m_Location, localX, localY, localZ))) {
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
        if (!read_vec3(pid, pawn + codm_off::kPawn_m_LastPawnPos, x, y, z))
            read_vec3(pid, pawn + codm_off::kPawn_m_Location, x, y, z);
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