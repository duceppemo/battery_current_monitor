# Battery Current Monitor firmware instructions

This firmware and the Flutter companion app at
`https://github.com/duceppemo/battery-monitor-app` form one product. For any
BLE protocol, control or OTA change, update and cross-check both repositories.
`docs/BLE_PROTOCOL.md` is the protocol source of truth.

- Preserve Binary Telemetry v1 as a fixed 20-byte packet that is valid without
  MTU negotiation. Firmware publishes binary telemetry and rotating dashboard
  pages on the one-second BLE cadence.
- Keep all XIAO ESP32-C3 pins in `include/AppConfig.h`: I2C is GPIO6 SDA and
  GPIO7 SCL; GPIO3 is session-reset/restart; GPIO4 is OLED page/power.
- Access INA228 (`0x40`) through direct I2C register reads/writes and SSD1309
  (`0x3C`) through U8g2. Do not add I2C reads in presentation transports.
- Keep the `min_spiffs.csv` 4 MB dual-OTA layout and preserve OTA headroom.
  Build with `pio run`; the release toolchain is pinned to `espressif32@7.0.1`.
- Every NOTIFY characteristic needs an explicit `BLE2902` CCCD descriptor
  added via `addDescriptor()`; this library never adds one automatically, and
  without it a central cannot enable notifications on that characteristic.
  When adding or removing a characteristic in `BleTelemetryService.cpp`,
  update `NOTIFY_CHARACTERISTIC_COUNT`/`WRITE_ONLY_CHARACTERISTIC_COUNT`
  (feeding `SERVICE_HANDLE_COUNT`, which replaces `createService`'s default
  15-handle allocation) and add/remove its `verifyCharacteristicRegistered()`
  call after `service->start()` — that check is what turns a wrong handle
  budget into a loud boot-time error instead of a characteristic silently
  missing from the live GATT database. Keep JSON float formatting types
  explicit for Arduino-ESP32 compatibility.
- Apply BLE dashboard commands on the application loop. App-originated
  commands include a request ID and must return an explicit Control Result;
  write success alone is not command success.
- Web and BLE OTA paths share one writer lock. Verify sequential frames,
  CRC-32 and the image before the post-success reboot grace period.
