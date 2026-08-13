#!/bin/bash
# Build self-contained Doppler+Qt for GT6606 (sources live under ./doppler_bus).
# Usage (VM): bash /mnt/win/Projects/QtProjects/GT6606/build_doppler_arm.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="${SRC:-$SCRIPT_DIR}"
DST="${DST:-$HOME/work/GT6606_doppler}"
QT_QMAKE="${QT_QMAKE:-/usr/local/htnice/Qt-5.8.0/bin/qmake}"
CROSS_BIN="${CROSS_BIN:-/usr/local/arm/gcc-linaro-arm-linux-gnueabihf-4.8-2014.04_linux/bin}"
JOBS="${JOBS:-2}"

if [ ! -f "$SRC/doppler_gt6606.pro" ]; then
  echo "ERROR: missing doppler_gt6606.pro in $SRC"
  exit 1
fi
if [ ! -f "$SRC/doppler_bus/Include/Common.h" ]; then
  echo "ERROR: missing doppler_bus (Doppler sources should be copied into GT6606)"
  exit 1
fi
if [ ! -x "$QT_QMAKE" ]; then
  echo "ERROR: qmake not found: $QT_QMAKE"
  exit 1
fi

export PATH="$PATH:$CROSS_BIN"

echo "==> sync $SRC -> $DST"
mkdir -p "$(dirname "$DST")"
if command -v rsync >/dev/null 2>&1; then
  rsync -a --delete \
    --exclude='.git/' \
    --exclude='*.o' \
    --exclude='moc_*.cpp' \
    --exclude='Makefile' \
    --exclude='doppler_gt6606' \
    --exclude='docs/' \
    --exclude='refs/' \
    "$SRC/" "$DST/"
else
  rm -rf "$DST"
  mkdir -p "$DST"
  for item in "$SRC"/*; do
    base=$(basename "$item")
    case "$base" in
      docs|refs) continue ;;
      *) cp -a "$item" "$DST/" ;;
    esac
  done
fi

# Never compile original Sysinit from the bus copy
rm -f "$DST/doppler_bus/Source/Sysinit.c"

cd "$DST"
cp -f "$DST/doppler/doppler.conf" "$DST/doppler.conf"

echo "==> qmake"
"$QT_QMAKE" ./doppler_gt6606.pro

echo "==> make -j$JOBS"
make -j"$JOBS"

cp -f "$DST/doppler/doppler.conf" "$DST/doppler.conf"

echo
echo "OK: $DST/doppler_gt6606"
file "$DST/doppler_gt6606" 2>/dev/null || true

# 打包 OTA（产品 ID + SHA256 尾），拷回 Windows 共享目录便于网页升级
OTA_PY="$DST/doppler_bus/libota/tools/ota_pack.py"
OTA_OUT="$DST/doppler_gt6606.ota"
if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "WARNING: python3/python not found, skip ota_pack"
  PY=
fi
if [ -n "$PY" ] && [ -f "$OTA_PY" ]; then
  echo "==> ota_pack -> $OTA_OUT"
  "$PY" "$OTA_PY" "$DST/doppler_gt6606" -c -o "$OTA_OUT"
  cp -f "$OTA_OUT" "$SRC/doppler_gt6606.ota"
  echo "OK: copied OTA package to $SRC/doppler_gt6606.ota"
else
  echo "WARNING: skip ota pack (missing script or python)"
fi

echo "Deploy: bash $SRC/deploy_doppler.sh"
