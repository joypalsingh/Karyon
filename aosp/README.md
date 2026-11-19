# Karyon AOSP 14 RootFS Pipeline

Karyon supports **two** ways to produce an AOSP 14 rootfs `.tar`/`.tar.gz` bundle importable by `Karyon.apk`:

## Option 1: Prebuilt AOSP 14 Extraction (Pure Python, No Linux/WSL2 Needed)

Use [`aosp/scripts/extract_prebuilt_aosp.py`](scripts/extract_prebuilt_aosp.py) directly on Windows/macOS/Linux to extract official prebuilt Android 14 GSI or SDK images (`system.img` containing `GPT`, dynamic `super` / `liblp`, `ext4`, `.apex`/`.capex`, and GApps under `product/priv-app/`):

```powershell
# Inspect partitions, sizes, GApps/Play Store presence, and init binaries
python aosp\scripts\extract_prebuilt_aosp.py path\to\system.img --inspect

# Pack lightweight core userland (bin, lib64, framework, flattened ART/Runtime APEXes)
python aosp\scripts\extract_prebuilt_aosp.py path\to\system.img --profile core --out karyon-aosp14-core.tar.gz

# Pack full AOSP + product/GApps + minimal hardware-daemon pruning
python aosp\scripts\extract_prebuilt_aosp.py path\to\system.img --profile full --out karyon-aosp14-full.tar.gz
```

## Option 2: Building AOSP 14 from Source (Linux / WSL2 Required)

If building customized AOSP components from source (`soong` / `m`), AOSP requires 64-bit Linux (Ubuntu 22.04 or WSL2 with `ext4` + ~150+ GB disk space and swap).

> Note: While `Dexe` (`Dexe-main/android-platform.json`) pins AOSP 14 commits for Java framework + Windows library inspection, it does not compile Android `init`, `zygote`, `libart.so`, or `SurfaceFlinger` daemons for ARM64 Linux.

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
