#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "telemetry/TelemetryStore.h"

class OledDisplay
{
public:
    OledDisplay();

    void begin();
    void showStartup();
    void showMeasurements(
        const TelemetryStore& store,
        bool bleConnected,
        uint8_t wifiClients,
        uint32_t failedSamples
    );

    void setOn(bool on);
    void toggle();
    bool isOn() const { return on_; }

private:
    static void formatCurrent(char* buffer, size_t size, float current);
    static void formatPower(char* buffer, size_t size, float power);

    U8G2_SSD1309_128X64_NONAME2_F_HW_I2C oled_;
    bool on_ = true;
};
