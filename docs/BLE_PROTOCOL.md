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
