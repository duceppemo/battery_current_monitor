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
5. [x] Make session-counter persistence opt-in and crash-safe:
   `EnergyPersistenceSettings` (disabled by default) plus periodic,
   schema-guarded NVS persistence in `EnergyAccumulator`, on the Web
   Dashboard's Session energy card and over BLE (dashboard page `0x12` byte
   17, control command `16`). A reset force-persists immediately so a crash
   before the next periodic tick can't resurrect pre-reset totals; enabling
   the setting never restores an old persisted value over live totals.
6. [x] Add configurable Wi-Fi station/AP modes, credentials and mDNS. Keep the `BatteryMonitor` AP as a concurrent recovery path, store credentials in a dedicated NVS namespace and retry station association without blocking measurements.
7. [x] Let the BLE app set and clear home Wi-Fi station credentials too, so a phone never has to leave its own network to join the monitor's recovery AP just to configure it. The Web Dashboard path stays available as an alternative.

**Done when:** memory limits are documented, exports carry timestamps and units, and no network operation blocks measurement polling.

## Phase 5 — Serviceability

1. [x] Add build metadata and diagnostics: firmware version, hardware revision and advertised BLE capabilities are exposed to Web and BLE clients. Device Information also includes a stable per-chip `ID` (from `ESP.getEfuseMac()`) so a BLE client can recognize "the same monitor" across reconnects independent of any OS-assigned peripheral address — notably useful on iOS, where that address is a privacy-scoped identifier that can change over time for the same physical device.
2. [x] Add local Web Dashboard `.bin` upload and a BLE transfer path with
   sequential offsets, CRC-32 and ESP32 image validation. Keep the Web path as
   recovery while the app downloads release assets before joining the monitor.
3. Add signed/authenticated OTA with a documented recovery path. CRC-32 is an
   integrity check, not an authenticity guarantee. Firmware-side done as of
   0.5.16: BLE transfers require an ECDSA-P256 signature (raw `r||s`) over
   the image's SHA-256 digest, checked against a public key embedded in
   `include/ota/FirmwareSigningKey.h`; the release workflow signs each
   published `.bin` with a private key held only as a GitHub Actions secret.
   The Web Dashboard path is deliberately unchanged (still CRC/format-checked
   only). Still open: the Flutter app needs to fetch the release's `.sig`
   asset and include it in the BLE transfer's start frame.
4. [x] Add a release checklist covering build, flash, I2C discovery, calibration, web/BLE compatibility and OTA rollback.
5. [x] Let the Web Dashboard check GitHub for a newer firmware release and
   link directly to the right OTA asset, once accessible over the home
   network (station mode) with real internet access. GitHub's release-asset
   CDN doesn't send CORS headers, so the browser can check versions and link
   to the asset, but can't fetch-and-reupload it automatically; installing
   the downloaded file is still the existing manual upload step.

## Phase 6 — Fuel gauge

1. [x] Add a `BatteryProfile` (rated capacity, charged voltage) and a
   coulomb-counted `StateOfChargeEstimator`, exposed on the OLED, Web
   Dashboard and BLE app. Unlike the Phase 4 item 5 session-counter question
   (still open — that's about the per-power-on-session Ah/Wh totals), this
   fuel gauge is deliberately persisted across reboots from the start, since
   an SoC/time-to-go that resets on every power cycle isn't useful; it
   persists periodically (not every sample) to bound flash writes.
2. [x] Resync to 100% automatically once voltage stays at or above the
   profile's charged voltage with a tapering (near-zero) current for a
   sustained period, or manually from Web/BLE. Time-to-go only applies while
   net discharging, using a smoothed current average.
3. [x] Track deepest-discharge, full-charge-cycle-count and
   average-discharge-depth history alongside the fuel gauge, persisted the
   same way and clearable independently (e.g. after replacing the physical
   battery) without touching the current charge level.
4. Validate the coulomb-counted capacity, auto-sync and history behavior
   against a real charge/discharge cycle once the planned 0.5 mOhm Kelvin
   shunt (Phase 3) is in place; the current 15 mOhm prototype shunt's
   accuracy hasn't been validated under load yet.

**Done when:** SoC, time-to-go and history stats read consistently across
Web Dashboard and BLE app (SoC/time-to-go also on the OLED), survive a
reboot, and a full-charge sync (auto or manual) reliably returns to 100%
while updating cycle history.

## Phase 7 — Dashboard responsiveness

1. [x] Add a push-only WebSocket (port 81) broadcasting the same telemetry
   JSON as `/api/telemetry`, so the dashboard updates without polling
   overhead. Every REST endpoint (settings, calibration, OTA) stays on the
   existing synchronous `WebServer`, untouched; the dashboard page falls back
   to polling if the socket never connects, so nothing regresses for a
   client that can't use WebSockets.

**Done when:** the dashboard stays responsive with the socket connected, and
degrades gracefully (no missing functionality, just slower updates) if it
can't connect.

## Phase 8 — MQTT / Home Assistant integration

1. [x] Add `MqttSettings` (host, port, optional username/password, enabled
   flag) in its own NVS namespace, and `MqttPublisher`, which connects,
   retries on a fixed interval, and publishes retained Home Assistant MQTT
   Discovery config plus a JSON state topic once per publish interval, using
   MQTT's Last Will and Testament for availability. Configurable from the Web
   Dashboard only; deliberately not exposed over BLE, since it's meaningless
   without the home Wi-Fi station already connected.
2. [ ] Validate end-to-end against a real broker (Mosquitto/Home Assistant on
   the local network): confirm entities appear automatically under one
   device, state updates track live telemetry, and availability flips to
   "offline" promptly on power loss or Wi-Fi drop. Verified so far only
   against the save/persist/status-reporting path and a refused connection
   (no local broker was available to test a live connection).

**Done when:** Home Assistant shows all published entities under one device
with live values and correct availability, without any BLE app involvement.

## Phase 9 — Load-protection relay

1. [x] Add `LoadProtectionSettings` (enabled flag, low-voltage threshold,
   low-SoC threshold) and `LoadProtectionMonitor`, which drives a relay/SSR
   GPIO (`Config::LOAD_PROTECTION_RELAY_PIN`). Off by default and a complete
   no-op until explicitly enabled from the Web Dashboard or BLE app, so an
   unreviewed default threshold can never disconnect a load nobody asked to
   protect.
2. [x] Trip (open the relay) the moment voltage or SoC crosses its threshold,
   whichever comes first, and latch — never auto-reconnect, even once the
   reading recovers, so a value hovering at the threshold under load can't
   chatter the relay. A "Reconnect load" control clears the latch, but is
   refused while the triggering condition is still active.
3. [x] Add "Test: force connect" / "Test: force disconnect" controls that
   bypass the enabled flag and every threshold, for bench-testing the
   relay/SSR wiring itself before trusting the automatic logic.
4. [x] Wire an InkBird SSR-25 DA to `GPIO5` through a PN2222 transistor
   stage and confirm real hardware switching: the SSR's control LED turns on
   with "Test: force connect" and off with "Test: force disconnect" via the
   Web Dashboard. Still not validated against a real battery/load — only the
   monitor's own bench-power rail so far — nor against a real automatic
   trip under load.
5. [x] Expose the same controls over BLE: dashboard page `0x19` and control
   commands `12`-`15` (save, reconnect, test-connect, test-disconnect),
   gated on the `protection1` Device Information capability, so the relay
   can be tested and reconnected away from the home Wi-Fi station.

**Done when:** the relay reliably switches a real load through the SSR, the
threshold trip and latched-reconnect behavior are confirmed against a real
discharging battery (not just the bench USB rail), and the test buttons are
confirmed against the physical wiring.

## Non-goals until earlier phases pass

- Persisting energy counters before their accuracy and reset semantics are proven.
- Adding graphs/logging that create a second telemetry path.
- Using placeholder headers as runtime APIs before their data contracts are agreed.
