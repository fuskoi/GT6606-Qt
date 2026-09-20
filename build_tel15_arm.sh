#!/bin/bash
# Build self-contained TEL-15+Qt for GT6606 (sources live under ./tel15_bus).
# Usage (VM): bash /mnt/win/Projects/QtProjects/GT6606_tel15/build_tel15_arm.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="${SRC:-$SCRIPT_DIR}"
DST="${DST:-$HOME/work/GT6606_tel15}"
QT_QMAKE="${QT_QMAKE:-/usr/local/htnice/Qt-5.8.0/bin/qmake}"
CROSS_BIN="${CROSS_BIN:-/usr/local/arm/gcc-linaro-arm-linux-gnueabihf-4.8-2014.04_linux/bin}"
JOBS="${JOBS:-2}"

if [ ! -f "$SRC/tel15_gt6606.pro" ]; then
  echo "ERROR: missing tel15_gt6606.pro in $SRC"
  exit 1
fi
if [ ! -f "$SRC/tel15_bus/Include/Common.h" ]; then
  echo "ERROR: missing tel15_bus"
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
    --exclude='tel15_gt6606' \
    "$SRC/" "$DST/"
else
  rm -rf "$DST"
  mkdir -p "$DST"
  cp -a "$SRC/." "$DST/"
fi

# Never compile original Sysinit from the bus copy
rm -f "$DST/tel15_bus/Source/Sysinit.c"

cd "$DST"
cp -f "$DST/tel15/tyls.conf" "$DST/tyls.conf"

# 共享盘时间戳常不准，旧 moc_*.cpp / *.o 不重编会直接 Segmentation fault
echo "==> clean stale build products"
rm -f moc_*.cpp moc_*.o *.o
rm -f tel15_ui/moc_*.cpp tel15_ui/*.o 2>/dev/null || true
find . -name 'moc_*.cpp' -delete 2>/dev/null || true
find . -name '*.o' -delete 2>/dev/null || true
rm -f Makefile tel15_gt6606
# 强制 moc 认为头文件更新
touch ./tel15_ui/tel15_mainwindow.h ./tel15_ui/section_view.h 2>/dev/null || true

echo "==> qmake"
"$QT_QMAKE" ./tel15_gt6606.pro

echo "==> make -j$JOBS"
make -j"$JOBS"

cp -f "$DST/tel15/tyls.conf" "$DST/tyls.conf"

echo
echo "OK: $DST/tel15_gt6606"
file "$DST/tel15_gt6606" 2>/dev/null || true

# 可执行文件留在 VM 的 $DST；同时拷一份到 Windows 共享目录方便查看
cp -f "$DST/tel15_gt6606" "$SRC/tel15_gt6606"
echo "OK: copied binary to $SRC/tel15_gt6606"

OTA_PY="$DST/tel15_bus/libota/tools/ota_pack.py"
OTA_OUT="$DST/tel15_gt6606.ota"
if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "WARNING: python not found, skip ota_pack"
  PY=
fi
if [ -n "$PY" ] && [ -f "$OTA_PY" ]; then
  echo "==> ota_pack -> $OTA_OUT"
  if "$PY" "$OTA_PY" "$DST/tel15_gt6606" -c -o "$OTA_OUT"; then
    cp -f "$OTA_OUT" "$SRC/tel15_gt6606.ota"
    echo "OK: copied OTA package to $SRC/tel15_gt6606.ota"
  else
    echo "WARNING: ota_pack failed (binary still OK at $DST/tel15_gt6606)"
  fi
else
  echo "WARNING: skip ota pack"
fi

echo "Deploy: bash $SRC/deploy_tel15.sh"
