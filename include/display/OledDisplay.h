#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "energy/EnergyAccumulator.h"
#include "telemetry/TelemetryStore.h"

class OledDisplay
{
public:
    OledDisplay();

    void begin();
    void showStartup();
    void showMeasurements(
        const TelemetryStore& store,
        const EnergyTotals& energy,
        bool bleConnected,
        uint8_t wifiClients,
        uint32_t failedSamples
    );

    void setOn(bool on);
    void toggle();
    void nextPage();
    bool isOn() const { return on_; }

private:
    enum class Page : uint8_t { Live, Extrema };

    static void formatCurrent(char* buffer, size_t size, float current, uint8_t decimals = 3);
    static void formatPower(char* buffer, size_t size, float power, uint8_t decimals = 3);
    static void formatVoltage(char* buffer, size_t size, float voltage, uint8_t decimals = 3);
    static void formatTemperature(char* buffer, size_t size, float temperature, uint8_t decimals = 1);
    static void formatShuntVoltage(char* buffer, size_t size, float voltage);
    static void formatEnergy(char* buffer, size_t size, float value, const char* label,
                             uint8_t decimals = 3);
    void showLivePage(const Telemetry& telemetry, const EnergyTotals& energy,
                      bool bleConnected, uint8_t wifiClients, uint32_t failedSamples);
    void showExtremaPage(const TelemetryStore& store);
    void drawPair(int baseline, const char* left, const char* right, int rightStart);
    void sendBufferIfChanged();
    void invalidateFrame();

    U8G2_SSD1309_128X64_NONAME2_F_HW_I2C oled_;
    bool on_ = true;
    Page page_ = Page::Live;
    uint32_t lastFrameHash_ = 0;
    bool hasLastFrameHash_ = false;
    bool frameRefreshRequired_ = true;
};
