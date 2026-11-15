#!/usr/bin/env python3
"""Regenerate anogs_patch.cpp kBypassOff[] from vendor/jnmaini/hooks.h BYPASS lines."""
import re
from pathlib import Path

hooks = Path(__file__).resolve().parents[1] / "vendor/jnmaini/hooks.h"
out = Path(__file__).resolve().parents[1] / "app/src/main/cpp/anogs_patch.cpp"
text = hooks.read_text(encoding="utf-8", errors="ignore")
offs = re.findall(r'BYPASS\("?(0x[0-9A-Fa-f]+)"?\)', text)
if not offs:
    raise SystemExit("no BYPASS() offsets in hooks.h")
body = """#include "anogs_patch.hpp"
#include "mem.hpp"
#include <cstdio>
#include <cstdint>

// AUTO-GENERATED from vendor/jnmaini/hooks.h — run tools/sync_anogs_bypass.py

static const uint32_t kBypassOff[] = {
    %s
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
                std::snprintf(log, logCap, "patched 0x%%x @ %%llx", off, (unsigned long long)addr);
        }
    }
    return ok;
}
""" % (",\n    ".join("0x" + o[2:].upper() if o.lower().startswith("0x") else o for o in offs))
out.write_text(body)
print("wrote", out, "with", len(offs), "offsets")