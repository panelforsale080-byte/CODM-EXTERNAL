#pragma once
#include <cstdint>

/** jnmaini CLASS.h FastOffsets — libunity.so RVA, hardcoded (no IL2CPP in overlay). */
namespace codm_off {
constexpr uintptr_t kCamera_get_main = 0x52D5CDC;
constexpr uintptr_t kPlayerCamera_get_WorldCamera = 0x6691290;
constexpr uintptr_t kWorldCameraField = 0x10;
constexpr uintptr_t kWorldToScreen = 0x52D5820;
constexpr uintptr_t kGameplay_get_MatchGame = 0x801191C;
constexpr uintptr_t kGameplay_get_LocalPawn = 0x8012814;
constexpr uintptr_t kEnemyPawns = 0x178;
constexpr uintptr_t kAllPawns = 0x160;
constexpr uintptr_t kPawn_m_IsAlive = 0x550;
constexpr uintptr_t kPawn_m_IsBot = 0x5C1;
constexpr uintptr_t kPawn_m_IsControlled = 0x131;
constexpr uintptr_t kPawn_m_Location = 0x5AC;
constexpr uintptr_t kPawn_m_LastPawnPos = 0x5D0;
constexpr uintptr_t kPawn_m_EyeHeight = 0x4F4;
constexpr uintptr_t kPawn_m_AimRotation = 0x388;
constexpr uintptr_t kPawn_m_PlayerInfo = 0x5C8;
constexpr uintptr_t kPlayerInfo_Nick = 0x170;
constexpr uintptr_t kPawn_m_Mesh = 0x630;
constexpr uintptr_t kTransform_get_position = 0x532DF40;
constexpr uintptr_t kComponent_get_transform = 0x52DAEC4;
constexpr uintptr_t kAttackableInfo = 0x78;
constexpr uintptr_t kHealth = 0x34;
constexpr uintptr_t kMaxHealth = 0x38;
constexpr uintptr_t kStaticFieldsA = 0xB8;
constexpr uintptr_t kStaticFieldsB = 0xA0;
constexpr uintptr_t kMatchGameField = 0x8;
} // namespace codm_off