#include "app/BatteryMonitorApp.h"

#include <esp_system.h>
#include <Wire.h>

#include "AppConfig.h"

namespace
{
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
        case ESP_RST_USB: return "USB reset";
        case ESP_RST_JTAG: return "JTAG reset";
        case ESP_RST_EFUSE: return "eFuse error";
        case ESP_RST_PWR_GLITCH: return "power glitch";
        case ESP_RST_CPU_LOCKUP: return "CPU lockup";
        case ESP_RST_UNKNOWN:
        default: return "unknown";
        }
    }
}

BatteryMonitorApp::BatteryMonitorApp()
    : resetExtremaButton_(
          Config::RESET_SESSION_BUTTON_PIN,
          Config::BUTTON_DEBOUNCE_MS
      ),
      displayToggleButton_(
          Config::DISPLAY_TOGGLE_BUTTON_PIN,
          Config::BUTTON_DEBOUNCE_MS
      )
{
}

void BatteryMonitorApp::begin()
{
    Serial.begin(115200);
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

    resetExtremaButton_.begin();
    displayToggleButton_.begin();

    Wire.begin(Config::SDA_PIN, Config::SCL_PIN);
    Wire.setClock(Config::I2C_CLOCK_HZ);
    Wire.setTimeOut(50);

    delay(100);
    scanI2C();

    display_.begin();
    Wire.setClock(Config::I2C_CLOCK_HZ);
    display_.showStartup();

    sensor_.begin(Wire);
    sensor_.identify();

    Telemetry initial;
    sensor_.read(initial);
    initial.sequence = ++measurementSequence_;
    initial.sampledAtMs = millis();
    telemetry_.update(initial);
    energy_.update(initial);

    ble_.begin();
    web_.begin(telemetry_, energy_.totals());
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

    display_.showMeasurements(
        telemetry_,
        ble_.connected(),
        web_.clientCount(),
        sensor_.failedSamples()
    );

    Serial.println();
    Serial.println("Battery monitor running.");
    Serial.println("Buttons:");
    Serial.printf("  Reset session : GPIO%u -> button -> GND\n", Config::RESET_SESSION_BUTTON_PIN);
    Serial.printf("  Display toggle: GPIO%u -> button -> GND\n", Config::DISPLAY_TOGGLE_BUTTON_PIN);
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

    if (resetExtremaButton_.consumePressed()) {
        resetPhysicalSessionState();
        Serial.println("Session statistics reset from physical button.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples()
            );
        }
    }

    if (displayToggleButton_.consumePressed()) {
        display_.toggle();
        Serial.printf("Display %s from physical button.\n", display_.isOn() ? "ON" : "OFF");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples()
            );
        }
    }

    if (web_.consumeDisplayToggleRequested()) {
        display_.toggle();
        Serial.printf("Display %s from web UI.\n", display_.isOn() ? "ON" : "OFF");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedSamples()
            );
        }
    }

    if (web_.consumeSessionResetRequested()) {
        energy_.reset();
        Serial.println("Energy session reset from web UI.");
    }
}

void BatteryMonitorApp::resetPhysicalSessionState()
{
    // The device has one physical reset control, so it clears every
    // user-visible session value. Add future history/persistent-session reset
    // hooks here rather than creating incompatible button semantics later.
    telemetry_.resetExtrema();
    energy_.reset();
}

void BatteryMonitorApp::updateMeasurement(uint32_t nowMs)
{
    if ((nowMs - lastMeasurementMs_) < Config::MEASUREMENT_INTERVAL_MS) {
        return;
    }

    lastMeasurementMs_ = nowMs;

    Telemetry sample;
    sensor_.read(sample);
    sample.sequence = ++measurementSequence_;
    sample.sampledAtMs = nowMs;
    telemetry_.update(sample);
    energy_.update(sample);
}

void BatteryMonitorApp::updateDisplay(uint32_t nowMs)
{
    if ((nowMs - lastDisplayMs_) < Config::DISPLAY_INTERVAL_MS) {
        return;
    }

    lastDisplayMs_ = nowMs;

    display_.showMeasurements(
        telemetry_,
        ble_.connected(),
        web_.clientCount(),
        sensor_.failedSamples()
    );
}

void BatteryMonitorApp::updateBle(uint32_t nowMs)
{
    if ((nowMs - lastBleMs_) < Config::BLE_INTERVAL_MS) {
        return;
    }

    lastBleMs_ = nowMs;
    ble_.publish(
        telemetry_,
        energy_.totals(),
        sensor_.failedSamples(),
        web_.clientCount()
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

    web_.update();
    ble_.maintain();
    updateButtons(now);
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

    delay(2);
}
