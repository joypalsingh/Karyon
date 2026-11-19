<div align="center">

# karyon.

**Rootless system-level AOSP 14 container runtime and workbench for Android.**

![Host](https://img.shields.io/badge/Host-Android%209%2B%20(Non--Root)-C6F432?style=flat-square&labelColor=161814)
![Guest](https://img.shields.io/badge/Guest%20Target-AOSP%2014%20(API%2034)-C6F432?style=flat-square&labelColor=161814)
![C++](https://img.shields.io/badge/C%2B%2B-20-C6F432?style=flat-square&labelColor=161814)
![UI](https://img.shields.io/badge/UI-Jetpack%20Compose-C6F432?style=flat-square&labelColor=161814)

</div>

---

## Overview

**Karyon** is a system-level Android-on-Android container platform built to boot an independent **AOSP 14 userland OS** inside a standard, **non-rooted** Android app (`com.karyon.app`).

Unlike single-app Binder-hooking sandboxes (which share the host `PackageManager`, host `GmsCore`, and host APK signing certificate — breaking third-party Google Play Games sync and cross-app isolation), Karyon separates the architecture cleanly into two halves:

1. **Host Supervisor & Workbench APK (`Karyon.apk`)**:
   - Manages isolated VM instances (`rootfs/`, `overlay/`, `snapshots/`, `sockets/`), Copy-on-Write delta cloning, and point-in-time snapshots.
   - Executes container bootstrap helpers from `nativeLibraryDir` (`libkaryon_init.so`) in compliance with Android 10+ W^X policies.
   - Runs a real `AF_UNIX` IPC & Framebuffer Broker (`karyon_ctrl.sock`, `karyon_fb.sock`) driving an `ANativeWindow` hardware `SurfaceView` viewport.
   - Probes host constraints (`SELinux`, `memfd_create`, ABI, Android 12+ Phantom Process Killer guidance).
   - Reuses Dexe's C++20 `dexe_core` DEX disassembler and binary `AndroidManifest.xml` (`axml_parser`) engine for APK & DEX inspection.

2. **Patched AOSP 14 Guest RootFS (`aosp/`) — Built on Linux / WSL2**:
   - Compiles an AOSP 14 userland rootfs (`init`, `servicemanager`, `system_server`, `zygote`, `SurfaceFlinger`, `installd`, `su`, and optional GApps/Play Store packages) using userspace socket `libbinder` routing and Virtual HAL bridges into Karyon's host display/input/audio sockets.

---

## Architecture

```mermaid
flowchart TB
    subgraph Host["Host Non-Rooted Android App (com.karyon.app)"]
        UI["Jetpack Compose Workbench<br/><sub>VM Manager · SurfaceView Viewport · Root Shell · APK/DEX Inspector</sub>"]
        SUP["Native Process Supervisor (C++20 / NDK)<br/><sub>execve(libkaryon_init.so) · Host Probe · RootFS Store (Clone/Snapshot)</sub>"]
        BRK["AF_UNIX Socket & ANativeWindow Broker<br/><sub>karyon_ctrl.sock · karyon_fb.sock -> SurfaceView</sub>"]
        UI <--> SUP
        UI <--> BRK
    end

    subgraph Guest["Isolated AOSP 14 Container (Userland RootFS)"]
        INIT["libkaryon_init.so -> Guest init / system_server<br/><sub>Userspace Socket libbinder · PackageManager · ART</sub>"]
        SYS["Guest Services & Apps<br/><sub>Play Store / GmsCore · Play Games · Guest APKs · Container su</sub>"]
        VHAL["Virtual HALs (hwcomposer / gralloc / audio)"]
        INIT --> SYS
        SYS --> VHAL
    end

    SUP <-->|"Pipes & Lifecycle"| INIT
    VHAL -->|"RGBA / Hardware Frames over AF_UNIX"| BRK
```

---

## Project Status

- **Phase 1 (Implemented & Verified):** Host APK (`com.karyon.app`, `targetSdk = 35`), C++20 `dexe_core` + `axml_parser`, VM `RootfsStore` (ensure/clone/snapshot/restore/delete), Host Capability Probe (`memfd`, `execmem`, `fileExec`, `init`, Phantom Process checks), `libkaryon_init.so` W^X child supervisor, `AF_UNIX` control & RGBA framebuffer sockets driving `ANativeWindow` `SurfaceView`, TAR RootFS importer with symlink + traversal safety, diagnostic shell, and APK/DEX inspector.
- **Phase 2A (Implemented & Verified):**
  - **Zero-WSL2 Prebuilt AOSP 14 Python Extractor** ([`extract_prebuilt_aosp.py`](aosp/scripts/extract_prebuilt_aosp.py)): Reads Android sparse images, GPT disks, `super` dynamic partitions (`liblp`), `ext4` volumes, System-as-Root (`SAR`), and compressed `.apex`/`.capex` payloads directly on Windows into `.tar` / `.tar.gz` (`--profile core` or `--profile full` with GApps & minimal daemon `.rc` pruning).
  - **`targetSdk = 35` Userspace ELF64 + `PT_INTERP` Loader (`libkaryon_elf_exec.so`):** Maps prebuilt AOSP 14 ELF64 binaries + `linker64` from writable `filesDir` into anonymous `RW -> RX` memory (`execmem`) cleanly without root (`elf-exec system/bin/toybox uname -a` verified on Android 14).
  - **Consolidated Minimal-Daemon Binder Registry (`binder_broker.cpp`):** Consolidates core OS service handles (`karyon.supervisor`, `karyon.binder`, `karyon.su`, `SurfaceFlinger`, `package`, `activity`, `window`, `input`) inside the supervisor process to stay below Android 12+ child-process limits, paired with a one-tap **Developer Options** shortcut (`Disable child process restrictions`).
- **Phase 2B (Next):** Preload/`seccomp` `/dev/binder` `ioctl` redirect + rootfs path/UID (`su`) translation + Virtual HAL (`SurfaceFlinger` -> `karyon_fb` socket) to boot full multi-app `system_server` + `ART` + GApps from imported prebuilt AOSP rootfs images.

---

## Host Requirements & Honest Constraints (Non-Rooted Android)

- **Android 12–15 Phantom Process Killer:** Stock Android limits child processes (`max_phantom_processes=32`). Full multi-process AOSP + GMS requires disabling child process restrictions via Developer Options ("Disable child process restrictions" where available) or Wireless Debugging (`device_config put activity_manager max_phantom_processes 2147483647`). Karyon probes Android version and provides copy-ready commands in the Dashboard.
- **Play Integrity:** Inside an uncertified userland AOSP rootfs with `su`, `GmsCore` sees each guest app's genuine package name and signing certificate via the guest `PackageManager`, enabling standard Google Play sign-in once GApps is installed in the rootfs, while apps enforcing strict hardware Play Integrity (`MEETS_DEVICE_INTEGRITY` / `MEETS_STRONG_INTEGRITY`) may still detect an uncertified/rooted environment.

---

## Building Karyon.apk

### Prerequisites
- Android SDK Platform 35 & Build-Tools
- Android NDK `27.0.12077973` & CMake `3.22.1`
- JDK 17+

```powershell
.\gradlew.bat :app:assembleDebug :app:testDebugUnitTest
```
