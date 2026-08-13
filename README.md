# Battery Current Monitor — Revision A

Modular firmware for a wireless battery current monitor using a Seeed Studio
XIAO ESP32-C3, INA228 and SSD1309 128x64 OLED.

## Current architecture

```text
INA228
  |
  v
Ina228Sensor
  |
  v
TelemetryStore  <---- physical session reset button (min/max + Ah/Wh)
  |
  +--> OledDisplay <---- physical display on/off button
  +--> BleTelemetryService
  +--> WebDashboard / JSON API
  +--> Serial diagnostics
```

`src/main.cpp` is intentionally tiny. `BatteryMonitorApp` coordinates the
subsystems; sensor, display, BLE, web and statistics code are separated.

## Implemented features

- Direct INA228 register reads over I2C
- Explicit INA228 conversion configuration with boot-time readback verification
- Current prototype R015 / 15 mOhm shunt support
- Versioned NVS calibration profile (resistance, zero offset and gain)
- Voltage, current, power and INA228 die temperature
- Shunt-voltage telemetry
- Timestamped, self-contained measurement snapshots with complete/incomplete
  sample health counters
- Session Ah / Wh accounting: positive current is discharge; totals reset on
  power cycle or from the web UI
- Min/max tracking for all measured/calculated metrics
- SSD1309 OLED
- BLE characteristics with human-readable descriptors
- Combined BLE telemetry
- Wi-Fi SoftAP and live dashboard
- `/api/telemetry` JSON endpoint
- Web min/max reset
- Physical reset pushbutton: session reset (min/max + Ah/Wh) or ESP32 restart
- Physical OLED power toggle pushbutton

## Wiring

### I2C

| XIAO ESP32-C3 | Function |
|---|---|
| GPIO6 / D4 | SDA |
| GPIO7 / D5 | SCL |
| 3V3 | INA228 VS + OLED VDD |
| GND | INA228 GND + OLED GND |

Current prototype addresses:

- INA228: `0x40`
- SSD1309: `0x3C`

### Pushbuttons

Both buttons use `INPUT_PULLUP`, so no external pull-up resistor is required.
Use normally-open momentary buttons.

```text
GPIO3 ---- pushbutton ---- GND    Session reset / ESP32 restart
GPIO4 ---- pushbutton ---- GND    OLED page / power
```

Briefly press the reset button to reset all session values (min/max + Ah/Wh).
Hold it for one second to restart the ESP32; a long press does not also reset
the session values first.

Briefly press the display button to switch between the live/session and
min/max pages. Hold it for one second to call the SSD1309/U8g2 power-save
function instead.
Measurement, BLE and Wi-Fi continue operating while the OLED is off.

Pins are centralized in `include/AppConfig.h` and can be changed there.

## Wi-Fi

Default access point:

```text
SSID:     BatteryMonitor
Password: Battery123
```

Open the IP printed in the serial monitor; the default ESP32 SoftAP address is
normally `192.168.4.1`.

The dashboard shows live, minimum and maximum values for:

- voltage
- current
- power
- INA228 temperature
- shunt voltage

The dashboard also shows BLE, I2C, Wi-Fi-client and display status.

## BLE

Device name:

```text
BatteryMonitor
```

The existing custom service retains the individual Voltage, Current, Power,
Temperature and Status characteristics plus the Combined Telemetry
characteristic. It also exposes a compact, notify-capable Binary Telemetry v1
characteristic for the Flutter app. Its fixed 20-byte format works without MTU
negotiation; the complete contract is in
[docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md).

## Measurement configuration and calibration

At boot, firmware explicitly configures the INA228 for continuous bus, shunt
and temperature conversion, 16-sample averaging, 1.052 ms conversion time and
the wide +/-163.84 mV shunt range. The register values are read back and shown
in the dashboard's Sensor details panel and `/api/telemetry` response.

The first-boot calibration default is the INA228 breakout's onboard `R015`
shunt:

```cpp
constexpr float SHUNT_RESISTANCE_OHMS = 0.015f;
```

The calibration module is deliberately separate from measurement acquisition.
It validates and stores shunt resistance, shunt-voltage offset and current gain
in NVS; invalid or absent settings safely use the compile-time default. The
dashboard's Guided calibration card captures zero-current and reference samples,
calculates the gain, and then requires an explicit save. Saving or restoring a
profile starts a new min/max and Ah/Wh session.

For the planned 100 A / 50 mV Kelvin shunt, the nominal resistance will be:

```cpp
0.050f / 100.0f == 0.0005f
```

See [docs/SHUNT_COMMISSIONING.md](docs/SHUNT_COMMISSIONING.md) before moving
to the new shunt.

## Build

Open the directory in VS Code with your PlatformIO/PIOArduino environment and
build/upload the `xiao_esp32c3` environment.

Only U8g2 is declared as an external library. ESP32 BLE, Wi-Fi and WebServer
come from the Arduino-ESP32 framework used by the board environment.

## Roadmap and future placeholders

See [docs/ROADMAP.md](docs/ROADMAP.md) for the implementation order, design
decisions and acceptance criteria. `include/future/` contains planning markers
only; those headers are not runtime APIs yet. The planned work covers:

- broader persistent settings beyond the current calibration profile
- telemetry history / CSV
- DS18B20 shunt temperature
- OTA updates

## PIOArduino / Arduino-ESP32 3.x compatibility

The BLE layer relies on NimBLE's automatic Client Characteristic Configuration Descriptor (CCCD/0x2902) creation for notify characteristics; it does not manually add `BLE2902`. JSON float formatting also uses explicit argument types to avoid overload ambiguity in current Arduino-ESP32 `String` constructors.


## Flash partition layout

The project uses the Arduino-ESP32 `min_spiffs.csv` 4 MB partition scheme. This provides two larger OTA-capable application slots (about 1.9 MB each), which is required because Wi-Fi + BLE + the web dashboard exceed the XIAO ESP32-C3 default 1.25 MB OTA application slot. It retains OTA capability for the planned OTA update feature.

If changing partition schemes on a board that already has firmware installed, perform a full flash erase once before uploading the new build if the board does not boot after the first upload.
