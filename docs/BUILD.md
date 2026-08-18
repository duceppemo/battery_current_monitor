# Build

Open the directory in VS Code with your PlatformIO/PIOArduino environment and
build/upload the `xiao_esp32c3` environment.

Only U8g2, WebSockets and PubSubClient are declared as external libraries.
ESP32 BLE, Wi-Fi and WebServer come from the Arduino-ESP32 framework used by
the board environment.

## PIOArduino / Arduino-ESP32 3.x compatibility

This library never auto-creates a Client Characteristic Configuration
Descriptor (CCCD/`0x2902`) for `NOTIFY` characteristics — only a descriptor
explicitly added via `addDescriptor()` gets registered. `BleTelemetryService`
adds a `BLE2902` descriptor to every notify characteristic for exactly this
reason; without it, a central has nothing to write to enable notifications on
that characteristic. See [ARCHITECTURE.md](ARCHITECTURE.md) for the GATT
handle budget this interacts with. JSON float formatting also uses explicit
argument types to avoid overload ambiguity in current Arduino-ESP32 `String`
constructors.

## Flash partition layout and releases

See [HARDWARE.md](HARDWARE.md#flash-partition-layout) for the partition
scheme, and [RELEASES.md](RELEASES.md) for the release/OTA-update process.
