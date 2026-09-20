#!/bin/sh
# Run TEL-15 GT6606 Qt app with vendor Qt 5.8 env
# Also sets host route for SocketServerIP: eth0 (LAN test) or usb0 (4G).
# Usage: ./run_tel15.sh [optional_slot_elf]

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT" || exit 1

if [ -n "$1" ] && [ -x "$1" ]; then
  APP="$1"
  shift
elif [ -x "$ROOT/tel15_gt6606" ]; then
  APP="$ROOT/tel15_gt6606"
else
  APP="./tel15_gt6606"
fi

QTDIR=/usr/local/htnice/Qt-5.8.0
export QT_QPA_PLATFORM_PLUGIN_PATH=$QTDIR/plugins/
export QML2_IMPORT_PATH=$QTDIR/qml/
export LD_LIBRARY_PATH=$QTDIR/lib:$LD_LIBRARY_PATH

if [ -z "$QT_QPA_PLATFORM" ]; then
  export QT_QPA_PLATFORM=linuxfb
fi
export QT_QPA_FB_FORCE_FULLSCREEN="${QT_QPA_FB_FORCE_FULLSCREEN:-1}"

iface_has_ipv4() {
  ip -4 addr show "$1" 2>/dev/null | grep -q 'inet '
}

iface_carrier_up() {
  ip link show "$1" 2>/dev/null | grep -q 'state UP'
}

read_conf_val() {
  key="$1"
  [ -f ./tyls.conf ] || return 0
  sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" ./tyls.conf \
    | head -n1 | tr -d '\r' | sed 's/[[:space:]]*$//'
}

is_private_ipv4() {
  case "$1" in
    10.*|192.168.*|172.1[6-9].*|172.2[0-9].*|172.3[0-1].*|169.254.*) return 0 ;;
    *) return 1 ;;
  esac
}

wait_iface_ip() {
  ifc="$1"
  n=0
  while [ "$n" -lt 15 ]; do
    if iface_has_ipv4 "$ifc"; then
      return 0
    fi
    n=$((n + 1))
    sleep 1
  done
  return 1
}

setup_uplink_route() {
  server="$(read_conf_val SocketServerIP)"
  prefer="$(read_conf_val PreferUplink)"
  [ -n "$PreferUplink" ] && prefer="$PreferUplink"
  [ -z "$prefer" ] && prefer=auto
  [ -z "$server" ] && {
    echo "Uplink: no SocketServerIP in tyls.conf"
    return 0
  }

  eth_ok=0
  usb_ok=0
  if iface_carrier_up eth0 && iface_has_ipv4 eth0; then eth_ok=1; fi
  if iface_carrier_up usb0; then
    if ! iface_has_ipv4 usb0; then
      echo "Uplink: waiting for usb0 address..."
      wait_iface_ip usb0 || true
    fi
    if iface_has_ipv4 usb0; then usb_ok=1; fi
  fi

  choice=
  case "$prefer" in
    eth0|ETH0) choice=eth0 ;;
    usb0|USB0|4g|4G) choice=usb0 ;;
    *)
      if [ "$eth_ok" = 1 ] && is_private_ipv4 "$server"; then
        choice=eth0
      elif [ "$usb_ok" = 1 ]; then
        choice=usb0
      elif [ "$eth_ok" = 1 ]; then
        choice=eth0
      fi
      ;;
  esac

  if [ -z "$choice" ]; then
    echo "Uplink: $server — no eth0/usb0 ready (prefer=$prefer)"
    return 0
  fi
  if ! iface_has_ipv4 "$choice"; then
    echo "Uplink: $choice has no IPv4, skip route for $server"
    return 0
  fi
  if ip route replace "$server/32" dev "$choice" 2>/dev/null; then
    echo "Uplink: $server/32 via $choice (prefer=$prefer)"
  else
    echo "Uplink: failed to set $server/32 via $choice (need root?)"
  fi
}

setup_uplink_route

# 旧实例占着 /dev/fb0 时，新进程 linuxfb 会直接 Segmentation fault
if pidof tel15_gt6606 >/dev/null 2>&1; then
  echo "Stopping previous tel15_gt6606..."
  killall tel15_gt6606 2>/dev/null || true
  sleep 1
  killall -9 tel15_gt6606 2>/dev/null || true
  sleep 0.3
fi

echo "Qt: $QTDIR"
echo "Platform: $QT_QPA_PLATFORM"
echo "App: $APP"
echo "Conf: $ROOT/tyls.conf"
"$APP" "$@"
ret=$?
if [ -w /dev/buzzer ]; then
  printf '0' > /dev/buzzer 2>/dev/null || true
fi
if [ -w /dev/fb0 ]; then
  dd if=/dev/zero of=/dev/fb0 bs=1024 count=4096 >/dev/null 2>&1 || true
fi
exit $ret
