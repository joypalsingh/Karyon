# Bundled AOSP 14 RootFS Assets

Place generated `.tar.gz` bundles here before building `:app:assembleDebug` so `Karyon.apk` carries a self-contained RootFS for matching device ABIs (e.g. `karyon-aosp14-core-x86_64.tar.gz` or `karyon-aosp14-core-arm64-v8a.tar.gz`):

```powershell
python aosp\scripts\extract_prebuilt_aosp.py path\to\system.img --profile core --out app\src\main\assets\rootfs\karyon-aosp14-core-x86_64.tar.gz
```

*(Note: `.tar*` archives are git-ignored here because GitHub rejects files > 100 MB, while `noCompress` in `app/build.gradle.kts` embeds them directly inside `app-debug.apk` without re-deflation.)*
