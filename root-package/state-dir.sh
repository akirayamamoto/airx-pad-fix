#!/system/bin/sh

STATE_DIR="$INSTALL_DIR/state"
if [ ! -f "$INSTALL_DIR/config.conf" ]; then
    echo "airxpadfix: config is missing; recovery state location is unknown" >&2
    return 1
fi
STATE_DIR=$(awk -v default="$STATE_DIR" '
        BEGIN { state = default }
        /^[[:space:]]*#/ { next }
        index($0, "=") {
            key = substr($0, 1, index($0, "=") - 1)
            value = substr($0, index($0, "=") + 1)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            if (key == "state_dir") state = value
        }
        END { if (state == "") exit 1; print state }
' "$INSTALL_DIR/config.conf") || {
    echo "airxpadfix: could not read recovery state directory from config" >&2
    return 1
}
case "$STATE_DIR" in
    /*) ;;
    *) echo "airxpadfix: recovery state directory must be an absolute path" >&2; return 1 ;;
esac
