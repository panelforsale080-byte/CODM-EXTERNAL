# CODM jnmaini (source)

Native inject menu from **jnmaini.zip** — same `jni/` tree, no external overlay APK and no launcher login.

| Output | Path after build |
|--------|------------------|
| arm64 lib | `libs/arm64-v8a/libZAGll.so` |

## Build (local)

```bash
export ANDROID_NDK_HOME=/path/to/ndk
$ANDROID_NDK_HOME/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -C jni
```

## Use

Inject `libZAGll.so` into `com.garena.game.codm` the same way as the original jnmaini build. Menu is in-game (logo tap); hooks/offsets live in `jni/ImGui/hooks.h` and `jni/ImGui/CLASS.h`.

CI uploads **libZAGll-arm64** on each push to `main`.