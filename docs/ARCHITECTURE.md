# Architecture

## Design rules

- Hardware acquisition, domain state and presentation transports have distinct owners.
- `TelemetryStore` is the only source of instantaneous measurements and extrema for all consumers.
- A polling pass creates a fresh snapshot; a failed field is invalid, never a stale value from a prior pass.
- Time-sensitive domain features receive application-stamped snapshots rather than reading hardware or wall-clock time themselves.
- `main.cpp` only hands control to `BatteryMonitorApp`.

## Runtime flow

```text
                   poll + timestamp + sequence
  INA228 ───────> Ina228Sensor ────────────────> TelemetryStore
                                                      │
                         ┌────────────────────────────┼───────────────────────────┐
                         ▼                            ▼                           ▼
                    OledDisplay              BleTelemetryService             WebDashboard
                                                                               JSON / browser

 buttons ───────────────────────────────────────────> App (display toggle / extrema reset)
```

`BatteryMonitorApp` owns the scheduler and subsystem instances. It assigns the sequence number and `millis()` timestamp after every sensor polling attempt, then supplies the snapshot to `TelemetryStore`, `EnergyAccumulator`, `AlarmMonitor` and `StateOfChargeEstimator`. This establishes one ordering and time base for extrema, energy integration, alarm evaluation and coulomb counting.

## Telemetry contract

`Telemetry` contains instantaneous values, per-field validity flags, a sample `sequence`, and `sampledAtMs`.

- `voltageOK`, `shuntOK` and `temperatureOK` describe their respective fields.
- `powerOK()` means voltage and current/power are valid; it does not require a valid die-temperature reading.
- `sensorOK()` requires all three hardware readings and is suitable for an overall sensor-health indicator.
- `sampledAtMs` is a wrapping `millis()` value. Consumers compute elapsed time with unsigned subtraction, not signed comparisons or wall-clock time.

`Ina228Sensor::read()` clears the output snapshot before attempting reads. This prevents an intermittent I2C failure from being misrepresented as a new valid value. It separately counts complete/incomplete samples and successful/failed low-level register transactions.

## Component responsibilities

### `BatteryMonitorApp`

Starts components in dependency order, performs non-blocking periodic work, handles buttons, and is the sole composition root. It may depend on all components; leaf components must not depend on it.

### `Ina228Sensor`

Owns direct INA228 register access, decoding, retry behavior and identity checking. It creates `Telemetry` field values only; it does not update stats, timestamps, BLE, HTTP, display or persistence.

At startup it writes and reads back the explicit ADC configuration. Its raw
shunt-voltage decoding always matches the configured `ADCRANGE`; conversion to
current uses the calibration profile passed by the application.

### `CalibrationSettings`

Owns one versioned, validated NVS calibration profile: nominal shunt
resistance, zero-current shunt-voltage offset and a multiplicative current
gain. It defaults safely to `AppConfig` when storage is absent, corrupt or out
of range. It has no I2C, display, BLE or HTTP dependency. The dashboard
submits validated requests, but `BatteryMonitorApp` is the only component that
saves or clears a profile, updates `Ina228Sensor`, and starts a fresh session.
This preserves a single defined boundary between calibration profiles.

### `AlarmSettings` and `AlarmMonitor`

`AlarmSettings` owns one versioned, validated NVS alarm profile: low and high
voltage, absolute current, die temperature and sensor-health limits.
`AlarmMonitor` evaluates the latest valid sample against that profile and
publishes alarm state with the shared telemetry. Web and BLE requests are
validated and applied by `BatteryMonitorApp`, which persists settings before
returning a control result. Alarms never perform a hardware read or maintain a
second sample cache.

### `BatteryProfile` and `StateOfChargeEstimator`

`BatteryProfile` owns one versioned NVS profile (rated capacity, charged
voltage) analogous to `CalibrationSettings`/`AlarmSettings`. `StateOfChargeEstimator`
coulomb-counts remaining amp-hours against that profile — unlike
`EnergyAccumulator`'s per-power-on-session Ah/Wh (which intentionally resets
every boot), this must survive reboots to be a useful fuel gauge, so it
persists its running state to its own NVS namespace periodically (not on
every sample, to bound flash writes) and has no notion of "correct" SoC until
a full-charge sync happens — automatically (sustained voltage at or above the
charged voltage with a tapering current) or manually from Web/BLE. Web and
BLE profile-save/sync requests are validated and applied by
`BatteryMonitorApp`, matching the calibration/alarm boundary.

### `TelemetryStore`

Owns the latest snapshot and extrema. It updates each statistic only with a finite valid value. The web min/max action calls `resetExtrema()` directly; the physical reset calls the application-level session reset, which includes extrema and Ah/Wh totals.

### Presentation and transport components

`OledDisplay`, `BleTelemetryService` and `WebDashboard` consume stored state only. `WebDashboard` owns the always-on recovery SoftAP, optional home-network station association, mDNS, HTTP and JSON serialization. `WifiSettings` keeps station credentials in its own NVS namespace and never exposes the password in telemetry. `BleTelemetryService` owns GATT, advertising state and the documented binary mobile-app telemetry contract. BLE control writes are queued for the application loop and return a request-ID-matched result on `control1`. Neither transport performs I2C work or retains a competing measurement cache.

### `FirmwareUpdateService`

Owns the ESP32 inactive-partition writer for the BLE transfer protocol. It only
accepts strictly ordered chunks and verifies byte count, CRC-32 and the ESP32
image before asking `BatteryMonitorApp` to restart. BLE owns the GATT callback;
the application owns the actual reboot. The Web Dashboard keeps its HTTP upload
adapter, but both routes claim the same update writer lock, so an active route
causes the other to fail safely. A verified BLE status remains available for a
short grace period before the application restarts.

### `DebouncedButton`

Provides active-low input with debounced short-press and optional long-press
events. The reset button uses a short press for the application
session reset and a one-second hold for an ESP32 restart. The display button
uses a short press for page selection and a one-second hold for OLED power; the
button component has no knowledge of those actions.

## Planned extension seams

The headers under `include/future/` are planning markers, not active interfaces or runtime dependencies. `EnergyAccumulator` has graduated from this area into the active `include/energy/` module.

- `SettingsStore` owns validated, versioned NVS configuration.
- `TelemetryHistory` consumes snapshots through a bounded buffer/decimation policy.
- `Ds18b20Sensor` is an optional, independent temperature source.
- Signed/authenticated OTA policy is a future hardening layer over the active
  `FirmwareUpdateService` and Web upload paths.

The ordering and acceptance criteria live in [ROADMAP.md](ROADMAP.md).
