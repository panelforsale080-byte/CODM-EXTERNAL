#pragma once
#include <sys/types.h>
#include "mem.hpp"

struct CodmEnemy {
    float hp = 0.f;
    float maxHp = 0.f;
    int alive = 0;
};

struct CodmFrame {
    pid_t pid = 0;
    uintptr_t unity = 0;
    uintptr_t anogs = 0;
    int enemies = 0;
    int shown = 0;
    CodmEnemy list[12]{};
    char status[128] = {};
};

bool codm_libs_ready();

void codm_attach(pid_t pid);
void codm_reset();
void codm_tick(CodmFrame &out);
bool codm_auto_patch_anogs();
bool codm_anogs_hooks_patched();
int codm_anogs_last_bypass_count();