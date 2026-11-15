#pragma once
#include <sys/types.h>
#include "mem.hpp"

struct CodmEnemy {
    float hp = 0.f;
    float maxHp = 0.f;
    float dist = 0.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float head = 0.f;
    int alive = 0;
    int bot = 0;
    char name[24] = {};
};

struct CodmFrame {
    pid_t pid = 0;
    uintptr_t unity = 0;
    uintptr_t anogs = 0;
    uintptr_t match = 0;
    int enemies = 0;
    int shown = 0;
    int viewOk = 0;
    int vpOk = 0;
    float vp[16] = {};
    float camX = 0.f, camY = 0.f, camZ = 0.f;
    float fx = 0.f, fy = 0.f, fz = 1.f;
    float rx = 1.f, ry = 0.f, rz = 0.f;
    float ux = 0.f, uy = 1.f, uz = 0.f;
    CodmEnemy list[24]{};
    char status[128] = {};
};

bool codm_libs_ready();

void codm_attach(pid_t pid);
void codm_reset();
void codm_tick(CodmFrame &out);
bool codm_auto_patch_anogs();
int codm_auto_patch_unity();
int codm_unity_hook_count();
bool codm_anogs_hooks_patched();
int codm_anogs_last_bypass_count();