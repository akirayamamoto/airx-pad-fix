#!/system/bin/sh
if ! sh /sdcard/Download/AirXPadFix/stop.sh; then
    exit 1
fi
exec sh /sdcard/Download/AirXPadFix/start.sh
