#include "app/BatteryMonitorApp.h"

#include <esp_system.h>
#include <Wire.h>
#include <cstring>

#include "AppConfig.h"

namespace
{
    // Survives any reset that doesn't remove power (including the reset the
    // native USB peripheral triggers just from a host opening the port), but
    // not a real power-off. Lets the *next* boot report exactly how far a
    // stuck prior boot got, since a live serial attach can't observe it
    // directly without itself resetting the board.
    RTC_NOINIT_ATTR uint32_t bootCheckpointMagic;
    RTC_NOINIT_ATTR uint32_t bootCheckpoint;
    constexpr uint32_t BOOT_CHECKPOINT_MAGIC = 0xB007C0DEu;

    const char* checkpointName(uint32_t checkpoint)
    {
        switch (checkpoint) {
        case 0: return "start";
        case 1: return "I2C bus init";
        case 2: return "I2C scan";
        case 3: return "OLED init";
        case 4: return "OLED splash drawn";
        case 5: return "sensor init/identify";
        case 6: return "sensor configured";
        case 7: return "BLE init";
        case 8: return "web/Wi-Fi init";
        case 9: return "boot complete";
        default: return "unknown";
        }
    }

    const char* resetReasonText(esp_reset_reason_t reason)
    {
        switch (reason) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external reset";
        case ESP_RST_SW: return "software reset";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "SDIO reset";
        case ESP_RST_UNKNOWN:
        default: return "unknown";
        }
    }

    void logRuntimeEvent(const char* message)
    {
        const size_t length = strlen(message);
        if (Serial.availableForWrite() >= static_cast<int>(length + 1)) {
            Serial.write(reinterpret_cast<const uint8_t*>(message), length);
            Serial.write('\n');
        }
    }

    // A slave (OLED or INA228) can be caught mid-transaction if power is cut
    // while it is holding SDA low; the bus then never recovers on its own on
    // the next power-up, wedging every later Wire transaction. Bit-bang up to
    // nine SCL pulses to walk a stuck slave through releasing the line, then
    // issue a STOP, before Wire.begin() ever touches the pins.
    void recoverI2CBus(uint8_t sdaPin, uint8_t sclPin)
    {
        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, INPUT_PULLUP);
        delayMicroseconds(10);

        if (digitalRead(sdaPin) == HIGH) {
            return;
        }

        pinMode(sclPin, OUTPUT);
        for (uint8_t i = 0; i < 9 && digitalRead(sdaPin) == LOW; ++i) {
            digitalWrite(sclPin, LOW);
            delayMicroseconds(5);
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(5);
        }

        digitalWrite(sclPin, HIGH);
        pinMode(sdaPin, OUTPUT);
        digitalWrite(sdaPin, LOW);
        delayMicroseconds(5);
        digitalWrite(sdaPin, HIGH);
        delayMicroseconds(5);

        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, INPUT_PULLUP);
    }
}

BatteryMonitorApp::BatteryMonitorApp()
    : resetExtremaButton_(
          Config::RESET_SESSION_BUTTON_PIN,
          Config::BUTTON_DEBOUNCE_MS,
          Config::RESET_RESTART_LONG_PRESS_MS
      ),
      displayToggleButton_(
          Config::DISPLAY_TOGGLE_BUTTON_PIN,
          Config::BUTTON_DEBOUNCE_MS,
          Config::DISPLAY_LONG_PRESS_MS
      )
{
}

void BatteryMonitorApp::begin()
{
    Serial.begin(115200);
    // USB serial output is diagnostic-only. Never hold the local controls or
    // radio startup while a monitor is detached or not consuming data.
    Serial.setTxTimeoutMs(0);
    delay(1200);
    resetReason_ = resetReasonText(esp_reset_reason());

    Serial.println();
    Serial.println("================================");
    Serial.println(" Battery Monitor Rev A");
    Serial.println(" XIAO ESP32-C3");
    Serial.println(" INA228 + OLED + BLE + WiFi");
    Serial.println(" Modular architecture");
    Serial.println("================================");
    Serial.printf("Reset reason: %s\n", resetReason_);
    if (bootCheckpointMagic == BOOT_CHECKPOINT_MAGIC) {
        Serial.printf(
            "Previous boot's last checkpoint: %lu (%s)\n",
            static_cast<unsigned long>(bootCheckpoint),
            checkpointName(bootCheckpoint)
        );
    } else {
        Serial.println("Previous boot checkpoint: none (cold RTC memory).");
    }
    bootCheckpointMagic = BOOT_CHECKPOINT_MAGIC;
    bootCheckpoint = 0;

    resetExtremaButton_.begin();
    displayToggleButton_.begin();

    recoverI2CBus(Config::SDA_PIN, Config::SCL_PIN);
    Wire.begin(Config::SDA_PIN, Config::SCL_PIN);
    Wire.setClock(Config::I2C_CLOCK_HZ);
    Wire.setTimeOut(50);
    bootCheckpoint = 1;

    delay(100);
    scanI2C();
    bootCheckpoint = 2;

    display_.begin();
    Wire.setClock(Config::I2C_CLOCK_HZ);
    bootCheckpoint = 3;
    display_.showStartup(Config::FIRMWARE_VERSION);
    bootCheckpoint = 4;
    delay(Config::SPLASH_SCREEN_DURATION_MS);

    sensor_.begin(Wire);
    sensor_.identify();
    bootCheckpoint = 5;
    calibration_.begin();
    alarms_.begin();
    batteryProfile_.begin();
    stateOfCharge_.begin();
    mqttSettings_.begin();
    loadProtectionSettings_.begin();
    loadProtectionMonitor_.begin();
    energyPersistenceSettings_.begin();
    energy_.begin(energyPersistenceSettings_.current());
    sensor_.setCalibration(calibration_.current());
    const CurrentCalibration& activeCalibration = calibration_.current();
    Serial.printf(
        "Current calibration: %s, R=%.6f ohm, offset=%+.2f uV, gain=%.6f\n",
        calibration_.loadedFromStorage() ? "stored" : "default",
        static_cast<double>(activeCalibration.shuntResistanceOhms),
        static_cast<double>(activeCalibration.shuntOffsetVolts * 1.0e6f),
        static_cast<double>(activeCalibration.currentGain)
    );
    if (sensor_.configure()) {
        Serial.println("INA228 configuration applied and verified.");
    } else {
        Serial.println("WARNING: INA228 configuration verification failed.");
    }
    bootCheckpoint = 6;

    Telemetry initial;
    sensor_.read(initial);
    initial.sequence = ++measurementSequence_;
    initial.sampledAtMs = millis();
    telemetry_.update(initial);
    alarmMonitor_.update(initial, alarms_.current());
    energy_.update(initial, energyPersistenceSettings_.current());
    stateOfCharge_.update(initial, batteryProfile_.current(), initial.sampledAtMs);

    ble_.begin(firmwareUpdate_);
    bootCheckpoint = 7;
    web_.begin(
        telemetry_, energy_.totals(), sensor_, calibration_, alarms_, alarmMonitor_,
        firmwareUpdate_, batteryProfile_, stateOfCharge_, mqttSettings_, mqttPublisher_,
        loadProtectionSettings_, loadProtectionMonitor_, energyPersistenceSettings_
    );
    bootCheckpoint = 8;
    web_.setCalibrationStatus(
        calibration_.loadedFromStorage() ? "stored calibration active"
                                         : "default calibration active"
    );
    Serial.println("HTTP server started.");

    const uint32_t now = millis();
    lastMeasurementMs_ = now;
    lastDisplayMs_ = now;
    lastBleMs_ = now;
    lastSerialMs_ = now;

    web_.setRuntimeStatus(
        ble_.connected(),
        ble_.advertising(),
        display_.isOn(),
        resetReason_,
        sensor_.successfulSamples(),
        sensor_.failedSamples()
    );

    // Defensive resync in case BLE/Wi-Fi radio bring-up disturbed the OLED
    // (it has no dedicated hardware reset line here); cheap and harmless
    // either way.
    display_.begin();
    Wire.setClock(Config::I2C_CLOCK_HZ);
    display_.showMeasurements(
        telemetry_,
        energy_.totals(),
        ble_.connected(),
        web_.clientCount(),
        sensor_.failedSamples(),
        stateOfCharge_.known(),
        stateOfCharge_.percent(batteryProfile_.current())
    );
    bootCheckpoint = 9;

    Serial.println();
    Serial.println("Battery monitor running.");
    Serial.println("Buttons:");
    Serial.printf("  Reset: short = session, hold = ESP32 restart (GPIO%u)\n",
                  Config::RESET_SESSION_BUTTON_PIN);
    Serial.printf("  Display: short press = page, hold = on/off (GPIO%u)\n", Config::DISPLAY_TOGGLE_BUTTON_PIN);
}

void BatteryMonitorApp::scanI2C()
{
    Serial.println();
    Serial.println("I2C devices:");

    uint8_t count = 0;
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission(true) == 0) {
            Serial.printf("  0x%02X\n", address);
            ++count;
        }
    }

    Serial.printf("%u device(s) found.\n", count);
}

void BatteryMonitorApp::updateButtons(uint32_t nowMs)
{
    resetExtremaButton_.update(nowMs);
    displayToggleButton_.update(nowMs);

    if (resetExtremaButton_.consumeLongPress()) {
        logRuntimeEvent("ESP32 restart from reset-button long press.");
        ESP.restart();
        return;
    }

    if (resetExtremaButton_.consumeShortPress()) {
        resetPhysicalSessionState();
        logRuntimeEvent("Session statistics reset from physical button.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                energy_.totals(),
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples(),
                stateOfCharge_.known(),
                stateOfCharge_.percent(batteryProfile_.current())
            );
        }
    }

    if (displayToggleButton_.consumeShortPress()) {
        display_.nextPage();
        logRuntimeEvent("Display page changed from physical button.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                energy_.totals(),
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples(),
                stateOfCharge_.known(),
                stateOfCharge_.percent(batteryProfile_.current())
            );
        }
    }

    if (displayToggleButton_.consumeLongPress()) {
        display_.toggle();
        logRuntimeEvent(display_.isOn() ? "Display ON from long press."
                                        : "Display OFF from long press.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                energy_.totals(),
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples(),
                stateOfCharge_.known(),
                stateOfCharge_.percent(batteryProfile_.current())
            );
        }
    }

    uint16_t bleRequestId = 0;
    if (ble_.consumeResetExtremaRequested(bleRequestId)) {
        telemetry_.resetExtrema();
        ble_.reportControlResult(1, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Extrema reset from BLE app.");
    }

    if (ble_.consumeSessionResetRequested(bleRequestId)) {
        energy_.reset(energyPersistenceSettings_.current());
        ble_.reportControlResult(2, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Energy session reset from BLE app.");
    }

    if (ble_.consumeDisplayToggleRequested(bleRequestId)) {
        display_.toggle();
        ble_.reportControlResult(3, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent(display_.isOn() ? "Display ON from BLE app."
                                        : "Display OFF from BLE app.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                energy_.totals(),
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples(),
                stateOfCharge_.known(),
                stateOfCharge_.percent(batteryProfile_.current())
            );
        }
    }

    if (web_.consumeDisplayToggleRequested()) {
        display_.toggle();
        logRuntimeEvent(display_.isOn() ? "Display ON from web UI."
                                        : "Display OFF from web UI.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                energy_.totals(),
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples(),
                stateOfCharge_.known(),
                stateOfCharge_.percent(batteryProfile_.current())
            );
        }
    }

    if (web_.consumeSessionResetRequested()) {
        energy_.reset(energyPersistenceSettings_.current());
        logRuntimeEvent("Energy session reset from web UI.");
    }

    CurrentCalibration requestedCalibration;
    DeviceAlarmSettings requestedAlarms;
    if (web_.consumeCalibrationSaveRequested(requestedCalibration)) {
        if (calibration_.save(requestedCalibration)) {
            sensor_.setCalibration(calibration_.current());
            resetPhysicalSessionState();
            web_.setCalibrationStatus("saved; session reset");
            logRuntimeEvent("Calibration saved from web UI; session reset.");
        } else {
            web_.setCalibrationStatus("save failed");
            logRuntimeEvent("Calibration save failed from web UI.");
        }
    }

    if (web_.consumeCalibrationResetRequested()) {
        if (calibration_.clear()) {
            sensor_.setCalibration(calibration_.current());
            resetPhysicalSessionState();
            web_.setCalibrationStatus("default restored; session reset");
            logRuntimeEvent("Calibration reset to default from web UI; session reset.");
        } else {
            web_.setCalibrationStatus("reset failed");
            logRuntimeEvent("Calibration reset failed from web UI.");
        }
    }

    if (web_.consumeAlarmSaveRequested(requestedAlarms)) {
        if (alarms_.save(requestedAlarms)) {
            alarmMonitor_.update(telemetry_.current(), alarms_.current());
            logRuntimeEvent("Device alarms saved from web UI.");
        } else {
            logRuntimeEvent("Device alarm save from web UI failed.");
        }
    }

    BatteryProfileSettings requestedBatteryProfile;
    if (web_.consumeBatteryProfileSaveRequested(requestedBatteryProfile)) {
        if (batteryProfile_.save(requestedBatteryProfile)) {
            logRuntimeEvent("Battery profile saved from web UI.");
        } else {
            logRuntimeEvent("Battery profile save from web UI failed.");
        }
    }

    if (web_.consumeBatterySyncRequested()) {
        stateOfCharge_.syncToFull(batteryProfile_.current());
        logRuntimeEvent("Battery fuel gauge synced to full from web UI.");
    }

    if (web_.consumeBatteryHistoryResetRequested()) {
        stateOfCharge_.resetHistory();
        logRuntimeEvent("Battery history reset from web UI.");
    }

    MqttBrokerSettings requestedMqtt;
    if (web_.consumeMqttSettingsSaveRequested(requestedMqtt)) {
        if (mqttSettings_.save(requestedMqtt)) {
            logRuntimeEvent("MQTT settings saved from web UI.");
        } else {
            logRuntimeEvent("MQTT settings save from web UI failed.");
        }
    }

    LoadProtectionConfig requestedProtection;
    if (web_.consumeLoadProtectionSaveRequested(requestedProtection)) {
        if (loadProtectionSettings_.save(requestedProtection)) {
            logRuntimeEvent("Load protection settings saved from web UI.");
        } else {
            logRuntimeEvent("Load protection settings save from web UI failed.");
        }
    }

    if (web_.consumeLoadProtectionReconnectRequested()) {
        const auto result = loadProtectionMonitor_.reconnect(
            loadProtectionSettings_.current(), telemetry_.current(),
            stateOfCharge_, batteryProfile_.current()
        );
        switch (result) {
        case LoadProtectionMonitor::ReconnectResult::Reconnected:
            logRuntimeEvent("Load reconnected from web UI.");
            break;
        case LoadProtectionMonitor::ReconnectResult::ConditionStillActive:
            logRuntimeEvent("Load reconnect from web UI rejected: condition still active.");
            break;
        case LoadProtectionMonitor::ReconnectResult::NotTripped:
            logRuntimeEvent("Load reconnect from web UI ignored: not tripped.");
            break;
        }
    }

    if (web_.consumeLoadProtectionTestDisconnectRequested()) {
        loadProtectionMonitor_.testDisconnect();
        logRuntimeEvent("Load relay test: forced disconnect from web UI.");
    }

    if (web_.consumeLoadProtectionTestConnectRequested()) {
        loadProtectionMonitor_.testConnect();
        logRuntimeEvent("Load relay test: forced connect from web UI.");
    }

    EnergyPersistenceConfig requestedEnergyPersistence;
    if (web_.consumeEnergyPersistenceSaveRequested(requestedEnergyPersistence)) {
        // Deliberately does not call energy_.begin() here: that would
        // restore whatever was last on NVS, discarding the live totals this
        // session has accumulated since boot if persistence was previously
        // enabled, then disabled, then re-enabled. begin() is a boot-time
        // restore only; toggling this setting on just starts persisting the
        // totals already running in RAM from here on.
        if (energyPersistenceSettings_.save(requestedEnergyPersistence)) {
            logRuntimeEvent("Energy persistence setting saved from web UI.");
        } else {
            logRuntimeEvent("Energy persistence setting save from web UI failed.");
        }
    }

    if (ble_.consumeCalibrationSaveRequested(requestedCalibration, bleRequestId)) {
        if (calibration_.save(requestedCalibration)) {
            sensor_.setCalibration(calibration_.current());
            resetPhysicalSessionState();
            ble_.reportControlResult(4, bleRequestId, BleTelemetryService::ControlResult::Applied);
            web_.setCalibrationStatus("saved by BLE; session reset");
            logRuntimeEvent("Calibration saved from BLE app; session reset.");
        } else {
            ble_.reportControlResult(4, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Calibration save from BLE app failed.");
        }
    }

    if (ble_.consumeCalibrationResetRequested(bleRequestId)) {
        if (calibration_.clear()) {
            sensor_.setCalibration(calibration_.current());
            resetPhysicalSessionState();
            ble_.reportControlResult(5, bleRequestId, BleTelemetryService::ControlResult::Applied);
            web_.setCalibrationStatus("default restored by BLE");
            logRuntimeEvent("Calibration reset to default from BLE app; session reset.");
        } else {
            ble_.reportControlResult(5, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Calibration reset from BLE app failed.");
        }
    }

    if (ble_.consumeAlarmSaveRequested(requestedAlarms, bleRequestId)) {
        if (alarms_.save(requestedAlarms)) {
            alarmMonitor_.update(telemetry_.current(), alarms_.current());
            ble_.reportControlResult(6, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Device alarms saved from BLE app.");
        } else {
            ble_.reportControlResult(6, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Device alarm save from BLE app failed.");
        }
    }

    WifiStationSettings requestedWifi;
    if (ble_.consumeWifiSaveRequested(requestedWifi, bleRequestId)) {
        if (web_.saveWifiSettings(requestedWifi)) {
            ble_.reportControlResult(7, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Wi-Fi station credentials saved from BLE app.");
        } else {
            ble_.reportControlResult(7, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Wi-Fi station save from BLE app failed.");
        }
    }

    if (ble_.consumeWifiClearRequested(bleRequestId)) {
        if (web_.clearWifiSettings()) {
            ble_.reportControlResult(8, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Wi-Fi station credentials cleared from BLE app.");
        } else {
            ble_.reportControlResult(8, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Wi-Fi station clear from BLE app failed.");
        }
    }

    if (ble_.consumeBatteryProfileSaveRequested(requestedBatteryProfile, bleRequestId)) {
        if (batteryProfile_.save(requestedBatteryProfile)) {
            ble_.reportControlResult(9, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Battery profile saved from BLE app.");
        } else {
            ble_.reportControlResult(9, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Battery profile save from BLE app failed.");
        }
    }

    if (ble_.consumeBatterySyncRequested(bleRequestId)) {
        stateOfCharge_.syncToFull(batteryProfile_.current());
        ble_.reportControlResult(10, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Battery fuel gauge synced to full from BLE app.");
    }

    if (ble_.consumeBatteryHistoryResetRequested(bleRequestId)) {
        stateOfCharge_.resetHistory();
        ble_.reportControlResult(11, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Battery history reset from BLE app.");
    }

    LoadProtectionConfig requestedProtectionBle;
    if (ble_.consumeLoadProtectionSaveRequested(requestedProtectionBle, bleRequestId)) {
        if (loadProtectionSettings_.save(requestedProtectionBle)) {
            ble_.reportControlResult(12, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Load protection settings saved from BLE app.");
        } else {
            ble_.reportControlResult(12, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Load protection settings save from BLE app failed.");
        }
    }

    if (ble_.consumeLoadProtectionReconnectRequested(bleRequestId)) {
        const auto result = loadProtectionMonitor_.reconnect(
            loadProtectionSettings_.current(), telemetry_.current(),
            stateOfCharge_, batteryProfile_.current()
        );
        if (result == LoadProtectionMonitor::ReconnectResult::ConditionStillActive) {
            ble_.reportControlResult(13, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Load reconnect from BLE app rejected: condition still active.");
        } else {
            ble_.reportControlResult(13, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Load reconnected from BLE app.");
        }
    }

    if (ble_.consumeLoadProtectionTestConnectRequested(bleRequestId)) {
        loadProtectionMonitor_.testConnect();
        ble_.reportControlResult(14, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Load relay test: forced connect from BLE app.");
    }

    if (ble_.consumeLoadProtectionTestDisconnectRequested(bleRequestId)) {
        loadProtectionMonitor_.testDisconnect();
        ble_.reportControlResult(15, bleRequestId, BleTelemetryService::ControlResult::Applied);
        logRuntimeEvent("Load relay test: forced disconnect from BLE app.");
    }

    EnergyPersistenceConfig requestedEnergyPersistenceBle;
    if (ble_.consumeEnergyPersistenceSaveRequested(requestedEnergyPersistenceBle, bleRequestId)) {
        // See the matching web-UI handler above: no energy_.begin() call
        // here either, for the same reason.
        if (energyPersistenceSettings_.save(requestedEnergyPersistenceBle)) {
            ble_.reportControlResult(16, bleRequestId, BleTelemetryService::ControlResult::Applied);
            logRuntimeEvent("Energy persistence setting saved from BLE app.");
        } else {
            ble_.reportControlResult(16, bleRequestId, BleTelemetryService::ControlResult::Failed);
            logRuntimeEvent("Energy persistence setting save from BLE app failed.");
        }
    }
}

void BatteryMonitorApp::resetPhysicalSessionState()
{
    // The device has one physical reset control, so it clears every
    // user-visible session value. Add future history/persistent-session reset
    // hooks here rather than creating incompatible button semantics later.
    telemetry_.resetExtrema();
    energy_.reset(energyPersistenceSettings_.current());
}

void BatteryMonitorApp::updateMeasurement(uint32_t nowMs)
{
    if ((nowMs - lastMeasurementMs_) < Config::MEASUREMENT_INTERVAL_MS) {
        return;
    }

    Telemetry sample;
    sensor_.read(sample);
    const uint32_t completedAtMs = millis();
    lastMeasurementMs_ = completedAtMs;
    sample.sequence = ++measurementSequence_;
    sample.sampledAtMs = completedAtMs;
    telemetry_.update(sample);
    alarmMonitor_.update(sample, alarms_.current());
    energy_.update(sample, energyPersistenceSettings_.current());
    stateOfCharge_.update(sample, batteryProfile_.current(), completedAtMs);
    loadProtectionMonitor_.update(
        loadProtectionSettings_.current(), sample, stateOfCharge_, batteryProfile_.current()
    );
}

void BatteryMonitorApp::updateDisplay(uint32_t nowMs)
{
    if ((nowMs - lastDisplayMs_) < Config::DISPLAY_INTERVAL_MS) {
        return;
    }

    lastDisplayMs_ = nowMs;

    display_.showMeasurements(
        telemetry_,
        energy_.totals(),
        ble_.connected(),
        web_.clientCount(),
        sensor_.failedSamples(),
        stateOfCharge_.known(),
        stateOfCharge_.percent(batteryProfile_.current())
    );
}

void BatteryMonitorApp::updateBle(uint32_t nowMs)
{
    if ((nowMs - lastBleMs_) < Config::BLE_INTERVAL_MS) {
        return;
    }

    lastBleMs_ = nowMs;
    uint8_t stationIp[4];
    web_.stationIpOctets(stationIp);
    ble_.publish(
        telemetry_,
        energy_.totals(),
        sensor_,
        calibration_.current(),
        alarms_.current(),
        alarmMonitor_.state(),
        calibration_.loadedFromStorage(),
        display_.isOn(),
        web_.accessPointReady(),
        resetReason_,
        web_.clientCount(),
        web_.stationConfigured(),
        web_.stationConnected(),
        web_.mdnsReady(),
        stationIp,
        batteryProfile_.current(),
        stateOfCharge_,
        loadProtectionSettings_.current(),
        loadProtectionMonitor_,
        energyPersistenceSettings_.current()
    );
}

void BatteryMonitorApp::updateMqtt(uint32_t nowMs)
{
    // Reconnect and publish are both interval-gated inside MqttPublisher
    // itself; call every loop, unthrottled here, so its underlying
    // PubSubClient::loop() stays responsive to keepalive pings.
    mqttPublisher_.update(
        nowMs,
        mqttSettings_.current(),
        telemetry_.current(),
        energy_.totals(),
        batteryProfile_.current(),
        stateOfCharge_,
        alarmMonitor_.state()
    );
}

void BatteryMonitorApp::updateSerial(uint32_t nowMs)
{
    if ((nowMs - lastSerialMs_) < Config::SERIAL_INTERVAL_MS) {
        return;
    }

    lastSerialMs_ = nowMs;
    printDiagnostics();
}

void BatteryMonitorApp::printDiagnostics() const
{
    const Telemetry& t = telemetry_.current();

    // Do not block the monitoring loop behind a detached or slow USB serial
    // client. One concise line is enough for live diagnostics; the dashboard
    // carries the detailed values and extrema.
    char report[256];
    const int length = snprintf(
        report,
        sizeof(report),
        "BM #%lu V=%.3f I=%+.6f P=%+.6f T=%.1f samples=%lu/%lu reg=%lu/%lu BLE=%s WiFi=%u OLED=%s\n",
        static_cast<unsigned long>(t.sequence),
        static_cast<double>(t.voltage),
        static_cast<double>(t.current),
        static_cast<double>(t.power),
        static_cast<double>(t.temperature),
        static_cast<unsigned long>(sensor_.successfulSamples()),
        static_cast<unsigned long>(sensor_.failedSamples()),
        static_cast<unsigned long>(sensor_.successfulRegisterReads()),
        static_cast<unsigned long>(sensor_.failedRegisterReads()),
        ble_.connected() ? "connected" : "advertising",
        static_cast<unsigned>(web_.clientCount()),
        display_.isOn() ? "on" : "off"
    );

    if (length > 0 && static_cast<size_t>(length) < sizeof(report) &&
        Serial.availableForWrite() >= length) {
        Serial.write(reinterpret_cast<const uint8_t*>(report), static_cast<size_t>(length));
    }
}

void BatteryMonitorApp::update()
{
    const uint32_t now = millis();

    // Keep the physical controls and OLED path ahead of transport servicing.
    // HTTP/Wi-Fi recovery may occasionally take longer than a normal loop,
    // but it must not make the local interface appear frozen.
    updateButtons(now);
    firmwareUpdate_.processPendingVerification();
    if (firmwareUpdate_.consumeRestartRequested()) {
        firmwareRestartAtMs_ = now + Config::BLE_OTA_RESTART_GRACE_MS;
        logRuntimeEvent("Firmware update verified; allowing BLE status delivery before restart.");
    }
    if (firmwareRestartAtMs_ != 0 &&
        static_cast<int32_t>(now - firmwareRestartAtMs_) >= 0) {
        logRuntimeEvent("Restarting into verified firmware image.");
        ESP.restart();
        return;
    }
    updateMeasurement(now);

    web_.setRuntimeStatus(
        ble_.connected(),
        ble_.advertising(),
        display_.isOn(),
        resetReason_,
        sensor_.successfulSamples(),
        sensor_.failedSamples()
    );

    updateDisplay(now);
    updateBle(now);
    updateSerial(now);

    web_.update();
    ble_.maintain();
    updateMqtt(now);

    delay(2);
}
