# CODM External APK

External overlay for **Call of Duty Mobile** (`com.garena.game.codm`), modeled on **mlbb-esp-pure**:

| Layer | Role |
|--------|------|
| **APK** | Floating ImGui overlay (no in-game inject, no license gate) |
| **Native** | Attach to game PID, read `libunity.so`, remote-call IL2CPP methods via **hardcoded RVAs** (`vendor/jnmaini/CLASS.h`) — no IL2CPP parser in the overlay |
| **ANOGS** | Auto-patch `BYPASS()` sites from `vendor/jnmaini/hooks.h` (MOV X0,#0; RET) when enabled in menu |
| **jni/** | Full **jnmaini.zip** reference tree (internal mod); sync hooks/offsets from here into `vendor/` |

## Menu (overlay)

- Auto-find / attach CODM (GSpace-friendly scoring)
- Toggle auto anogs patch
- Status: PID, `libunity` / `libanogs` base, enemy pawn count via `MatchGame` + `EnemyPawns`

Gameplay hooks (speed, recoil, etc.) from jnmaini still need an **in-process** agent; this build is **external read + anogs patch + menu** first.

## Regenerate anogs patches

```bash
python3 tools/sync_anogs_bypass.py
```

Reads `vendor/jnmaini/hooks.h`, writes `app/src/main/cpp/anogs_patch.cpp`.

## Build

```bash
./gradlew assembleRelease
```

CI: **Build APK** → artifact `codm-external-release`.

Package: `com.xorel.codm.external`