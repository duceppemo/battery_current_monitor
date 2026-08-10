#include "web/WebDashboard.h"

#include <WiFi.h>

#include "AppConfig.h"
#include "web/DashboardHtml.h"

void WebDashboard::begin(TelemetryStore& store)
{
    store_ = &store;

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(Config::WIFI_AP_SSID, Config::WIFI_AP_PASSWORD)) {
        Serial.println("ERROR: Wi-Fi SoftAP failed.");
        return;
    }

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/telemetry", HTTP_GET, [this]() { handleTelemetry(); });
    server_.on("/api/reset-extrema", HTTP_POST, [this]() { handleResetExtrema(); });
    server_.onNotFound([this]() { handleNotFound(); });
    server_.begin();
    running_ = true;

    Serial.printf("Wi-Fi SSID: %s\n", Config::WIFI_AP_SSID);
    Serial.printf("Wi-Fi IP:   %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("HTTP server started.");
}

void WebDashboard::update()
{
    if (running_) {
        server_.handleClient();
    }
}

void WebDashboard::setRuntimeStatus(
    bool bleConnected,
    bool displayOn,
    uint32_t goodReads,
    uint32_t i2cErrors)
{
    bleConnected_ = bleConnected;
    displayOn_ = displayOn;
    goodReads_ = goodReads;
    i2cErrors_ = i2cErrors;
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
        json += String(static_cast<double>(value), static_cast<unsigned int>(decimals));
    } else {
        json += "null";
    }
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
    if (store_ == nullptr) {
        server_.send(503, "application/json", "{\"error\":\"telemetry unavailable\"}");
        return;
    }

    const Telemetry& t = store_->current();

    String json;
    json.reserve(1024);
    json = "{";

    appendMetric(json, "voltage", t.voltageOK, t.voltage, store_->voltageStats(), 6);
    json += ",";
    appendMetric(json, "shuntVoltage", t.shuntOK, t.shuntVoltage, store_->shuntStats(), 9);
    json += ",";
    appendMetric(json, "current", t.shuntOK, t.current, store_->currentStats(), 6);
    json += ",";
    appendMetric(json, "power", t.voltageOK && t.shuntOK, t.power, store_->powerStats(), 6);
    json += ",";
    appendMetric(json, "temperature", t.temperatureOK, t.temperature, store_->temperatureStats(), 3);

    json += ",\"sensorOK\":";
    json += t.sensorOK() ? "true" : "false";
    json += ",\"bleConnected\":";
    json += bleConnected_ ? "true" : "false";
    json += ",\"displayOn\":";
    json += displayOn_ ? "true" : "false";
    json += ",\"wifiClients\":";
    json += String(clientCount());
    json += ",\"i2cReads\":";
    json += String(goodReads_);
    json += ",\"i2cErrors\":";
    json += String(i2cErrors_);
    json += ",\"uptimeSeconds\":";
    json += String(millis() / 1000UL);
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

void WebDashboard::handleNotFound()
{
    server_.send(404, "text/plain", "Not found");
}
