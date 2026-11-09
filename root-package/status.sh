#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DAEMON=/dev/airxpadfix/airxpadfixd
PID_FILE="$INSTALL_DIR/state/airxpadfix.pid"
LOG_FILE="$INSTALL_DIR/state/airxpadfix.log"
. "$INSTALL_DIR/state-dir.sh" || exit 1
MODE_FILE="$STATE_DIR/mode"

DAEMON_PID=
for EXE_LINK in /proc/[0-9]*/exe; do
    if [ "$(readlink "$EXE_LINK" 2>/dev/null)" = "$RUNTIME_DAEMON" ]; then
        CANDIDATE=${EXE_LINK#/proc/}
        DAEMON_PID=${CANDIDATE%%/*}
        break
    fi
done
if [ -n "$DAEMON_PID" ]; then
    echo "airxpadfix: running (pid $DAEMON_PID)"
else
    echo "airxpadfix: not running"
fi
if [ -f "$MODE_FILE" ]; then
    echo "mode: $(cat "$MODE_FILE")"
fi

for STATE_FILE in "$STATE_DIR/real-node.state" "$STATE_DIR/virtual-node.state"; do
    if [ -f "$STATE_FILE" ]; then
        echo "hidden node: $(sed -n '1p' "$STATE_FILE")"
    fi
done

if [ -f "$LOG_FILE" ]; then
    echo "--- recent log ---"
    tail -n 20 "$LOG_FILE"
fi
