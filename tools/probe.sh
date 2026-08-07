#!/system/bin/sh

echo "=== AIR X input probe ==="
date
echo "fingerprint=$(getprop ro.build.fingerprint)"
echo "android=$(getprop ro.build.version.release) sdk=$(getprop ro.build.version.sdk)"
echo "kernel=$(uname -a)"
echo "selinux=$(getenforce 2>/dev/null)"

echo
echo "=== Kernel input devices ==="
cat /proc/bus/input/devices

echo
echo "=== event nodes and sysfs ==="
for NODE in /dev/input/event*; do
    [ -e "$NODE" ] || continue
    EVENT=${NODE##*/}
    ROOT="/sys/class/input/$EVENT"
    echo "--- $NODE ---"
    echo "name=$(cat "$ROOT/device/name" 2>/dev/null)"
    echo "phys=$(cat "$ROOT/device/phys" 2>/dev/null)"
    echo "uniq=$(cat "$ROOT/device/uniq" 2>/dev/null)"
    echo "dev=$(cat "$ROOT/dev" 2>/dev/null)"
    echo "sysfs=$(readlink -f "$ROOT/device" 2>/dev/null)"
    ls -lZ "$NODE" 2>/dev/null || ls -l "$NODE"
    for ID in bustype vendor product version; do
        echo "$ID=$(cat "$ROOT/device/id/$ID" 2>/dev/null)"
    done
done
echo
echo "=== Android InputManager ==="
dumpsys input

echo
echo "=== Processes with /dev/uinput open (candidates only) ==="
for FD in /proc/[0-9]*/fd/*; do
    TARGET=$(readlink "$FD" 2>/dev/null)
    case "$TARGET" in
        /dev/uinput|/dev/input/uinput)
            PID=${FD#/proc/}
            PID=${PID%%/*}
            COMM=$(cat "/proc/$PID/comm" 2>/dev/null)
            CMD=$(tr '\000' ' ' < "/proc/$PID/cmdline" 2>/dev/null)
            echo "pid=$PID comm=$COMM cmd=$CMD fd=${FD##*/}"
            ;;
    esac
done
