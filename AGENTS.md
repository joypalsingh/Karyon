# Karyon Agent Instructions

1. **Rootless AOSP Container Principles:**
   - Karyon targets an installable Android app (`com.karyon.app`) that supervises a real system-level AOSP 14 userland container without requiring root access on the host phone.
   - Never fabricate or mock a fake Android boot screen, fake package installs, or fake GMS sign-in responses. All diagnostics, rootfs validations, child processes, sockets, and builds must reflect ground-truth state.

2. **Global Mechanisms Only:**
   - Never add hardcoded package names, class names, or signature-bypass hacks targeting a specific third-party guest app inside `app/src/main`.

3. **Host Compatibility & Verification:**
   - Keep executable container binaries compliant with Android 10+ W^X (`lib*.so` extracted to `nativeLibraryDir` via `useLegacyPackaging = true`).
   - Run `./gradlew :app:assembleDebug` and unit tests (`:app:testDebugUnitTest`) after changes, and verify on an Android emulator/device before reporting behavior.
