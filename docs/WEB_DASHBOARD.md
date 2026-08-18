# Web Dashboard

## Wi-Fi

Default access point:

```text
SSID:     BatteryMonitor
Password: Battery123
```

Open the IP printed in the serial monitor; the default ESP32 SoftAP address is
normally `192.168.4.1`.

The dashboard can also save a home-network SSID and password. The monitor then
joins that network in parallel with its always-on recovery AP. Once joined,
open `http://battery-monitor.local/` from the same network (or use the station
IP shown in the dashboard/serial log). The Wi-Fi password is stored only in the
ESP32's NVS and is never returned by the API or shown in the dashboard. If the
home network is unavailable, measurements, BLE and the `BatteryMonitor` AP
continue normally; the station connection retries in the background.

## Dashboard overview

The dashboard shows live, minimum and maximum values for:

- voltage
- current
- power
- INA228 temperature
- shunt voltage

The dashboard also shows BLE, I2C, Wi-Fi-client and display status, plus a
battery fuel gauge card (state of charge and time-to-empty) once a battery
profile is saved and synced to full at least once.

A "Show °F"/"Show °C" button in the header toggles the displayed temperature
unit (live value, min/max, and the alarm temperature threshold field). This
is purely a browser-local display preference, persisted in `localStorage`;
the monitor, `/api/telemetry` and the alarm-save API always use Celsius —
the dashboard converts the alarm threshold to Celsius before saving and back
to the display unit when showing the stored value.

It provides grouped controls beside the affected data: separate extrema and
session-energy resets, OLED power, guided calibration, persistent alarm
limits, battery profile setup, manual full-charge sync and history reset, home Wi-Fi setup
and Web OTA. The update card shows both the installed firmware version and
the version detected in a selected OTA image before upload, and can check
GitHub for a newer release (needs the *browser's* device to have internet
access, independent of the monitor's own connectivity) with a direct
download link to the right `.bin` asset. Browsers don't allow a page to
install a downloaded file automatically, so installing it is still the same
manual "select file, then upload" step as before.

## MQTT / Home Assistant

The Web Dashboard has an MQTT card (host, port, optional username/password,
enable toggle) that is entirely separate from the BLE app; there is no BLE
control for MQTT. Settings persist in NVS and require the home Wi-Fi station
to be connected, since the recovery AP has no internet or LAN broker path.

When enabled, the monitor publishes Home Assistant MQTT Discovery config
messages (retained, under `homeassistant/...`) once per broker connection,
creating voltage, current, power, temperature, net Ah/Wh, state of charge,
time-to-empty, sensor-problem and alarm-active entities automatically grouped
under one Home Assistant device. State (`batterymonitor/<device-id>/state`)
publishes as JSON every 10 seconds, and availability
(`batterymonitor/<device-id>/availability`) uses MQTT's Last Will and
Testament so Home Assistant marks the device offline promptly if it loses
power or Wi-Fi. Reconnection is retried every 15 seconds while enabled and
not connected; the stored password is never echoed back to the dashboard.

## Load protection (optional)

Both the Web Dashboard (low-voltage cutoff, low-SoC cutoff, enable toggle)
and the BLE app have a load-protection card that drives the relay described
in [HARDWARE.md](HARDWARE.md#load-protection-relay-optional). It is
**disabled by default and does nothing until explicitly enabled**, so an
unreviewed default threshold can never disconnect a load the operator never
asked to protect. Once enabled, the relay opens (load disconnected) the
moment voltage or state of charge drops below its configured threshold,
whichever happens first — SoC is only checked once the fuel gauge has been
synced to full at least once. The trip **latches**: it never reconnects on
its own, even if the reading recovers, so it cannot chatter if a value hovers
right at the threshold under load. A "Reconnect load" control clears it, but
is refused if the trigger condition is still active. Separate "Test: force
connect" / "Test: force disconnect" controls bypass all of this to bench-test
the relay wiring directly, whether or not protection is enabled, from either
transport. The BLE side of this feature (dashboard page and control commands)
is documented in [BLE_PROTOCOL.md](BLE_PROTOCOL.md).
