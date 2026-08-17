# BLE protocol

The monitor is a BLE GATT peripheral named `BatteryMonitor`. It exposes the
custom service:

```text
7d9f0000-9c65-4d3d-bdd5-8f4c6b2e1000
```

The existing readable text characteristics remain supported for diagnostics and
generic tools such as nRF Connect. The mobile app should subscribe to the
Binary Telemetry characteristic instead:

```text
7d9f0009-9c65-4d3d-bdd5-8f4c6b2e1000
```

It supports `Read` and `Notify`. Firmware publishes it on the one-second BLE
cadence after completed measurements. It is a fixed 20-byte little-endian
packet, deliberately small enough to fit in the initial BLE ATT notification
payload. The app therefore must not rely on MTU negotiation for live telemetry.

## Binary Telemetry v1

| Offset | Size | Type | Field | Meaning |
| --- | ---: | --- | --- | --- |
| 0 | 1 | `uint8` | version and flags | High nibble is protocol version (`1`). Low nibble: bit 0 voltage valid, bit 1 current valid, bit 2 power valid, bit 3 temperature valid. Ignore other bits. |
| 1 | 2 | `uint16` | sequence | Little-endian measurement sequence, modulo 65,536. It increases for each completed sample. |
| 3 | 2 | `uint16` | voltage | Millivolts. Valid only when the voltage flag is set. |
| 5 | 3 | `int24` | current | Signed milliamps. Positive is battery discharge. Valid only when the current flag is set. |
| 8 | 3 | `int24` | power | Signed milliwatts. Positive is battery discharge. Valid only when the power flag is set. |
| 11 | 1 | `int8` | temperature | Degrees Celsius, rounded to the nearest whole degree. Valid only when the temperature flag is set. |
| 12 | 4 | `int32` | net charge | Signed milliamp-hours for this power-on session. Positive is discharge. |
| 16 | 4 | `int32` | net energy | Signed milliwatt-hours for this power-on session. Positive is discharge. |

For invalid live fields the packet transmits zero and clears the matching flag;
the app must use the flag rather than treating zero as an error. Energy fields
are saturated only if their signed 32-bit scaled range is exceeded.

### Flutter decoding notes

Use a `ByteData` view with `Endian.little`. A signed 24-bit integer is decoded
by reading three bytes, then sign-extending it when bit 23 is set:

```dart
int int24LE(Uint8List bytes, int offset) {
  var value = bytes[offset] | (bytes[offset + 1] << 8) |
      (bytes[offset + 2] << 16);
  if ((value & 0x800000) != 0) value |= ~0xFFFFFF;
  return value;
}
```

Divide voltage, current, power, Ah and Wh by 1,000 for their displayed units.
The app should accept a packet only when it is exactly 20 bytes and the high
nibble at offset 0 is a supported version. It should treat a paused sequence as
stale telemetry and reconnect/refresh its UI as appropriate.

## Compatibility and future versions

This contract is append-only in spirit but fixed-size by design. A future
incompatible packet uses a new characteristic UUID and a new version number;
Binary Telemetry v1 stays available for existing app releases. Do not infer a
device identity from a phone's BLE identifier: iOS provides a privacy-scoped
identifier that can change. A future firmware serial-number characteristic is
the appropriate stable identity.

## Dashboard Data v1

The companion app also subscribes to the Dashboard Data characteristic:

```text
7d9f000a-9c65-4d3d-bdd5-8f4c6b2e1000
```

It supports `Read` and `Notify`. Each notification is one 20-byte
little-endian page. Firmware rotates through seven pages once per one-second
BLE update, so the app has a complete dashboard within roughly seven seconds
after connecting.
The live Binary Telemetry v1 characteristic remains the authoritative fast
live-data stream.

| Type byte | Page | Payload |
| --- | --- | --- |
| `0x11` | extrema | `u16` voltage min/max in mV at 1/3; `int24` current min/max in mA at 5/8; `int24` power min/max in mW at 11/14; `int8` temperature min/max in C at 17/18; validity flags at 19 use the Binary Telemetry v1 flag bits. |
| `0x12` | directional energy | `int32` discharged Ah, charged Ah, discharged Wh and charged Wh at offsets 1, 5, 9 and 13, each scaled to milli-units. |
| `0x13` | monitor state | flags at 1: sensor OK, display on, INA configured, readback valid, wide shunt range, Wi-Fi AP ready, stored calibration. `u16` sequence at 2; `u32` uptime seconds, successful samples and failed samples at 4/8/12; Wi-Fi client count at 16; reset-reason code at 17; conversion time in us at 18. |
| `0x14` | calibration | stored flag at 1; `u32` shunt resistance in micro-ohms at 2; `int32` offset in nanovolts at 6; `int32` gain in ppm at 10; current shunt voltage in nanovolts at 14; shunt-valid flag at 18. |
| `0x15` | shunt/config | `int32` shunt min/max in nanovolts at 1/5; INA CONFIG/ADC_CONFIG at 9/11; averages/conversion time at 13/15; temperature min/max in C at 17/18; bit 0 shunt extrema valid and bit 1 temperature extrema valid at 19. |
| `0x16` | alarms | enabled flags at 1: bit 0 low voltage, bit 1 high voltage, bit 2 current, bit 3 temperature, bit 4 sensor health; active-alarm flags (same bits) at 2; `u16` low/high voltage in mV at 3/5; `int24` max absolute current in mA at 7; `int32` max temperature in deci-C at 10. |
| `0x17` | Wi-Fi station | flags at 1: bit 0 station configured, bit 1 station connected, bit 2 mDNS ready; station IPv4 as four raw octets at 2-5 (all zero when not connected). The recovery AP (`BatteryMonitor` / `192.168.4.1`) is always available regardless of these flags. |

Unknown page types must be ignored. All pages are fixed at 20 bytes; a client
must not rely on an enlarged ATT MTU.

## Dashboard Control v1

The control characteristic is write-with-response:

```text
7d9f000b-9c65-4d3d-bdd5-8f4c6b2e1000
```

The first byte is a command. Commands are applied on the firmware main loop;
the app observes the resulting state on the next dashboard page.

| Command | Payload | Effect |
| ---: | --- | --- |
| `1` | none | Reset min/max extrema. |
| `2` | none | Reset power-on session Ah/Wh totals. |
| `3` | none | Toggle the OLED display. |
| `4` | 12 bytes after command: `u32` resistance micro-ohms, `int32` offset nanovolts, `int32` gain ppm | Validate and store calibration, then reset session values. |
| `5` | none | Restore compile-time default calibration, then reset session values. |
| `6` | flags, low/high voltage in mV, current in mA, temperature in deci-C | Save persistent monitor alarm thresholds. Flags: bit 0 low voltage, bit 1 high voltage, bit 2 absolute current, bit 3 temperature, bit 4 sensor health. |
| `7` | `u8` SSID length (1-32), SSID bytes, `u8` password length (0-64, 0 only for an open network), password bytes | Save home Wi-Fi station credentials and start connecting, in parallel with the always-available recovery AP. Rejected if the SSID is empty or the password is 1-7 bytes (below the WPA2 minimum). |
| `8` | none | Forget the stored Wi-Fi station credentials. The recovery AP remains available. |

Every app-originated command appends a `u16` request ID. A command is not
considered successful until its matching Control Result notification reports
that the monitor applied it; a BLE write response only confirms receipt.
Commands `7` and `8` echo the resulting state on the next Wi-Fi station
dashboard page (`0x17`) rather than in the Control Result itself.

Command `7`'s payload can reach 99 bytes, well past the 20-byte notification
size but still comfortably under a negotiated MTU. Request a larger MTU
before writing it; a default 23-byte MTU only guarantees 20 usable bytes and
will fail for anything beyond a very short SSID and password.

### Control Result v1

The companion app subscribes to this read/notify characteristic before writing
a control command:

```text
7d9f000f-9c65-4d3d-bdd5-8f4c6b2e1000
```

Its fixed six-byte packet is: protocol version (`1`), command byte, `u16`
request ID, result (`0` idle, `1` applied, `2` rejected because another command
is pending, `3` failed to persist), and one reserved byte. Older clients may
omit the request ID and receive status with ID zero.

## Device Information

The readable device-information characteristic is:

```text
7d9f000c-9c65-4d3d-bdd5-8f4c6b2e1000
```

Its UTF-8 value is a semicolon-separated diagnostic string, currently
`FW=<firmware-version>;HW=<hardware-revision>;BLE=telemetry1,dashboard1,ota1,control1,wifi1`.
Clients must ignore unknown keys so information can be added without a protocol
break. The app must enable the BLE Wi-Fi settings UI only when this list
includes `wifi1`.

## Firmware Transfer v1

Firmware version 0.5.1 and later exposes a sequential, checksummed BLE update
transport. It is intended for the companion app after it has downloaded a
release asset while the phone still has internet access. The Web Dashboard's
local `.bin` upload remains available as a recovery path.

The app must enable this path only when Device Information includes `ota1`.

### Transfer characteristic

Write-with-response characteristic:

```text
7d9f000d-9c65-4d3d-bdd5-8f4c6b2e1000
```

All multi-byte fields are little-endian. A transfer is strictly sequential;
the monitor rejects duplicate, missing or out-of-order data frames.

| Command | Frame | Meaning |
| ---: | --- | --- |
| `0xA0` | command, `u32` image size, `u32` IEEE CRC-32 | Start a new image. The checksum covers the complete raw `.bin` asset. |
| `0xA1` | command, `u32` offset, 1..N bytes image data | Write one image chunk at exactly the next expected offset. |
| `0xA2` | command only | Finish. The monitor checks size, CRC-32 and ESP32 image verification before scheduling a reboot. |
| `0xA3` | command only | Abort and discard the inactive-partition image. |

The companion app requests an enlarged MTU when the platform allows it, limits
data frames to 180 bytes, and uses write-with-response for every frame. Do not
start another control operation while a transfer is active. Keep the app in
the foreground and keep monitor power connected.

### Firmware Update Status v1

Read/Notify characteristic:

```text
7d9f000e-9c65-4d3d-bdd5-8f4c6b2e1000
```

It is always a 12-byte packet: protocol version at byte 0 (`1`), state at byte
1 (`0` idle, `1` receiving, `2` verified, `3` error), received/expected byte
counts as `u32` at offsets 2/6, error code at byte 10, reserved byte 11. Error
codes are: `1` start, `2` sequence, `3` flash write, `4` CRC-32, `5` final
image verification. A `verified` status is sent, then the monitor waits two
seconds before rebooting so the central can receive it.

CRC-32 detects transfer corruption; it is not a cryptographic signature. Only
install release assets you trust. Signed/authenticated update policy remains a
future hardening task.
