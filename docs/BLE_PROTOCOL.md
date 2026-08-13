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

It supports `Read` and `Notify`. Firmware updates it after each completed
measurement pass (currently every 500 ms). It is a fixed 20-byte little-endian
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
little-endian page. Firmware rotates through the pages once per BLE update, so
the app has a complete dashboard within roughly three seconds after connecting.
The live Binary Telemetry v1 characteristic remains the authoritative fast
live-data stream.

| Type byte | Page | Payload |
| --- | --- | --- |
| `0x11` | extrema | `u16` voltage min/max in mV at 1/3; `int24` current min/max in mA at 5/8; `int24` power min/max in mW at 11/14; `int8` temperature min/max in C at 17/18; validity flags at 19 use the Binary Telemetry v1 flag bits. |
| `0x12` | directional energy | `int32` discharged Ah, charged Ah, discharged Wh and charged Wh at offsets 1, 5, 9 and 13, each scaled to milli-units. |
| `0x13` | monitor state | flags at 1: sensor OK, display on, INA configured, readback valid, wide shunt range, Wi-Fi AP ready, stored calibration. `u16` sequence at 2; `u32` uptime seconds, successful samples and failed samples at 4/8/12; Wi-Fi client count at 16; reset-reason code at 17; conversion time in us at 18. |
| `0x14` | calibration | stored flag at 1; `u32` shunt resistance in micro-ohms at 2; `int32` offset in nanovolts at 6; `int32` gain in ppm at 10; current shunt voltage in nanovolts at 14; shunt-valid flag at 18. |
| `0x15` | shunt/config | `int32` shunt min/max in nanovolts at 1/5; INA CONFIG/ADC_CONFIG at 9/11; averages/conversion time at 13/15; temperature min/max in C at 17/18; bit 0 shunt extrema valid and bit 1 temperature extrema valid at 19. |

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
