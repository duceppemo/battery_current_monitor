# Architecture

## Design goals

- Keep hardware drivers independent from presentation/network layers.
- Maintain one shared telemetry source of truth.
- Make min/max/session statistics reusable by OLED, BLE, web and future logging.
- Keep `main.cpp` nearly empty.
- Make future features additive rather than requiring another monolithic rewrite.

## Runtime flow

```text
                  +-------------------+
                  |   Ina228Sensor    |
                  +---------+---------+
                            |
                            v
                  +-------------------+
                  |  TelemetryStore   |
                  | current + min/max |
                  +---------+---------+
                            |
          +-----------------+-----------------+
          |                 |                 |
          v                 v                 v
   +-------------+   +-------------+   +-------------+
   | OledDisplay |   | BLE Service |   | WebDashboard|
   +-------------+   +-------------+   +-------------+
          ^                                   |
          |                                   v
 Display toggle                         JSON / browser
    pushbutton

 Min/max reset pushbutton ---> TelemetryStore::resetExtrema()
```

## Component responsibilities

### `BatteryMonitorApp`
Owns the subsystem objects, starts them in the correct order and schedules
periodic work. It is the only component that deliberately knows about almost
every subsystem.

### `Ina228Sensor`
Owns INA228 register access, decoding, retries, identification and I2C read
counters. It produces a `Telemetry` sample and has no knowledge of BLE, OLED or
HTTP.

### `TelemetryStore`
Stores the newest `Telemetry` sample and tracks min/max values for each metric.
Both the web UI and physical reset button call the same `resetExtrema()` API.

### `OledDisplay`
Owns all SSD1309/U8g2 rendering and OLED power state. Turning the display off
does not stop measurements or networking.

### `BleTelemetryService`
Owns GATT setup, characteristics, notifications, connection state and
re-advertising.

### `WebDashboard`
Owns SoftAP startup, HTTP routes, JSON serialization and the embedded dashboard.
It reads from `TelemetryStore`; it does not read the INA228 directly.

### `DebouncedButton`
Reusable active-low momentary-button input. The current application instantiates
one button for min/max reset and one for OLED on/off.

## Future boundaries

The placeholders in `include/future/` are intentionally not coupled into the
runtime yet. Planned additions include:

- `EnergyAccumulator`: Ah / Wh and signed charge integration
- `SettingsStore`: NVS calibration and user settings
- `TelemetryHistory`: rolling history, graphs and CSV
- `Ds18b20Sensor`: physical shunt temperature
- `OtaService`: firmware update workflow
