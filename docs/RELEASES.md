# Firmware release and update process

## Release asset

An OTA release contains the PlatformIO application image only:

```text
.pio/build/xiao_esp32c3/firmware.bin
```

The factory image combines bootloader, partitions and application at fixed
flash offsets. It is for a USB/serial initial installation, not for either OTA
route. The release workflow publishes the OTA asset as
`battery-monitor-<version>.bin` and the factory image separately.

## Install paths

1. **Flutter app (preferred):** use internet/mobile data to download the
   firmware asset, connect to the monitor by BLE, then choose **Install via
   BLE**. The app requests a larger MTU, sends sequential checked frames, and
   waits for the monitor's verified status before it restarts. Firmware 0.5.1
   keeps that successful status available for about two seconds so the app can
   show a conclusive result.
2. **Web Dashboard (recovery/fallback):** join the `BatteryMonitor` AP, open
   `http://192.168.4.1`, choose the downloaded OTA `.bin`, and upload it.
3. **USB/serial (initial provisioning or recovery):** use PlatformIO upload
   with a wired board. A changed partition layout may require an erase first.

Never remove monitor power during either OTA transfer. Keep the Flutter app in
the foreground during a BLE update. After a verified update, wait for the
three-second splash screen and reconnect.

## Integrity and trust

The monitor checks ordered frame offsets, final byte count, CRC-32 and the
ESP32 image before booting the new slot. This catches accidental corruption;
it is not a cryptographic signature or authentication system. Install assets
only from the project's GitHub Releases. Signed image/authenticated OTA is
tracked in the roadmap before the monitor is used in an exposed environment.

Web and BLE updates share one inactive-partition writer lock. Starting an
update through either transport rejects a simultaneous request from the other;
the existing update must finish, fail or be aborted before retrying.

## Publishing a firmware release

1. Update `BATTERY_MONITOR_FIRMWARE_VERSION` in `include/AppConfig.h`. The
   dashboard's OTA image marker is derived from it automatically.
2. Run `pio run` and confirm the output is below the OTA partition limit.
3. Flash and exercise the release candidate over USB; verify I2C, OLED, BLE,
   Wi-Fi Dashboard, alarm controls, acknowledged BLE controls and both reset
   controls.
4. Commit the verified changes, push `main`, then create and push a matching
   tag such as `v0.5.1`.
5. The GitHub Action builds both images and creates a GitHub Release. Verify
   the OTA `.bin` asset name and size before offering it in the Flutter app.
6. Test the new asset using the Web Dashboard first, then the BLE app. Confirm
   that the selected image version, verified result and post-restart version
   all agree. Keep a known-good USB upload path until both pass.
