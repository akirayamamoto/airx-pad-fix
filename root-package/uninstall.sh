#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DIR=/dev/airxpadfix
STATE_DIR="$INSTALL_DIR/state"

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: uninstaller must run as root" >&2
    exit 1
fi
if [ "$INSTALL_DIR" != "/sdcard/Download/AirXPadFix" ]; then
    echo "airxpadfix: refusing unexpected install path" >&2
    exit 1
fi

if ! sh "$INSTALL_DIR/stop.sh"; then
    echo "airxpadfix: uninstall aborted because default controller nodes were not restored" >&2
    exit 1
fi
if [ -f "$STATE_DIR/real-node.state" ] || [ -f "$STATE_DIR/virtual-node.state" ]; then
    echo "airxpadfix: uninstall aborted because recovery metadata remains" >&2
    exit 1
fi

rm -rf "$INSTALL_DIR"
rm -rf "$RUNTIME_DIR"
rm -f /sdcard/Download/AirXPadFix-start.sh \
    /sdcard/Download/AirXPadFix-restart.sh \
    /sdcard/Download/AirXPadFix-stop.sh \
    /sdcard/Download/AirXPadFix-status.sh \
    /sdcard/Download/AirXPadFix-uninstall.sh
echo "airxpadfix: uninstalled; its Downloads folder and runtime copy were removed"
