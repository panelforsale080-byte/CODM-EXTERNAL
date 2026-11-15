#include "anogs_patch.hpp"
#include "mem.hpp"
#include "ui_log.hpp"
#include <vector>

#include <android/log.h>
#include <cstdio>
#include <cstdint>

#define CODMTAG "CodmAnogs"

// AUTO-GENERATED from vendor/jnmaini/hooks.h — run tools/sync_anogs_bypass.py

static const uint32_t kBypassOff[] = {
    0x204218,
    0x45B334,
    0x44A714,
    0x48F964,
};

static const char *kBypassSym[] = {
    "BYPASS@0x204218",
    "BYPASS@0x45B334",
    "BYPASS@0x44A714",
    "BYPASS@0x48F964",
};

static const uint8_t kArm64Mov0Ret[8] = {0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6};

int anogs_apply_bypass(pid_t pid, uintptr_t base, char *log, size_t logCap) {
    if (!pid || !base) {
        ui_log("[anogs] hooks.h BYPASS skip — no pid/base");
        return 0;
    }
    ui_log("[anogs] hooks.h BYPASS start pid=%d libanogs=0x%llx", (int)pid, (unsigned long long)base);
    __android_log_print(ANDROID_LOG_INFO, CODMTAG, "BYPASS start pid=%d base=0x%llx", (int)pid,
                        (unsigned long long)base);

    int ok = 0;
    const size_t n = sizeof(kBypassOff) / sizeof(kBypassOff[0]);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t off = kBypassOff[i];
        const uintptr_t addr = base + off;
        char perms[8] = "?";
        std::vector<MapsReg> maps;
        if (mem_get_maps(pid, maps)) {
            for (const auto &r : maps) {
                if (addr >= r.start && addr + sizeof(kArm64Mov0Ret) <= r.end) {
                    std::snprintf(perms, sizeof(perms), "%s", r.perms);
                    break;
                }
            }
        }
        if (mem_write(pid, addr, kArm64Mov0Ret, sizeof(kArm64Mov0Ret))) {
            ++ok;
            ui_log("[anogs] hooks.h %s OK @ 0x%llx via %s perms=%s", kBypassSym[i],
                   (unsigned long long)addr, mem_write_error(), perms);
            __android_log_print(ANDROID_LOG_INFO, CODMTAG, "%s OK addr=0x%llx", kBypassSym[i],
                                (unsigned long long)addr);
            if (log && logCap)
                std::snprintf(log, logCap, "%s OK", kBypassSym[i]);
        } else {
            ui_log("[anogs] hooks.h %s FAIL @ 0x%llx perms=%s (%s)", kBypassSym[i],
                   (unsigned long long)addr, perms, mem_write_error());
            __android_log_print(ANDROID_LOG_WARN, CODMTAG, "%s FAIL", kBypassSym[i]);
        }
    }
    ui_log("[anogs] hooks.h BYPASS done %d/%zu", ok, n);
    __android_log_print(ANDROID_LOG_INFO, CODMTAG, "BYPASS done %d/%zu", ok, (unsigned)n);
    return ok;
}