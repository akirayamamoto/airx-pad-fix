#!/system/bin/sh

SOURCE_DIR=${0%/*}
INSTALL_DIR=/sdcard/Download/AirXPadFix

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: installer must run as root" >&2
    exit 1
fi
. "$SOURCE_DIR/operation-lock.sh" || exit 1
if [ ! -f "$SOURCE_DIR/airxpadfixd" ]; then
    echo "airxpadfix: airxpadfixd is missing from $SOURCE_DIR" >&2
    exit 1
fi
for FILE in airxpadfixd config.conf operation-lock.sh state-dir.sh recover.sh restart.sh start.sh \
    stop.sh status.sh uninstall.sh probe.sh; do
    if [ ! -f "$SOURCE_DIR/$FILE" ]; then
        echo "airxpadfix: package is missing $FILE" >&2
        exit 1
    fi
done
if [ -f "$INSTALL_DIR/stop.sh" ] && [ "$SOURCE_DIR" != "$INSTALL_DIR" ]; then
    . "$SOURCE_DIR/state-dir.sh" || exit 1
    if ! sh "$INSTALL_DIR/stop.sh" --lock-held; then
        echo "airxpadfix: existing installation could not be safely stopped" >&2
        exit 1
    fi
    if [ -f "$STATE_DIR/real-node.state" ] || [ -f "$STATE_DIR/virtual-node.state" ]; then
        echo "airxpadfix: existing installation still has pending recovery metadata" >&2
        exit 1
    fi
fi

SOURCE_REAL=$(cd "$SOURCE_DIR" 2>/dev/null && pwd -P)
mkdir -p "$INSTALL_DIR/state" || exit 1
INSTALL_REAL=$(cd "$INSTALL_DIR" 2>/dev/null && pwd -P)
if [ "$SOURCE_REAL" != "$INSTALL_REAL" ]; then
    for FILE in airxpadfixd operation-lock.sh state-dir.sh recover.sh restart.sh start.sh \
        stop.sh status.sh uninstall.sh probe.sh; do
        cp -f "$SOURCE_DIR/$FILE" "$INSTALL_DIR/$FILE" || exit 1
    done
    if [ ! -f "$INSTALL_DIR/config.conf" ]; then
        cp "$SOURCE_DIR/config.conf" "$INSTALL_DIR/config.conf" || exit 1
    fi
fi

echo "airxpadfix: installed in $INSTALL_DIR"
echo "Edit $INSTALL_DIR/config.conf, then use the AIR X root script runner to execute:"
echo "  sh $INSTALL_DIR/start.sh"
