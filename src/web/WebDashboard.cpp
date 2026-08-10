#include "web/WebDashboard.h"

#include <WiFi.h>

#include "AppConfig.h"
#include "web/DashboardHtml.h"

namespace
{
    constexpr uint32_t ACCESS_POINT_HEALTH_CHECK_MS = 5000;
}

void WebDashboard::begin(TelemetryStore& store, const EnergyTotals& energyTotals)
{
    store_ = &store;
    energyTotals_ = &energyTotals;
    telemetryJson_.reserve(1024);

    startAccessPoint();

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/telemetry", HTTP_GET, [this]() { handleTelemetry(); });
    server_.on("/api/reset-extrema", HTTP_POST, [this]() { handleResetExtrema(); });
    server_.on("/api/reset-session", HTTP_POST, [this]() { handleResetSession(); });
    server_.on("/api/toggle-display", HTTP_POST, [this]() { handleToggleDisplay(); });
    server_.onNotFound([this]() { handleNotFound(); });
    server_.begin();
    running_ = true;
}

bool WebDashboard::startAccessPoint()
{
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(Config::WIFI_AP_SSID, Config::WIFI_AP_PASSWORD)) {
        Serial.println("ERROR: Wi-Fi SoftAP failed.");
        accessPointReady_ = false;
        return false;
    }

    accessPointReady_ = true;

    Serial.printf("Wi-Fi SSID: %s\n", Config::WIFI_AP_SSID);
    Serial.printf("Wi-Fi IP:   %s\n", WiFi.softAPIP().toString().c_str());
    return true;
}

void WebDashboard::update()
{
    if (running_) {
        server_.handleClient();
        maintainAccessPoint(millis());
    }
}

bool WebDashboard::consumeDisplayToggleRequested()
{
    const bool requested = displayToggleRequested_;
    displayToggleRequested_ = false;
    return requested;
}

bool WebDashboard::consumeSessionResetRequested()
{
    const bool requested = sessionResetRequested_;
    sessionResetRequested_ = false;
    return requested;
}

void WebDashboard::maintainAccessPoint(uint32_t nowMs)
{
    if ((nowMs - lastAccessPointCheckMs_) < ACCESS_POINT_HEALTH_CHECK_MS) {
        return;
    }
    lastAccessPointCheckMs_ = nowMs;

    const bool modeIsAccessPoint = WiFi.getMode() == WIFI_AP;
    const bool hasAccessPointIp = WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
    accessPointReady_ = modeIsAccessPoint && hasAccessPointIp;

    if (!accessPointReady_) {
        Serial.println("WARNING: Wi-Fi AP unavailable; restarting it.");
        WiFi.softAPdisconnect(true);
        startAccessPoint();
    }
}

void WebDashboard::setRuntimeStatus(
    bool bleConnected,
    bool bleAdvertising,
    bool displayOn,
    const char* resetReason,
    uint32_t successfulSamples,
    uint32_t failedSamples)
{
    bleConnected_ = bleConnected;
    bleAdvertising_ = bleAdvertising;
    displayOn_ = displayOn;
    resetReason_ = resetReason;
    successfulSamples_ = successfulSamples;
    failedSamples_ = failedSamples;
}

uint8_t WebDashboard::clientCount() const
{
    if (!running_) {
        return 0;
    }
    return static_cast<uint8_t>(WiFi.softAPgetStationNum());
}

void WebDashboard::handleRoot()
{
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html", DASHBOARD_HTML);
}

void WebDashboard::appendNullableFloat(
    String& json,
    bool valid,
    float value,
    uint8_t decimals)
{
    if (valid && std::isfinite(value)) {
        char formatted[32];
        snprintf(
            formatted,
            sizeof(formatted),
            "%.*f",
            static_cast<int>(decimals),
            static_cast<double>(value)
        );
        json += formatted;
    } else {
        json += "null";
    }
}

void WebDashboard::appendUnsigned(String& json, uint32_t value)
{
    char formatted[16];
    snprintf(formatted, sizeof(formatted), "%lu", static_cast<unsigned long>(value));
    json += formatted;
}

void WebDashboard::appendMetric(
    String& json,
    const char* name,
    bool valid,
    float value,
    const MetricStats& stats,
    uint8_t decimals)
{
    json += "\"";
    json += name;
    json += "\":{";
    json += "\"value\":";
    appendNullableFloat(json, valid, value, decimals);
    json += ",\"min\":";
    appendNullableFloat(json, stats.initialized, stats.minimum, decimals);
    json += ",\"max\":";
    appendNullableFloat(json, stats.initialized, stats.maximum, decimals);
    json += "}";
}

void WebDashboard::handleTelemetry()
{
    if (store_ == nullptr || energyTotals_ == nullptr) {
        server_.send(503, "application/json", "{\"error\":\"telemetry unavailable\"}");
        return;
    }

    const Telemetry& t = store_->current();

    String& json = telemetryJson_;
    json = "{";

    appendMetric(json, "voltage", t.voltageOK, t.voltage, store_->voltageStats(), 6);
    json += ",";
    appendMetric(json, "shuntVoltage", t.shuntOK, t.shuntVoltage, store_->shuntStats(), 9);
    json += ",";
    appendMetric(json, "current", t.shuntOK, t.current, store_->currentStats(), 6);
    json += ",";
    appendMetric(json, "power", t.powerOK(), t.power, store_->powerStats(), 6);
    json += ",";
    appendMetric(json, "temperature", t.temperatureOK, t.temperature, store_->temperatureStats(), 3);

    json += ",\"energy\":{\"netAh\":";
    appendNullableFloat(json, true, energyTotals_->netAh, 6);
    json += ",\"netWh\":";
    appendNullableFloat(json, true, energyTotals_->netWh, 6);
    json += ",\"dischargedAh\":";
    appendNullableFloat(json, true, energyTotals_->dischargedAh, 6);
    json += ",\"dischargedWh\":";
    appendNullableFloat(json, true, energyTotals_->dischargedWh, 6);
    json += ",\"chargedAh\":";
    appendNullableFloat(json, true, energyTotals_->chargedAh, 6);
    json += ",\"chargedWh\":";
    appendNullableFloat(json, true, energyTotals_->chargedWh, 6);
    json += "}";

    json += ",\"sensorOK\":";
    json += t.sensorOK() ? "true" : "false";
    json += ",\"bleConnected\":";
    json += bleConnected_ ? "true" : "false";
    json += ",\"bleAdvertising\":";
    json += bleAdvertising_ ? "true" : "false";
    json += ",\"accessPointReady\":";
    json += accessPointReady_ ? "true" : "false";
    json += ",\"resetReason\":\"";
    json += resetReason_;
    json += "\"";
    json += ",\"displayOn\":";
    json += displayOn_ ? "true" : "false";
    json += ",\"wifiClients\":";
    appendUnsigned(json, clientCount());
    json += ",\"sampleSequence\":";
    appendUnsigned(json, t.sequence);
    json += ",\"sampleAgeMs\":";
    appendUnsigned(json, millis() - t.sampledAtMs);
    json += ",\"successfulSamples\":";
    appendUnsigned(json, successfulSamples_);
    json += ",\"failedSamples\":";
    appendUnsigned(json, failedSamples_);
    json += ",\"uptimeSeconds\":";
    appendUnsigned(json, millis() / 1000UL);
    json += "}";

    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", json);
}

void WebDashboard::handleResetExtrema()
{
    if (store_ != nullptr) {
        store_->resetExtrema();
        Serial.println("Min/max statistics reset from web UI.");
    }

    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleResetSession()
{
    sessionResetRequested_ = true;
    Serial.println("Energy session reset requested from web UI.");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleToggleDisplay()
{
    displayToggleRequested_ = true;
    Serial.println("Display toggle requested from web UI.");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleNotFound()
{
    server_.send(404, "text/plain", "Not found");
}
