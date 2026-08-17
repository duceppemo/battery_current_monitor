# Battery Current Monitor Roadmap

This roadmap treats measurement trust as a dependency of every feature that records, displays, or persists a value. Work is ordered so energy counters do not become a second source of truth or integrate stale/partial samples.

## Current baseline (complete)

The firmware has one polling owner (`BatteryMonitorApp`), one measurement source (`Ina228Sensor`) and one shared state owner (`TelemetryStore`). OLED, BLE, HTTP and serial diagnostics only consume that state.

Each poll produces a self-contained `Telemetry` snapshot. It includes a monotonic sample sequence and completion timestamp; failed registers remain invalid rather than retaining an old value. Public health counters refer to complete versus incomplete *samples*, while serial diagnostics also expose low-level register transaction counts.

### Baseline acceptance checklist

- [x] Voltage, shunt voltage, current, power and die temperature have explicit validity flags.
- [x] Min/max values are updated only from finite, valid values and reset from the current valid sample.
- [x] A failed read cannot reuse a previous measurement as a new one.
- [x] All outputs read the same stored snapshot; no UI/network layer reads I2C.
- [x] The JSON API includes sample sequence, age, and complete/incomplete sample counters.
- [ ] On-hardware validation: verify polarity, accuracy, zero offset and the 15 mOhm shunt value against trusted instruments at the intended operating range.

## Phase 1 — Ah / Wh session counters (implemented; validation pending)

**Goal:** add explicit session accounting without changing the meaning of the existing instantaneous telemetry or min/max reset.

The current implementation uses firmware-side trapezoidal integration. Positive
current/power means **battery discharge**. Counters are intentionally
session-only and reset after a power cycle. The Web Dashboard and Flutter app
can reset energy without resetting extrema; the single physical reset button
clears both energy and extrema, with a dedicated application hook for future
session stores. Signed
(`netAh`, `netWh`) and directional charge/discharge totals are available from
JSON and BLE.

### Design decisions to lock before coding

1. [x] Use application-assigned timestamps and trapezoidal integration between consecutive valid samples. It skips incomplete samples, unexpected timing gaps and boot.
2. [x] Keep signed totals (`netAh`, `netWh`) and directional totals (`chargedAh`/`dischargedAh`, `chargedWh`/`dischargedWh`). Positive is discharge.
3. [x] Keep session reset separate from min/max reset. A reset establishes a new integration origin and cannot double-count the next interval.
4. [x] Start with firmware integration from calibrated instantaneous values. Add INA228 native charge/energy registers only as a documented cross-check after ADC configuration and calibration are fixed.

### Module boundary

`EnergyAccumulator` is a pure domain component owned by `BatteryMonitorApp`. It receives the newest `Telemetry` snapshot and returns an `EnergyTotals` value. `TelemetryStore` remains responsible for the latest measurement and extrema only; it does not learn integration policy or storage.

**Done when:** controlled constant-current and constant-power tests agree with an external reference within a documented tolerance; reversed current updates signed and directional totals correctly; missing samples do not create a large integration step; OLED, BLE and JSON show identical totals.

## Phase 2 — Configuration and measurement quality

1. [x] Add an explicit INA228 configuration module: continuous conversion, 16-sample averaging, 1.052 ms conversion time and wide `ADCRANGE` are written, read back and reported.
2. [x] Add NVS-backed settings with schema versioning, defaults and validation for shunt resistance, current offset and gain calibration, plus the monitor alarm profile. The guided dashboard flow saves and restores calibration explicitly.
3. [x] Add a guided zero-current/reference-current calibration flow. It rejects invalid resistance, offset and gain ranges; the operator must still record test conditions with the reference instrument.
4. Define filtering separately from raw measurement acquisition. Preserve raw samples for diagnostics; use filtered values only where explicitly chosen.

**Done when:** settings survive reset, invalid settings fall back safely, and the diagnostics endpoint shows active configuration and calibration version.

## Phase 3 — Physical shunt and thermal safety

1. Move from the breakout's R015 shunt to the planned 100 A / 50 mV (0.5 mOhm) Kelvin shunt only after confirming wiring, polarity and safe common-mode conditions.
2. Update the configured nominal resistance and repeat the Phase 1 validation.
3. [x] Add persistent monitor alarms for low/high voltage, absolute current, die temperature and sensor-health status.
4. Add an optional DS18B20 shunt-temperature driver and independent shunt thermal warning/alarm policy.

**Done when:** electrical and thermal readings are validated under load, and a missing optional temperature sensor degrades only its own feature.

## Phase 4 — History, persistence and networking

1. [x] Define and implement a fixed-size, versioned Binary Telemetry v1 BLE characteristic for a mobile app. Keep the existing text characteristics for diagnostics and generic BLE tools.
2. [x] Build the Flutter companion app: service-filtered scan, connection lifecycle, binary-telemetry decoding, controls, calibration, alarms and a live dashboard.
3. [x] Add an app-local bounded session history buffer with an explicit 7,200-entry retention policy and trend views.
4. [x] Add user-approved CSV export from the app-local session log. Consider bounded LittleFS persistence and wear limits only as a separate device-side feature.
5. Make session-counter persistence opt-in and crash-safe.
6. [x] Add configurable Wi-Fi station/AP modes, credentials and mDNS. Keep the `BatteryMonitor` AP as a concurrent recovery path, store credentials in a dedicated NVS namespace and retry station association without blocking measurements.

**Done when:** memory limits are documented, exports carry timestamps and units, and no network operation blocks measurement polling.

## Phase 5 — Serviceability

1. [x] Add build metadata and diagnostics: firmware version, hardware revision and advertised BLE capabilities are exposed to Web and BLE clients.
2. [x] Add local Web Dashboard `.bin` upload and a BLE transfer path with
   sequential offsets, CRC-32 and ESP32 image validation. Keep the Web path as
   recovery while the app downloads release assets before joining the monitor.
3. Add signed/authenticated OTA with a documented recovery path. CRC-32 is an
   integrity check, not an authenticity guarantee.
4. [x] Add a release checklist covering build, flash, I2C discovery, calibration, web/BLE compatibility and OTA rollback.

## Non-goals until earlier phases pass

- Persisting energy counters before their accuracy and reset semantics are proven.
- Adding graphs/logging that create a second telemetry path.
- Using placeholder headers as runtime APIs before their data contracts are agreed.
