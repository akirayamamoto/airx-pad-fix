# AIR X Controller / Xbox 360 Compatibility Fix

AirXPadFix is a standalone root daemon for the MANGMI AIR X running Android 14. It fixes the
controller-order problem caused by the firmware's persistent, non-input-producing `Xbox Wireless
Controller` and can optionally present the awake physical controls as a standard Xbox 360 controller.

It is not an Android application or Magisk module. It does not modify the system partition. The
persistent package, configuration, logs, and recovery data live in `Download/AirXPadFix`; only a
temporary executable copy is placed under `/dev` while the daemon is running.

The implementation and identities documented below were verified on an AIR X:

- physical controls: `AIR X Controller`, USB ID `4001:0428`;
- firmware virtual controller: `Xbox Wireless Controller`, USB ID `2020:0112`;
- generated Xbox proxy: `Microsoft X-Box 360 pad`, USB ID `045e:028e`;
- Android key layout: `/system/usr/keylayout/Vendor_045e_Product_028e.kl`;
- the generated controller receives Android `ControllerNumber: 1`, which is the first gamepad/player
  position.

## Operating modes

The mode is selected in `/sdcard/Download/AirXPadFix/config.conf`.

### Xbox 360 proxy mode (default)

```ini
xbox_proxy=true
```

When the hardware controls are awake, the daemon:

1. opens and exclusively grabs the physical AIR X event stream;
2. saves recovery metadata and removes the original AIR X and firmware virtual nodes from Android;
3. creates a `uinput` controller named `Microsoft X-Box 360 pad` with USB ID `045e:028e`;
4. forwards the face buttons, shoulders, stick buttons, Start, Select, Guide, D-pad, sticks, and
   triggers using the Linux Xbox 360 axis layout;
5. creates a small non-gamepad device named `AIR X Extra Keys` to preserve the AIR X Back key.

The important axis translations are:

| AIR X hardware | Xbox proxy | Android result |
| --- | --- | --- |
| `ABS_X`, `ABS_Y` | `ABS_X`, `ABS_Y` | Left stick X/Y |
| `ABS_Z`, `ABS_RZ` | `ABS_RX`, `ABS_RY` | Right stick X/Y |
| `ABS_BRAKE` | `ABS_Z` | Left trigger |
| `ABS_GAS` | `ABS_RZ` | Right trigger |
| `ABS_HAT0X`, `ABS_HAT0Y` | unchanged | D-pad |

When the physical controls sleep, the daemon destroys both generated devices and restores the
firmware `Xbox Wireless Controller` as the first gamepad. When the controls wake again, it recreates
the Xbox 360 proxy.

### Controller-priority mode

```ini
xbox_proxy=false
```

This retains the original fix without changing the hardware identity. While the controls are awake,
the daemon hides the firmware virtual controller and cycles the unmodified `AIR X Controller` node so
Android assigns it the first gamepad position. When the controls sleep, it restores the firmware
virtual controller.

## Configuration

Edit this file using an Android text editor or by copying it over ADB:

```text
/sdcard/Download/AirXPadFix/config.conf
```

The packaged configuration is:

```ini
# Case-insensitive substrings used to find the two firmware devices.
real_name=AIR X Controller
virtual_name=Xbox Wireless Controller

# true: Xbox 360 uinput proxy; false: original controller-priority mode.
xbox_proxy=true
proxy_name=Microsoft X-Box 360 pad
preserve_back_key=true

# Used in controller-priority mode.
cycle_real=true

settle_ms=250
poll_ms=500
state_dir=/sdcard/Download/AirXPadFix/state
```

After changing any setting, run the top-level `AirXPadFix-restart.sh` through the AIR X root-script
facility. Running `AirXPadFix-start.sh` is not enough when the daemon is already active because start
is intentionally idempotent and will leave the existing process running.

Do not change `state_dir` while the daemon is active. Stop it first so pending recovery metadata stays
at the known location.

## Build

Install Android SDK Platform Tools for `adb` and a side-by-side Android NDK. Android Studio's SDK Manager can install both. The PowerShell build script supports Windows and macOS and selects the newest installed NDK. Set `ANDROID_HOME` or `ANDROID_SDK_ROOT` if the SDK is outside its default location.

On macOS, run the script from PowerShell 7. Gradle, an Android SDK platform, CMake, and Ninja are not required for this build.

From the project directory, run:

```powershell
.\build-root-package.ps1
```

The output is:

```text
build/AirXPadFix-root-arm64-v8a.zip
```

## Install in Downloads

Extract the ZIP directly into the device's standard Download folder. The final layout is:

```text
/sdcard/Download/AirXPadFix-start.sh
/sdcard/Download/AirXPadFix-restart.sh
/sdcard/Download/AirXPadFix-stop.sh
/sdcard/Download/AirXPadFix-status.sh
/sdcard/Download/AirXPadFix-uninstall.sh
/sdcard/Download/AirXPadFix/
  airxpadfixd
  config.conf
  install.sh
  probe.sh
  recover.sh
  start.sh
  status.sh
  stop.sh
  uninstall.sh
```

The short top-level scripts exist because the AIR X root-script picker can see scripts in Downloads
but may not expose scripts inside subdirectories. Shared storage is mounted `noexec`, so scripts must
be run through the root-script facility or passed explicitly to `sh`; executable permission bits on
the Downloads copies are neither retained nor required.

Run `AirXPadFix-start.sh` using the AIR X root-script facility. Internally, it:

- restores any node left hidden by an earlier crash;
- copies `airxpadfixd` to the executable temporary directory `/dev/airxpadfix`;
- starts the copied daemon as root;
- records its PID and appends output to the persistent log.

If using an already-rooted interactive shell instead of the AIR X picker, the equivalent command is:

```sh
sh /sdcard/Download/AirXPadFix/start.sh
```

The included `install.sh` validates and copies the package when it was staged somewhere other than
`/sdcard/Download/AirXPadFix`. It is optional when the ZIP was extracted directly into Downloads.

## Deploy over wireless ADB

This pairing flow requires Android 11 or newer and the AIR X and host on the same Wi-Fi network. On the AIR X, enable Wireless debugging and open **Pair device with pairing code**. Pair from the host using the address and pairing port shown on that screen, then connect using the separate ADB port shown on the Wireless debugging page:

```sh
adb pair <air-x-ip>:<pairing-port>
adb connect <air-x-ip>:<adb-port>
adb devices -l
adb -s <device-serial> push build/AirXPadFix-root-arm64-v8a.zip /sdcard/Download/
```

Extract the ZIP in Downloads, then run `AirXPadFix-start.sh` from the AIR X root-script facility. ADB shell is unprivileged on this firmware, so it cannot run the root scripts.

## Start automatically after reboot

The runtime copy under `/dev` is removed by a reboot, so the daemon must be started again after every
boot. Configure the AIR X root-script/startup facility to run the top-level `AirXPadFix-start.sh` at
boot. It is safe to invoke more than once: if the daemon is already active, it reports the existing PID
and does not start a second process.

## Upgrade or apply configuration changes

Replace the files in Downloads with the new package, then run `AirXPadFix-restart.sh` as root. Restart
performs a graceful stop, restores the stock nodes, copies the new binary into `/dev`, and starts it
with the current `config.conf`.

If replacing the entire `AirXPadFix` directory, preserve a customized `config.conf` or reapply its
settings before restarting.

## Check status and logs

Run `AirXPadFix-status.sh` through the root-script facility. It reports the PID, current mode, hidden
nodes, and recent log entries. The persistent files are:

```text
/sdcard/Download/AirXPadFix/state/airxpadfix.pid
/sdcard/Download/AirXPadFix/state/mode
/sdcard/Download/AirXPadFix/state/airxpadfix.log
```

Expected modes are:

- `proxy`: the awake controls are being forwarded through the Xbox 360 proxy;
- `real`: controller-priority mode has activated the physical AIR X controller;
- `virtual`: the physical controls are asleep and the firmware virtual controller is active;
- `stopped`: the daemon restored stock behavior and exited.

In `proxy` mode, `real-node.state` and `virtual-node.state` are expected. They contain the metadata
needed to recreate the hidden firmware nodes; their presence is not an error.

## Stop and restore stock behavior

Run `AirXPadFix-stop.sh` through the root-script facility, or from a root shell:

```sh
sh /sdcard/Download/AirXPadFix/stop.sh
```

The daemon destroys its generated devices, releases the physical controller, and restores the stock nodes before exiting. If graceful shutdown exceeds ten seconds, the script force-stops the daemon and waits up to ten more seconds for it to exit before running `recover.sh`. It leaves recovery files in place and stops if the daemon still appears active.

Stopping is also the immediate rollback if a game behaves badly with either operating mode.

## Clean uninstall

First disable the automatic `AirXPadFix-start.sh` entry in the AIR X startup facility. Then run
`AirXPadFix-uninstall.sh` as root.

The uninstaller:

1. stops the daemon;
2. refuses to continue if the original controller nodes cannot be safely restored;
3. removes `/dev/airxpadfix`;
4. removes `/sdcard/Download/AirXPadFix` and all five top-level launchers.

After a successful uninstall, controller handling is back to the stock firmware behavior.

## Probe and troubleshooting

The packaged names are already verified for the tested AIR X firmware. If another firmware revision
uses different names, run `probe.sh` once while the controls are awake and once while they are asleep,
then adjust `real_name` and `virtual_name` to unique, case-insensitive substrings.

From ADB, the read-only probe can normally be captured with:

```sh
adb shell sh /sdcard/Download/AirXPadFix/probe.sh > probe-awake.txt
```

Repeat it after the controls sleep and save the output as `probe-asleep.txt`. The AIR X root-script
facility can also run the probe when additional permissions are required; this firmware does not
provide a conventional `su` command to the ADB shell.

Useful checks for Xbox proxy mode are:

```sh
adb shell getevent -lp
adb shell dumpsys input
```

The awake state should list `Microsoft X-Box 360 pad` with vendor `045e`, product `028e`, the stock
`Vendor_045e_Product_028e.kl`, and Android controller number 1. The original `AIR X Controller` and
firmware `Xbox Wireless Controller` should not be exposed under `/dev/input` at the same time.

## Safety and limitations

- All device-node removals are limited to validated `/dev/input/eventN` paths with Linux input major
  number 13.
- Recovery metadata is written before every removal and can be consumed independently by
  `recover.sh` after a crash.
- Closing the daemon automatically destroys its `uinput` devices; stop/recovery then restores the
  original firmware nodes with their device numbers, permissions, ownership, and SELinux labels.
- The Xbox proxy changes software-visible identity and input mapping only. It does not add rumble or
  other output capabilities that the hardware path does not provide.
- Games that permanently cache a disconnected controller may need to be restarted after the physical
  controls sleep or wake.
- The solution depends on root access to the physical event node, `/dev/uinput`, and `/dev/input`.
  Firmware or SELinux-policy changes may require another probe and adjustment.
