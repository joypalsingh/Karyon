#pragma once

// The Android version Dexe implements, and the runtime constants that depend on it. Porting to a newer Android
// version starts in android-platform.json and here; see HANDOFF.md, "Porting to a newer Android version".
// Keep version-specific values in this file instead of scattering literals through the runtime.

#include <cstdint>

namespace dexe::vm::platform {

// From android-platform.json, through CMake.
inline constexpr int32_t kApiLevel = DEXE_ANDROID_API_LEVEL;
inline constexpr const char* kRelease = DEXE_ANDROID_RELEASE;

// The core library's Android root has system/build.prop with the version it was built for (build_corelib.py).
inline constexpr const char* kBuildPropPath = "system/build.prop";

// The boot class path (Android's BOOTCLASSPATH), in order: the core library, the framework, and Dexe's own
// framework classes (its system services; on Android those live in system_server's services.jar). Jars that are
// not present in the Android root are skipped (the framework is built separately by build_framework.py).
inline constexpr const char* kFrameworkJar = "/system/framework/framework.jar";
inline constexpr const char* kBootClassPath[] = {
    "/apex/com.android.art/javalib/core.jar",
    kFrameworkJar,
    "/system/framework/dexe-services.jar",
};

// ART's ClassStatus values (art/runtime/class_status.h). libcore reads Class.status, so the values must match the
// ART version paired with this libcore.
inline constexpr int32_t kClassStatusNotReady = 0;
inline constexpr int32_t kClassStatusInitialized = 14;

// ART's class access flag for proxy classes (art/libdexfile/dex/modifiers.h), which libcore's Class.isProxy() tests.
inline constexpr uint32_t kAccClassIsProxy = 0x00040000;

}  // namespace dexe::vm::platform
