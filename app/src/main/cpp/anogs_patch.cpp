#include "anogs_patch.hpp"
#include "mem.hpp"
#include <cstdio>
#include <cstdint>

// AUTO-GENERATED from vendor/jnmaini/hooks.h — run tools/sync_anogs_bypass.py

static const uint32_t kBypassOff[] = {
    0x204218,
    0x45B334,
    0x44A714,
    0x48F964
};

static const uint8_t kArm64Mov0Ret[8] = {0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6};

int anogs_apply_bypass(pid_t pid, uintptr_t base, char *log, size_t logCap) {
    if (!pid || !base) return 0;
    int ok = 0;
    for (uint32_t off : kBypassOff) {
        const uintptr_t addr = base + off;
        if (mem_write(pid, addr, kArm64Mov0Ret, sizeof(kArm64Mov0Ret))) {
            ++ok;
            if (log && logCap)
                std::snprintf(log, logCap, "patched 0x%x @ %llx", off, (unsigned long long)addr);
        }
    }
    return ok;
}
