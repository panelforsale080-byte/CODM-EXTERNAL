# CODM-EXTERNAL

External overlay for **Garena CODM** (`com.garena.game.codm`), converted from **jnmaini.zip**.

- **No IL2CPP** in overlay — hardcoded `CLASS.h` / libunity RVAs + `remote_call`
- **ANOGS**: auto `BYPASS` patch from `vendor/jnmaini/hooks.h` via `tools/sync_anogs_bypass.py`
- **Unity gameplay hooks** (speed, recoil, etc.): still need in-process agent (jnmaini `hooking()`); ESP + anogs bypass work externally

## Sync anogs offsets after game update

```bash
python3 tools/sync_anogs_bypass.py
```

## Build

```bash
./gradlew assembleRelease
```