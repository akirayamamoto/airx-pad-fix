#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DIR=/dev/airxpadfix

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: uninstaller must run as root" >&2
    exit 1
fi
if [ "$INSTALL_DIR" != "/sdcard/Download/AirXPadFix" ]; then
    echo "airxpadfix: refusing unexpected install path" >&2
    exit 1
fi
. "$INSTALL_DIR/operation-lock.sh" || exit 1
. "$INSTALL_DIR/state-dir.sh" || exit 1

if ! sh "$INSTALL_DIR/stop.sh" --lock-held; then
    echo "airxpadfix: uninstall aborted because default controller nodes were not restored" >&2
    exit 1
fi
if [ -f "$STATE_DIR/real-node.state" ] || [ -f "$STATE_DIR/virtual-node.state" ]; then
    echo "airxpadfix: uninstall aborted because recovery metadata remains" >&2
    exit 1
fi

if ! rm -rf "$RUNTIME_DIR"; then
    echo "airxpadfix: could not remove runtime directory" >&2
    exit 1
fi
if ! rm -f /sdcard/Download/AirXPadFix-start.sh \
    /sdcard/Download/AirXPadFix-restart.sh \
    /sdcard/Download/AirXPadFix-stop.sh \
    /sdcard/Download/AirXPadFix-status.sh; then
    echo "airxpadfix: could not remove launcher scripts" >&2
    exit 1
fi
if ! rm -rf "$INSTALL_DIR" || ! rm -f /sdcard/Download/AirXPadFix-uninstall.sh; then
    echo "airxpadfix: could not remove all installed files" >&2
    exit 1
fi
echo "airxpadfix: uninstalled; its Downloads folder and runtime copy were removed"
