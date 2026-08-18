#include "display/OledDisplay.h"

#include <cmath>

#include "AppConfig.h"

namespace
{
    constexpr uint32_t FNV1A_OFFSET_BASIS = 2166136261UL;
    constexpr uint32_t FNV1A_PRIME = 16777619UL;
}

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
    invalidateFrame();
}

void OledDisplay::setOn(bool on)
{
    if (on_ == on) {
        return;
    }

    on_ = on;
    oled_.setPowerSave(on ? 0 : 1);
    invalidateFrame();
}

void OledDisplay::toggle()
{
    setOn(!on_);
}

void OledDisplay::showStartup(const char* firmwareVersion)
{
    if (!on_) {
        return;
    }

    oled_.clearBuffer();
    oled_.setFont(u8g2_font_6x12_tf);

    constexpr char TITLE[] = "Battery Monitor";
    constexpr char REVISION_AND_RATING[] = "Rev. A | 60V 50A cont.";
    constexpr char HARDWARE[] = "INA228 + SSD1309";
    constexpr char STARTING[] = "BLE + WiFi starting";
    constexpr int DISPLAY_WIDTH = 128;

    auto drawCentered = [this](const char* text, int baseline) {
        oled_.drawStr((DISPLAY_WIDTH - oled_.getStrWidth(text)) / 2, baseline, text);
    };

    drawCentered(TITLE, 12);
    oled_.setFont(u8g2_font_5x8_tf);
    drawCentered(REVISION_AND_RATING, 24);
    oled_.drawHLine(4, 30, 120);
    oled_.setFont(u8g2_font_5x8_tf);
    drawCentered(HARDWARE, 42);
    char firmware[24];
    snprintf(firmware, sizeof(firmware), "Firmware v%s",
             firmwareVersion != nullptr ? firmwareVersion : "?");
    drawCentered(firmware, 52);
    drawCentered(STARTING, 62);
    oled_.sendBuffer();
    invalidateFrame();
}

void OledDisplay::formatCurrent(char* buffer, size_t size, float current, uint8_t decimals)
{
    if (!std::isfinite(current)) {
        snprintf(buffer, size, "--");
        return;
    }

    if (std::fabs(current) < 1.0f) {
        snprintf(buffer, size, "%+.*fmA", static_cast<int>(decimals), current * 1000.0f);
    } else {
        snprintf(buffer, size, "%+.*fA", static_cast<int>(decimals), current);
    }
}

void OledDisplay::formatPower(char* buffer, size_t size, float power, uint8_t decimals)
{
    if (!std::isfinite(power)) {
        snprintf(buffer, size, "--");
        return;
    }

    if (std::fabs(power) < 1.0f) {
        snprintf(buffer, size, "%+.*fmW", static_cast<int>(decimals), power * 1000.0f);
    } else {
        snprintf(buffer, size, "%+.*fW", static_cast<int>(decimals), power);
    }
}

void OledDisplay::formatVoltage(char* buffer, size_t size, float voltage, uint8_t decimals)
{
    if (!std::isfinite(voltage)) {
        snprintf(buffer, size, "--");
        return;
    }

    snprintf(buffer, size, "%.*fV", static_cast<int>(decimals), voltage);
}

void OledDisplay::formatTemperature(char* buffer, size_t size, float temperature, uint8_t decimals)
{
    if (!std::isfinite(temperature)) {
        snprintf(buffer, size, "--");
        return;
    }

    snprintf(buffer, size, "%.*f\xB0" "C", static_cast<int>(decimals), temperature);
}

void OledDisplay::formatShuntVoltage(char* buffer, size_t size, float voltage)
{
    if (!std::isfinite(voltage)) {
        snprintf(buffer, size, "--");
        return;
    }

    snprintf(buffer, size, "%+.4fmV", voltage * 1000.0f);
}

void OledDisplay::formatEnergy(
    char* buffer,
    size_t size,
    float value,
    const char* label,
    uint8_t decimals)
{
    if (!std::isfinite(value)) {
        snprintf(buffer, size, "--%s", label);
        return;
    }

    snprintf(buffer, size, "%+.*f%s", static_cast<int>(decimals), value, label);
}

void OledDisplay::nextPage()
{
    page_ = page_ == Page::Live ? Page::Extrema : Page::Live;
    invalidateFrame();
}

void OledDisplay::invalidateFrame()
{
    hasLastFrameHash_ = false;
    frameRefreshRequired_ = true;
}

void OledDisplay::sendBufferIfChanged()
{
    const uint8_t* buffer = oled_.getBufferPtr();
    // U8g2 exposes the full-buffer dimensions even when getBufferSize() is
    // unavailable (it is conditional on dynamic buffer allocation).
    const uint16_t size = static_cast<uint16_t>(oled_.getBufferTileWidth()) *
                          static_cast<uint16_t>(oled_.getBufferTileHeight()) * 8U;
    uint32_t hash = FNV1A_OFFSET_BASIS;

    for (uint16_t index = 0; index < size; ++index) {
        hash ^= buffer[index];
        hash *= FNV1A_PRIME;
    }

    if (frameRefreshRequired_ || !hasLastFrameHash_ || hash != lastFrameHash_) {
        oled_.sendBuffer();
        lastFrameHash_ = hash;
        hasLastFrameHash_ = true;
        frameRefreshRequired_ = false;
    }
}

void OledDisplay::drawPair(int baseline, const char* left, const char* right, int rightStart)
{
    oled_.drawStr(2, baseline, left);
    oled_.drawStr(rightStart, baseline, right);
}

void OledDisplay::showLivePage(
    const Telemetry& telemetry,
    const EnergyTotals& energy,
    bool bleConnected,
    uint8_t wifiClients,
    uint32_t failedSamples,
    bool socKnown,
    float socPercent)
{
    // Failed-sample count isn't shown on this compact header; it's already
    // visible via the Web Dashboard and BLE status.
    (void)failedSamples;
    char left[32];
    char right[32];

    oled_.setFont(u8g2_font_5x8_tf);
    if (socKnown) {
        snprintf(left, sizeof(left), "SoC:%.0f%%", static_cast<double>(socPercent));
    } else {
        snprintf(left, sizeof(left), "SoC:--%%");
    }
    oled_.drawStr(2, 8, left);
    snprintf(
        right,
        sizeof(right),
        "B:%c W:%u",
        bleConnected ? 'C' : '-',
        static_cast<unsigned>(wifiClients)
    );
    // Center this segment in the gap between the SoC text and the page
    // indicator rather than assuming a fixed column, since the SoC text's
    // width varies with the percent's digit count.
    constexpr int PAGE_LABEL_X = 108;
    const int socEndX = 2 + oled_.getStrWidth(left);
    const int gap = PAGE_LABEL_X - socEndX - oled_.getStrWidth(right);
    oled_.drawStr(socEndX + (gap > 0 ? gap / 2 : 0), 8, right);
    oled_.drawStr(PAGE_LABEL_X, 8, "1/2");

    oled_.setFont(u8g2_font_6x12_tf);
    formatCurrent(left, sizeof(left), telemetry.current, 2);
    formatVoltage(right, sizeof(right), telemetry.voltage, 2);
    drawPair(27, left, right, 66);

    formatPower(left, sizeof(left), telemetry.power, 2);
    formatTemperature(right, sizeof(right), telemetry.temperature, 2);
    drawPair(43, left, right, 66);

    formatEnergy(left, sizeof(left), energy.netAh, "Ah", 2);
    formatEnergy(right, sizeof(right), energy.netWh, "Wh", 2);
    drawPair(59, left, right, 66);
}

void OledDisplay::showExtremaPage(const TelemetryStore& store)
{
    oled_.setFont(u8g2_font_5x8_tf);
    constexpr int LABEL_X = 2;
    constexpr int MIN_X = 14;
    constexpr int MAX_X = 72;

    oled_.drawStr(MIN_X, 8, "MIN");
    oled_.drawStr(MAX_X, 8, "MAX");
    oled_.drawStr(108, 8, "2/2");

    char minimum[32];
    char maximum[32];

    formatVoltage(minimum, sizeof(minimum), store.voltageStats().minimum);
    formatVoltage(maximum, sizeof(maximum), store.voltageStats().maximum);
    oled_.drawStr(LABEL_X, 18, "V");
    oled_.drawStr(MIN_X, 18, minimum);
    oled_.drawStr(MAX_X, 18, maximum);

    formatCurrent(minimum, sizeof(minimum), store.currentStats().minimum);
    formatCurrent(maximum, sizeof(maximum), store.currentStats().maximum);
    oled_.drawStr(LABEL_X, 27, "I");
    oled_.drawStr(MIN_X, 27, minimum);
    oled_.drawStr(MAX_X, 27, maximum);

    formatPower(minimum, sizeof(minimum), store.powerStats().minimum);
    formatPower(maximum, sizeof(maximum), store.powerStats().maximum);
    oled_.drawStr(LABEL_X, 36, "P");
    oled_.drawStr(MIN_X, 36, minimum);
    oled_.drawStr(MAX_X, 36, maximum);

    formatTemperature(minimum, sizeof(minimum), store.temperatureStats().minimum);
    formatTemperature(maximum, sizeof(maximum), store.temperatureStats().maximum);
    oled_.drawStr(LABEL_X, 45, "T");
    oled_.drawStr(MIN_X, 45, minimum);
    oled_.drawStr(MAX_X, 45, maximum);

    formatShuntVoltage(minimum, sizeof(minimum), store.shuntStats().minimum);
    formatShuntVoltage(maximum, sizeof(maximum), store.shuntStats().maximum);
    oled_.drawStr(LABEL_X, 54, "S");
    oled_.drawStr(MIN_X, 54, minimum);
    oled_.drawStr(MAX_X, 54, maximum);
    oled_.drawStr(2, 63, "Hold: OLED power");
}

void OledDisplay::showMeasurements(
    const TelemetryStore& store,
    const EnergyTotals& energy,
    bool bleConnected,
    uint8_t wifiClients,
    uint32_t failedSamples,
    bool socKnown,
    float socPercent)
{
    if (!on_) {
        return;
    }

    const Telemetry& telemetry = store.current();

    oled_.clearBuffer();
    oled_.setFont(u8g2_font_6x12_tf);

    if (page_ == Page::Live) {
        showLivePage(telemetry, energy, bleConnected, wifiClients, failedSamples, socKnown, socPercent);
    } else {
        showExtremaPage(store);
    }

    sendBufferIfChanged();
}
