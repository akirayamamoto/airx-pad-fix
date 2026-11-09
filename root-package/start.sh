#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
RUNTIME_DIR=/dev/airxpadfix
RUNTIME_DAEMON="$RUNTIME_DIR/airxpadfixd"
PID_FILE="$INSTALL_DIR/state/airxpadfix.pid"
LOG_FILE="$INSTALL_DIR/state/airxpadfix.log"

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: start must run as root" >&2
    exit 1
fi
. "$INSTALL_DIR/operation-lock.sh" || exit 1
. "$INSTALL_DIR/state-dir.sh" || exit 1

mkdir -p "$INSTALL_DIR/state"

find_daemon_pid() {
    for EXE_LINK in /proc/[0-9]*/exe; do
        if [ "$(readlink "$EXE_LINK" 2>/dev/null)" = "$RUNTIME_DAEMON" ]; then
            FOUND_PID=${EXE_LINK#/proc/}
            echo "${FOUND_PID%%/*}"
            return 0
        fi
    done
    return 1
}

OLD_PID=$(find_daemon_pid)
if [ -n "$OLD_PID" ]; then
    echo "$OLD_PID" > "$PID_FILE"
    echo "airxpadfix: already running as pid $OLD_PID"
    exit 0
fi
if [ -f "$PID_FILE" ]; then
    rm -f "$PID_FILE"
fi

# Repair any node left hidden by an earlier crash before starting a new daemon.
if ! sh "$INSTALL_DIR/recover.sh"; then
    echo "airxpadfix: recovery failed; refusing to start" >&2
    exit 1
fi
rm -f "$STATE_DIR/mode" || exit 1

# Shared storage is normally mounted noexec. Copy only the executable into /dev tmpfs; persistent
# configuration, logs, and recovery metadata remain in Downloads.
mkdir -p "$RUNTIME_DIR" || exit 1
cp -f "$INSTALL_DIR/airxpadfixd" "$RUNTIME_DAEMON" || exit 1
chmod 0700 "$RUNTIME_DAEMON" || exit 1
if command -v chcon >/dev/null 2>&1; then
    chcon u:object_r:system_file:s0 "$RUNTIME_DIR" "$RUNTIME_DAEMON" 2>/dev/null || true
fi

if command -v nohup >/dev/null 2>&1; then
    nohup "$RUNTIME_DAEMON" --config "$INSTALL_DIR/config.conf" \
        </dev/null >>"$LOG_FILE" 2>&1 &
else
    "$RUNTIME_DAEMON" --config "$INSTALL_DIR/config.conf" \
        </dev/null >>"$LOG_FILE" 2>&1 &
fi
DAEMON_PID=$!
echo "$DAEMON_PID" > "$PID_FILE"
sleep 1

DAEMON_EXE=$(readlink "/proc/$DAEMON_PID/exe" 2>/dev/null)
if [ "$DAEMON_EXE" != "$RUNTIME_DAEMON" ]; then
    echo "airxpadfix: daemon did not start; inspect $LOG_FILE" >&2
    rm -f "$PID_FILE"
    sh "$INSTALL_DIR/recover.sh"
    rm -rf "$RUNTIME_DIR"
    exit 1
fi

echo "airxpadfix: started as pid $DAEMON_PID"
