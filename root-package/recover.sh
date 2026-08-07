#!/system/bin/sh

INSTALL_DIR=/sdcard/Download/AirXPadFix
STATE_DIR="$INSTALL_DIR/state"

if [ "$(id -u)" != "0" ]; then
    echo "airxpadfix: recovery must run as root" >&2
    exit 1
fi

restore_state_file() {
    RECOVERY_FILE=$1
    [ -f "$RECOVERY_FILE" ] || return 0

    NODE=$(sed -n '1p' "$RECOVERY_FILE")
    MODE_DEC=$(sed -n '2p' "$RECOVERY_FILE")
    NODE_UID=$(sed -n '3p' "$RECOVERY_FILE")
    NODE_GID=$(sed -n '4p' "$RECOVERY_FILE")
    NODE_MAJOR=$(sed -n '5p' "$RECOVERY_FILE")
    NODE_MINOR=$(sed -n '6p' "$RECOVERY_FILE")
    NODE_CONTEXT=$(sed -n '7p' "$RECOVERY_FILE")

    case "$NODE" in
        /dev/input/event*) ;;
        *) echo "airxpadfix: refusing invalid recovery path: $NODE" >&2; return 1 ;;
    esac
    EVENT_NUMBER=${NODE#/dev/input/event}
    case "$EVENT_NUMBER" in
        ''|*[!0-9]*) echo "airxpadfix: refusing invalid recovery path: $NODE" >&2; return 1 ;;
    esac
    for VALUE in "$MODE_DEC" "$NODE_UID" "$NODE_GID" "$NODE_MAJOR" "$NODE_MINOR"; do
        case "$VALUE" in
            ''|*[!0-9]*) echo "airxpadfix: corrupt recovery metadata" >&2; return 1 ;;
        esac
    done
    if [ "$NODE_MAJOR" != "13" ]; then
        echo "airxpadfix: refusing non-input major number $NODE_MAJOR" >&2
        return 1
    fi

    if [ -e "$NODE" ]; then
        # Android has already published this event path, normally because /dev was rebuilt at boot.
        rm -f "$RECOVERY_FILE"
        return 0
    fi

    EVENT=${NODE##*/}
    SYSFS_DEV=$(cat "/sys/class/input/$EVENT/dev" 2>/dev/null)
    if [ "$SYSFS_DEV" != "$NODE_MAJOR:$NODE_MINOR" ]; then
        # A node cannot usefully be restored when its kernel device has gone away or the event index
        # was reassigned. If that controller returns, ueventd will create its new node normally.
        echo "airxpadfix: discarding stale recovery metadata for $EVENT"
        rm -f "$RECOVERY_FILE"
        return 0
    fi

    STAGING=/dev/input/.airxpadfix-recovery
    rm -f "$STAGING"
    if ! mknod "$STAGING" c "$NODE_MAJOR" "$NODE_MINOR"; then
        echo "airxpadfix: could not create recovery node" >&2
        return 1
    fi

    MODE_OCT=$(printf '%o' "$MODE_DEC")
    if ! chown "$NODE_UID:$NODE_GID" "$STAGING" || ! chmod "$MODE_OCT" "$STAGING"; then
        echo "airxpadfix: could not restore node ownership or permissions" >&2
        rm -f "$STAGING"
        return 1
    fi
    if [ -n "$NODE_CONTEXT" ]; then
        if ! command -v chcon >/dev/null 2>&1 || ! chcon "$NODE_CONTEXT" "$STAGING"; then
            echo "airxpadfix: could not restore SELinux label $NODE_CONTEXT" >&2
            rm -f "$STAGING"
            return 1
        fi
    fi
    if ! ln "$STAGING" "$NODE"; then
        echo "airxpadfix: could not publish recovered node $NODE" >&2
        rm -f "$STAGING"
        return 1
    fi
    rm -f "$STAGING" "$RECOVERY_FILE"
    echo "airxpadfix: restored $NODE"
}

RESULT=0
restore_state_file "$STATE_DIR/real-node.state" || RESULT=1
restore_state_file "$STATE_DIR/virtual-node.state" || RESULT=1
exit "$RESULT"
