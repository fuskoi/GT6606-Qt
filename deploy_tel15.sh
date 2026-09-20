#!/bin/bash
# Deploy TEL-15+Qt binary + conf + run script to board
set -e

BOARD_IP="${BOARD_IP:-192.168.101.230}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_DIR="${BOARD_DIR:-/work/tel15}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${BIN:-$HOME/work/GT6606_tel15/tel15_gt6606}"
CONF="${CONF:-$SCRIPT_DIR/tel15/tyls.conf}"
if [ ! -f "$CONF" ]; then
  CONF="$HOME/work/GT6606_tel15/tyls.conf"
fi

if [ ! -f "$BIN" ]; then
  echo "ERROR: binary not found: $BIN"
  echo "Run build_tel15_arm.sh first."
  exit 1
fi

echo "==> scp -> ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/"
ssh "${BOARD_USER}@${BOARD_IP}" "mkdir -p ${BOARD_DIR}"
scp "$BIN" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/tel15_gt6606"
scp "$CONF" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/tyls.conf"
scp "$SCRIPT_DIR/run_tel15.sh" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/run_tel15.sh"

echo "OK. On board:"
echo "  cd $BOARD_DIR && chmod +x tel15_gt6606 run_tel15.sh && ./run_tel15.sh"
