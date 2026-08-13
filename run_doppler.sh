#!/bin/sh
# Run Doppler GT6606 Qt app with vendor Qt 5.8 env

APP="$(dirname "$0")/doppler_gt6606"
if [ ! -x "$APP" ]; then
  APP="./doppler_gt6606"
fi

# conf must sit next to the binary (GetAppDir + /doppler.conf)
cd "$(dirname "$APP")"

QTDIR=/usr/local/htnice/Qt-5.8.0
export QT_QPA_PLATFORM_PLUGIN_PATH=$QTDIR/plugins/
export QML2_IMPORT_PATH=$QTDIR/qml/
export LD_LIBRARY_PATH=$QTDIR/lib:$LD_LIBRARY_PATH

if [ -z "$QT_QPA_PLATFORM" ]; then
  # GT6606 上 eglfs 常失败，默认用 linuxfb
  export QT_QPA_PLATFORM=linuxfb
fi

echo "Qt: $QTDIR"
echo "Platform: $QT_QPA_PLATFORM"
echo "App: $(pwd)/$(basename "$APP")"
echo "Conf: $(pwd)/doppler.conf"
./"$(basename "$APP")" "$@"
ret=$?
# Hard-exit may leave buzzer latched ON (pulse wrote '1', timer never ran).
if [ -w /dev/buzzer ]; then
  printf '0' > /dev/buzzer 2>/dev/null || true
fi
if [ -w /dev/fb0 ]; then
  dd if=/dev/zero of=/dev/fb0 bs=1024 count=4096 >/dev/null 2>&1 || true
fi
exit $ret
