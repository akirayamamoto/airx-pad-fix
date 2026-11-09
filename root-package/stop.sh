#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DIR=/dev/airxpadfix
RUNTIME_DAEMON="$RUNTIME_DIR/airxpadfixd"
PID_FILE="$INSTALL_DIR/state/airxpadfix.pid"

daemon_is_running() {
    [ -n "$DAEMON_PID" ] || return 1
    DAEMON_EXE=$(readlink "/proc/$DAEMON_PID/exe" 2>/dev/null)
    [ "$DAEMON_EXE" = "$RUNTIME_DAEMON" ]
}

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: stop must run as root" >&2
    exit 1
fi
. "$INSTALL_DIR/operation-lock.sh" || exit 1

RESULT=0
for EXE_LINK in /proc/[0-9]*/exe; do
    [ "$(readlink "$EXE_LINK" 2>/dev/null)" = "$RUNTIME_DAEMON" ] || continue
    CANDIDATE=${EXE_LINK#/proc/}
    DAEMON_PID=${CANDIDATE%%/*}
    kill -TERM "$DAEMON_PID" 2>/dev/null
    WAIT_COUNT=0
    while daemon_is_running && [ "$WAIT_COUNT" -lt 10 ]; do
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))
    done
    if daemon_is_running; then
        echo "airxpadfix: graceful stop timed out; forcing daemon stop" >&2
        kill -KILL "$DAEMON_PID" 2>/dev/null
        WAIT_COUNT=0
        while daemon_is_running && [ "$WAIT_COUNT" -lt 10 ]; do
            sleep 1
            WAIT_COUNT=$((WAIT_COUNT + 1))
        done
        if daemon_is_running; then
            echo "airxpadfix: daemon did not exit; refusing node recovery" >&2
            RESULT=1
        fi
    fi
done
[ "$RESULT" -eq 0 ] || exit 1

rm -f "$PID_FILE" || exit 1
if ! sh "$INSTALL_DIR/recover.sh"; then
    echo "airxpadfix: stopped, but node recovery requires attention" >&2
    exit 1
fi
rm -rf "$RUNTIME_DIR" || exit 1
echo "airxpadfix: stopped; default controller nodes restored"
