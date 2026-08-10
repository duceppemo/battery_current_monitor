#include "display/OledDisplay.h"

#include <cmath>

#include "AppConfig.h"

OledDisplay::OledDisplay()
    : oled_(U8G2_R0, U8X8_PIN_NONE)
{
}

void OledDisplay::begin()
{
    oled_.setI2CAddress(Config::OLED_ADDRESS << 1);
    oled_.begin();
    on_ = true;
    oled_.setPowerSave(0);
}

void OledDisplay::setOn(bool on)
{
    on_ = on;
    oled_.setPowerSave(on ? 0 : 1);
}

void OledDisplay::toggle()
{
    setOn(!on_);
}

void OledDisplay::showStartup()
{
    if (!on_) {
        return;
    }

    oled_.clearBuffer();
    oled_.setFont(u8g2_font_6x12_tf);

    constexpr char TITLE[] = "Battery Monitor";
    constexpr char REVISION[] = "Revision A";
    constexpr char HARDWARE[] = "INA228 + SSD1309";
    constexpr char STARTING[] = "BLE + WiFi starting";
    constexpr int DISPLAY_WIDTH = 128;

    auto drawCentered = [this](const char* text, int baseline) {
        oled_.drawStr((DISPLAY_WIDTH - oled_.getStrWidth(text)) / 2, baseline, text);
    };

    drawCentered(TITLE, 12);
    drawCentered(REVISION, 25);
    oled_.drawHLine(4, 30, 120);
    drawCentered(HARDWARE, 45);
    drawCentered(STARTING, 60);
    oled_.sendBuffer();
}

void OledDisplay::formatCurrent(char* buffer, size_t size, float current)
{
    if (std::fabs(current) < 1.0f) {
        snprintf(buffer, size, "%+.3f mA", current * 1000.0f);
    } else {
        snprintf(buffer, size, "%+.3f A", current);
    }
}

void OledDisplay::formatPower(char* buffer, size_t size, float power)
{
    if (std::fabs(power) < 1.0f) {
        snprintf(buffer, size, "%+.3f mW", power * 1000.0f);
    } else {
        snprintf(buffer, size, "%+.2f W", power);
    }
}

void OledDisplay::showMeasurements(
    const TelemetryStore& store,
    bool bleConnected,
    uint8_t wifiClients,
    uint32_t failedSamples)
{
    if (!on_) {
        return;
    }

    constexpr int LEFT = 4;
    constexpr int RIGHT = 124;

    const Telemetry& telemetry = store.current();
    char value[32];

    oled_.clearBuffer();
    oled_.setFont(u8g2_font_6x12_tf);

    oled_.drawStr(LEFT, 13, "V");
    if (telemetry.voltageOK) {
        snprintf(value, sizeof(value), "%.3f V", telemetry.voltage);
    } else {
        snprintf(value, sizeof(value), "ERROR");
    }
    oled_.drawStr(RIGHT - oled_.getStrWidth(value), 13, value);

    oled_.drawStr(LEFT, 28, "I");
    if (telemetry.shuntOK) {
        formatCurrent(value, sizeof(value), telemetry.current);
    } else {
        snprintf(value, sizeof(value), "ERROR");
    }
    oled_.drawStr(RIGHT - oled_.getStrWidth(value), 28, value);

    oled_.drawStr(LEFT, 43, "P");
    if (telemetry.powerOK()) {
        formatPower(value, sizeof(value), telemetry.power);
    } else {
        snprintf(value, sizeof(value), "ERROR");
    }
    oled_.drawStr(RIGHT - oled_.getStrWidth(value), 43, value);

    if (telemetry.temperatureOK) {
        snprintf(value, sizeof(value), "T %.1fC", telemetry.temperature);
    } else {
        snprintf(value, sizeof(value), "T ERR");
    }
    oled_.drawStr(LEFT, 59, value);

    snprintf(
        value,
        sizeof(value),
        "B:%c W:%u E:%lu",
        bleConnected ? 'C' : '-',
        static_cast<unsigned>(wifiClients),
        static_cast<unsigned long>(failedSamples)
    );
    oled_.drawStr(RIGHT - oled_.getStrWidth(value), 59, value);

    oled_.sendBuffer();
}
