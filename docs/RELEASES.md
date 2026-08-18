# Firmware release and update process

## Release asset

An OTA release contains the PlatformIO application image only:

```text
.pio/build/xiao_esp32c3/firmware.bin
```

The factory image combines bootloader, partitions and application at fixed
flash offsets. It is for a USB/serial initial installation, not for either OTA
route. The release workflow publishes the OTA asset as
`battery-monitor-<version>.bin`, a detached signature
`battery-monitor-<version>.sig`, and the factory image separately.

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
ESP32 image before booting the new slot; on BLE, as of firmware 0.5.16, it
also requires a valid ECDSA-P256 signature over the image's SHA-256 digest,
checked against a public key embedded in firmware. The matching private key
exists only as this repository's `FIRMWARE_SIGNING_KEY` GitHub Actions
secret, used solely by the release workflow to sign each published `.bin` as
`battery-monitor-<version>.sig` (raw `r||s`, 64 bytes, not DER). An attacker
reachable over BLE can no longer push an arbitrary image through that path,
only one signed by the project's release key.

The Web Dashboard upload path is intentionally unaffected: it stays
CRC/format-checked only, matching its role as a secondary/recovery path that
already requires reaching the recovery AP or home network. Install assets
only from the project's GitHub Releases either way.

Signature verification runs on the main loop rather than inside the BLE GATT
write callback: mbedTLS's ECDSA math is too stack-heavy for the Bluetooth
controller task and crashes it (`BTC_TASK` stack overflow) if run there
directly. `FirmwareUpdateService::finish()` only validates size/CRC-32 and
moves to a `Verifying` state; `processPendingVerification()`, polled every
main-loop iteration, does the actual signature check and `Update.end()`. See
`docs/BLE_PROTOCOL.md` for the wire-level detail.

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
5. The GitHub Action builds both images, signs the OTA `.bin` and creates a
   GitHub Release. Verify the OTA `.bin` asset name/size and that a matching
   `.sig` asset was published before offering it in the Flutter app.
6. Test the new asset using the Web Dashboard first, then the BLE app. Confirm
   that the selected image version, verified result and post-restart version
   all agree. Keep a known-good USB upload path until both pass.
