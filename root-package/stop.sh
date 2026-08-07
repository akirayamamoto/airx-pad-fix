#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DIR=/dev/airxpadfix
RUNTIME_DAEMON="$RUNTIME_DIR/airxpadfixd"
PID_FILE="$INSTALL_DIR/state/airxpadfix.pid"

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: stop must run as root" >&2
    exit 1
fi

DAEMON_PID=
if [ -f "$PID_FILE" ]; then
    CANDIDATE=$(cat "$PID_FILE")
    CANDIDATE_EXE=$(readlink "/proc/$CANDIDATE/exe" 2>/dev/null)
    if [ "$CANDIDATE_EXE" = "$RUNTIME_DAEMON" ]; then
        DAEMON_PID=$CANDIDATE
    fi
fi
if [ -z "$DAEMON_PID" ]; then
    for EXE_LINK in /proc/[0-9]*/exe; do
        if [ "$(readlink "$EXE_LINK" 2>/dev/null)" = "$RUNTIME_DAEMON" ]; then
            CANDIDATE=${EXE_LINK#/proc/}
            DAEMON_PID=${CANDIDATE%%/*}
            break
        fi
    done
fi

if [ -n "$DAEMON_PID" ]; then
    kill -TERM "$DAEMON_PID" 2>/dev/null
    WAIT_COUNT=0
    while [ -d "/proc/$DAEMON_PID" ] && [ "$WAIT_COUNT" -lt 10 ]; do
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))
    done
    if [ -d "/proc/$DAEMON_PID" ]; then
        echo "airxpadfix: graceful stop timed out; forcing stop and recovering nodes" >&2
        kill -KILL "$DAEMON_PID" 2>/dev/null
        sleep 1
    fi
fi

rm -f "$PID_FILE"
if ! sh "$INSTALL_DIR/recover.sh"; then
    echo "airxpadfix: stopped, but node recovery requires attention" >&2
    exit 1
fi
rm -rf "$RUNTIME_DIR"
echo "airxpadfix: stopped; default controller nodes restored"
