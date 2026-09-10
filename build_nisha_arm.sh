#!/bin/bash
# Build self-contained Nisha+Qt for GT6606 (sources under ./nisha_bus).
# Usage (VM): bash /mnt/win/Projects/QtProjects/TES91_GT6606/build_nisha_arm.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="${SRC:-$SCRIPT_DIR}"
DST="${DST:-$HOME/work/GT6606_nisha}"
QT_QMAKE="${QT_QMAKE:-/usr/local/htnice/Qt-5.8.0/bin/qmake}"
CROSS_BIN="${CROSS_BIN:-/usr/local/arm/gcc-linaro-arm-linux-gnueabihf-4.8-2014.04_linux/bin}"
JOBS="${JOBS:-2}"

if [ ! -f "$SRC/nisha_gt6606.pro" ]; then
  echo "ERROR: missing nisha_gt6606.pro in $SRC"
  exit 1
fi
if [ ! -f "$SRC/nisha_bus/Include/Common.h" ]; then
  echo "ERROR: missing nisha_bus (tongliao sources should be copied into TES91_GT6606)"
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
    --exclude='nisha_gt6606' \
    "$SRC/" "$DST/"
else
  rm -rf "$DST"
  mkdir -p "$DST"
  cp -a "$SRC"/. "$DST/"
fi

# bump build epoch for FirmwareVersion
EPOCH=$(date +%s)
cat > "$DST/nisha_bus/Include/build_version.h" <<EOF
#ifndef BUILD_VERSION_H
#define BUILD_VERSION_H
#define FW_BUILD_EPOCH ${EPOCH}u
#endif
EOF

cd "$DST"
cp -f "$DST/nisha/tyls.conf" "$DST/tyls.conf"

echo "==> qmake"
"$QT_QMAKE" ./nisha_gt6606.pro

echo "==> bump firmware version object"
rm -f ./FirmwareVersion.o
touch "$DST/nisha_bus/Source/FirmwareVersion.c"

echo "==> make -j$JOBS"
make -j"$JOBS"

cp -f "$DST/nisha/tyls.conf" "$DST/tyls.conf"

echo
echo "OK: $DST/nisha_gt6606"
file "$DST/nisha_gt6606" 2>/dev/null || true

OTA_PY="$DST/nisha_bus/libota/tools/ota_pack.py"
OTA_OUT="$DST/nisha_gt6606.ota"
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
  "$PY" "$OTA_PY" "$DST/nisha_gt6606" -c -o "$OTA_OUT" || echo "WARNING: ota_pack failed"
  if [ -f "$OTA_OUT" ]; then
    cp -f "$OTA_OUT" "$SRC/nisha_gt6606.ota"
    echo "OK: copied OTA package to $SRC/nisha_gt6606.ota"
  fi
else
  echo "WARNING: skip ota pack (missing script or python)"
fi

echo "Deploy: bash $SRC/deploy_nisha.sh"
