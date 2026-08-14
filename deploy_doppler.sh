#!/bin/bash
# Deploy Doppler+Qt binary + conf + run script to board
set -e

BOARD_IP="${BOARD_IP:-192.168.101.230}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_DIR="${BOARD_DIR:-/work/time-difference}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${BIN:-$HOME/work/GT6606_doppler/doppler_gt6606}"
# 优先用仓库源码里的 UTF-8 conf，避免板上被写坏的旧文件残留
CONF="${CONF:-$SCRIPT_DIR/doppler/doppler.conf}"
if [ ! -f "$CONF" ]; then
  CONF="$HOME/work/GT6606_doppler/doppler.conf"
fi

if [ ! -f "$BIN" ]; then
  echo "ERROR: binary not found: $BIN"
  echo "Run build_doppler_arm.sh first."
  exit 1
fi

echo "==> scp -> ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/"
scp "$BIN" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/doppler_gt6606"
scp "$CONF" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/doppler.conf"
scp "$SCRIPT_DIR/run_doppler.sh" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/run_doppler.sh"

echo "OK. On board:"
echo "  cd $BOARD_DIR && chmod +x doppler_gt6606 run_doppler.sh && ./run_doppler.sh"
