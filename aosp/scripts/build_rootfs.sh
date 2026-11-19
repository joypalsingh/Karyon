#!/usr/bin/env bash
set -euo pipefail

# Packages a compiled Linux AOSP product output + optional GApps & container su
# into a TAR bundle importable by Karyon.apk.

PRODUCT_OUT=""
GAPPS_DIR=""
SU_BIN=""
OUT_TAR="karyon-rootfs-aosp14-arm64.tar"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --product-out) PRODUCT_OUT="$2"; shift 2 ;;
    --gapps-dir)   GAPPS_DIR="$2"; shift 2 ;;
    --su-bin)      SU_BIN="$2"; shift 2 ;;
    --out)         OUT_TAR="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$PRODUCT_OUT" || ! -d "$PRODUCT_OUT/system" ]]; then
  echo "Error: --product-out must point to an AOSP out/target/product/<target> directory containing system/" >&2
  exit 1
fi

STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT

mkdir -p "$STAGE_DIR/system" "$STAGE_DIR/vendor" "$STAGE_DIR/data" "$STAGE_DIR/etc"

echo "[1/4] Staging AOSP system/ and vendor/..."
cp -a "$PRODUCT_OUT/system/." "$STAGE_DIR/system/"
if [[ -d "$PRODUCT_OUT/vendor" ]]; then
  cp -a "$PRODUCT_OUT/vendor/." "$STAGE_DIR/vendor/"
fi

# Verify mandatory AOSP binaries exist in staged tree
for req in "system/bin/init" "system/framework/services.jar"; do
  if [[ ! -e "$STAGE_DIR/$req" ]]; then
    echo "Error: Missing required AOSP file '$req' in product output." >&2
    exit 1
  fi
done

if [[ -n "$GAPPS_DIR" ]]; then
  echo "[2/4] Overlay GApps / Play Store from $GAPPS_DIR..."
  if [[ -d "$GAPPS_DIR/system" ]]; then
    cp -a "$GAPPS_DIR/system/." "$STAGE_DIR/system/"
  else
    cp -a "$GAPPS_DIR/." "$STAGE_DIR/system/priv-app/"
  fi
else
  echo "[2/4] Skipping GApps overlay (--gapps-dir not set)."
fi

if [[ -n "$SU_BIN" ]]; then
  echo "[3/4] Installing container-scoped su binary..."
  mkdir -p "$STAGE_DIR/system/xbin"
  install -m 0755 "$SU_BIN" "$STAGE_DIR/system/xbin/su"
else
  echo "[3/4] Skipping container su (--su-bin not set)."
fi

cat > "$STAGE_DIR/system/etc/karyon_rootfs.json" <<EOF
{
  "format": "karyon-rootfs-v1",
  "android_release": "14",
  "api_level": 34,
  "has_gapps": $([[ -n "$GAPPS_DIR" ]] && echo "true" || echo "false"),
  "has_su": $([[ -n "$SU_BIN" ]] && echo "true" || echo "false")
}
EOF

echo "[4/4] Creating $OUT_TAR..."
tar -cf "$OUT_TAR" -C "$STAGE_DIR" system vendor data etc
echo "Done: $OUT_TAR"
