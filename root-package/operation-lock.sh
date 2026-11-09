#!/system/bin/sh

if [ "${1:-}" != "--lock-held" ]; then
    LOCK_DIR=/dev/airxpadfix-operation.lock
    if ! mkdir "$LOCK_DIR" 2>/dev/null; then
        echo "airxpadfix: another operation is active or its lock remains after interruption" >&2
        return 1
    fi
    trap 'rmdir "$LOCK_DIR" 2>/dev/null' EXIT
    trap 'exit 1' HUP INT TERM
fi
