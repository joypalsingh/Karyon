# Bundled AOSP 14 RootFS Assets

Place generated `.tgz` bundles here before building `:app:assembleDebug` so `Karyon.apk` carries a self-contained RootFS for matching device ABIs (e.g. `karyon-aosp14-core-arm64-v8a.tgz` or `karyon-aosp14-core-x86_64.tgz`):

```powershell
python aosp\scripts\extract_prebuilt_aosp.py path\to\system.img --profile core --out app\src\main\assets\rootfs\karyon-aosp14-core-arm64-v8a.tgz
```

*(Note: Use `.tgz` rather than `.tar.gz` because `aapt2` automatically gunzips `.gz` assets during APK packaging, which expands a ~190 MB compressed bundle to ~480 MB. `.tgz` is preserved intact by `noCompress` in `app/build.gradle.kts`, and `app/src/main/assets/rootfs/*.tgz` is git-ignored because GitHub rejects files > 100 MB.)*
