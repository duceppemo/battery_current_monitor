#include "app/BatteryMonitorApp.h"

#include <Wire.h>

#include "AppConfig.h"

BatteryMonitorApp::BatteryMonitorApp()
    : resetExtremaButton_(
          Config::RESET_EXTREMA_BUTTON_PIN,
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

    Serial.println();
    Serial.println("================================");
    Serial.println(" Battery Monitor Rev A");
    Serial.println(" XIAO ESP32-C3");
    Serial.println(" INA228 + OLED + BLE + WiFi");
    Serial.println(" Modular architecture");
    Serial.println("================================");

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
    telemetry_.update(initial);

    ble_.begin();
    web_.begin(telemetry_);

    const uint32_t now = millis();
    lastMeasurementMs_ = now;
    lastDisplayMs_ = now;
    lastBleMs_ = now;
    lastSerialMs_ = now;

    web_.setRuntimeStatus(
        ble_.connected(),
        display_.isOn(),
        sensor_.goodReads(),
        sensor_.failedReads()
    );

    display_.showMeasurements(
        telemetry_,
        ble_.connected(),
        web_.clientCount(),
        sensor_.failedReads()
    );

    Serial.println();
    Serial.println("Battery monitor running.");
    Serial.println("Buttons:");
    Serial.printf("  Reset min/max : GPIO%u -> button -> GND\n", Config::RESET_EXTREMA_BUTTON_PIN);
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
        telemetry_.resetExtrema();
        Serial.println("Min/max statistics reset from physical button.");

        if (display_.isOn()) {
            display_.showMeasurements(
                telemetry_,
                ble_.connected(),
                web_.clientCount(),
                sensor_.failedReads()
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
                sensor_.failedReads()
            );
        }
    }
}

void BatteryMonitorApp::updateMeasurement(uint32_t nowMs)
{
    if ((nowMs - lastMeasurementMs_) < Config::MEASUREMENT_INTERVAL_MS) {
        return;
    }

    lastMeasurementMs_ = nowMs;

    Telemetry sample;
    sensor_.read(sample);
    telemetry_.update(sample);
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
        sensor_.failedReads()
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
        sensor_.failedReads(),
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

    Serial.println();
    Serial.println("----- BATTERY MONITOR -----");

    if (t.voltageOK) {
        Serial.printf(
            "Voltage:       %.6f V [%.6f .. %.6f]\n",
            t.voltage,
            telemetry_.voltageStats().minimum,
            telemetry_.voltageStats().maximum
        );
    }

    if (t.shuntOK) {
        Serial.printf(
            "Shunt:         %+.6f mV [%+.6f .. %+.6f]\n",
            t.shuntVoltage * 1000.0f,
            telemetry_.shuntStats().minimum * 1000.0f,
            telemetry_.shuntStats().maximum * 1000.0f
        );
        Serial.printf(
            "Current:       %+.6f A [%+.6f .. %+.6f]\n",
            t.current,
            telemetry_.currentStats().minimum,
            telemetry_.currentStats().maximum
        );
    }

    if (t.voltageOK && t.shuntOK) {
        Serial.printf(
            "Power:         %+.6f W [%+.6f .. %+.6f]\n",
            t.power,
            telemetry_.powerStats().minimum,
            telemetry_.powerStats().maximum
        );
    }

    if (t.temperatureOK) {
        Serial.printf(
            "Temperature:   %.2f C [%.2f .. %.2f]\n",
            t.temperature,
            telemetry_.temperatureStats().minimum,
            telemetry_.temperatureStats().maximum
        );
    }

    Serial.printf(
        "I2C:           %lu OK / %lu failed\n",
        static_cast<unsigned long>(sensor_.goodReads()),
        static_cast<unsigned long>(sensor_.failedReads())
    );
    Serial.printf("BLE:           %s\n", ble_.connected() ? "CONNECTED" : "advertising");
    Serial.printf("Wi-Fi clients: %u\n", static_cast<unsigned>(web_.clientCount()));
    Serial.printf("Display:       %s\n", display_.isOn() ? "ON" : "OFF");
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
        display_.isOn(),
        sensor_.goodReads(),
        sensor_.failedReads()
    );

    updateDisplay(now);
    updateBle(now);
    updateSerial(now);

    delay(2);
}
