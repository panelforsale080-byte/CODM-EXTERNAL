#pragma once
#include <cstdint>

/** jnmaini CLASS.h FastOffsets — libunity.so RVA, hardcoded (no IL2CPP in overlay). */
namespace codm_off {
constexpr uintptr_t kCamera_get_main = 0x52D5CDC;
constexpr uintptr_t kWorldToScreen = 0x52D5820;
constexpr uintptr_t kGameplay_get_MatchGame = 0x801191C;
constexpr uintptr_t kGameplay_get_LocalPawn = 0x8012814;
constexpr uintptr_t kEnemyPawns = 0x178;
constexpr uintptr_t kPawn_m_IsAlive = 0x550;
constexpr uintptr_t kPawn_m_Mesh = 0x630;
constexpr uintptr_t kTransform_get_position = 0x532DF40;
constexpr uintptr_t kComponent_get_transform = 0x52DAEC4;
constexpr uintptr_t kAttackableInfo = 0x78;
constexpr uintptr_t kHealth = 0x34;
constexpr uintptr_t kMaxHealth = 0x38;
} // namespace codm_off