# Battery Current Monitor — Revision A

Modular firmware prototype for a modernized Watt's Up-style current monitor
using a Seeed Studio XIAO ESP32-C3, INA228 and SSD1309 128x64 OLED.

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
- Current prototype R015 / 15 mOhm shunt support
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
- Physical session reset pushbutton (min/max + Ah/Wh)
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
GPIO3 ---- pushbutton ---- GND    Reset session values (min/max + Ah/Wh)
GPIO4 ---- pushbutton ---- GND    OLED on/off
```

The display button calls the SSD1309/U8g2 power-save function. Measurement,
BLE and Wi-Fi continue operating while the OLED is off.

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
characteristic.

## Important prototype setting

The bundle is configured for the INA228 breakout's onboard `R015` shunt:

```cpp
constexpr float SHUNT_RESISTANCE_OHMS = 0.015f;
```

When the breakout shunt is removed and the INA228 is connected to the original
Watt's Up 1 mOhm Kelvin shunt, change that value in `include/AppConfig.h` to:

```cpp
constexpr float SHUNT_RESISTANCE_OHMS = 0.001f;
```

## Build

Open the directory in VS Code with your PlatformIO/PIOArduino environment and
build/upload the `xiao_esp32c3` environment.

Only U8g2 is declared as an external library. ESP32 BLE, Wi-Fi and WebServer
come from the Arduino-ESP32 framework used by the board environment.

## Roadmap and future placeholders

See [docs/ROADMAP.md](docs/ROADMAP.md) for the implementation order, design
decisions and acceptance criteria. `include/future/` contains planning markers
only; those headers are not runtime APIs yet. The planned work covers:

- persistent settings and calibration
- telemetry history / CSV
- DS18B20 shunt temperature
- OTA updates

## PIOArduino / Arduino-ESP32 3.x compatibility

The BLE layer relies on NimBLE's automatic Client Characteristic Configuration Descriptor (CCCD/0x2902) creation for notify characteristics; it does not manually add `BLE2902`. JSON float formatting also uses explicit argument types to avoid overload ambiguity in current Arduino-ESP32 `String` constructors.


## Flash partition layout

The project uses the Arduino-ESP32 `min_spiffs.csv` 4 MB partition scheme. This provides two larger OTA-capable application slots (about 1.9 MB each), which is required because Wi-Fi + BLE + the web dashboard exceed the XIAO ESP32-C3 default 1.25 MB OTA application slot. It retains OTA capability for the planned OTA update feature.

If changing partition schemes on a board that already has firmware installed, perform a full flash erase once before uploading the new build if the board does not boot after the first upload.
