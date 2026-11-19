# Karyon AOSP 14 RootFS Pipeline

This directory tracks the manifest, overlay configuration, and packaging scripts for building Karyon's **bootable AOSP 14 userland rootfs** (`karyon-rootfs-aosp14-arm64.tar`).

## Why Linux / WSL2 Is Required

AOSP's Soong build system (`lunch`, `m`, Bionic, `SurfaceFlinger`, `libbinder`, `ART`) only compiles on **64-bit Linux** (Ubuntu 22.04/24.04 or WSL2 with case-sensitive ext4 filesystem and ~150+ GB free disk space). Windows bare-metal builds cannot compile a full AOSP OS image.

> Note: While `Dexe` (`Dexe-main/android-platform.json`) pins AOSP 14 commits for Java framework + Windows library inspection, it does not compile Android `init`, `zygote`, `libart.so`, or `SurfaceFlinger` daemons for ARM64 Linux. Karyon uses `android-platform.json` only as a pinned source reference while full rootfs images come from a Linux AOSP build.

## Target Patches Required for Rootless Container Execution

To boot AOSP 14 inside a non-rooted Android APK (`com.karyon.app`):

1. **Userspace `libbinder` Socket Transport:** Redirect container Binder IPC (`servicemanager`, `system_server`, guest apps) over Karyon's `AF_UNIX` broker sockets inside `sockets/` so guest apps talk to the guest `PackageManagerService` without hitting host `/dev/binder` UID conflicts.
2. **Container Virtual HALs:**
   - `hwcomposer` / `gralloc` module streaming guest `SurfaceFlinger` frames into `KARYON_FB_SOCK`.
   - Virtual `input` & `audio` bridges reading events from `KARYON_CTRL_SOCK`.
3. **Path Translation / Virtual Root (`PRoot` / Custom Linker Overlay):** Rewrites guest `/system`, `/vendor`, `/data`, `/etc` into Karyon's instance directory (`instances/<id>/rootfs` & `overlay/`) and emulates `uid=0` for container `init`, `installd`, and `su`.
4. **GApps + `su` Overlay:** Places `PrebuiltGmsCore`, `Phonesky` (Play Store), `GoogleServicesFramework`, privileged XML permissions (`etc/permissions/`), and container `su` (`system/xbin/su`) inside the rootfs prior to packing.

## Packaging an AOSP Build Output into a Karyon RootFS Bundle

Once your Linux/WSL2 AOSP build (`$ANDROID_PRODUCT_OUT`) finishes:

```bash
./aosp/scripts/build_rootfs.sh \
  --product-out /path/to/aosp/out/target/product/vsoc_arm64_only \
  --gapps-dir /path/to/gapps_arm64 \
  --su-bin /path/to/container_su \
  --out ./karyon-rootfs-aosp14-arm64.tar
```

Then open **Karyon** on your phone -> **Import RootFS (.tar)** -> select `karyon-rootfs-aosp14-arm64.tar`.
