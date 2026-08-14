#include "web/WebDashboard.h"

#include <cmath>
#include <cstdlib>

#include <WiFi.h>
#include <Update.h>
#include <esp_system.h>

#include "AppConfig.h"
#include "web/DashboardHtml.h"

namespace
{
    constexpr uint32_t ACCESS_POINT_HEALTH_CHECK_MS = 5000;

    bool parseFiniteFloat(const WebServer& server, const char* name, float& value)
    {
        if (!server.hasArg(name)) {
            return false;
        }

        const String text = server.arg(name);
        char* end = nullptr;
        const float parsed = strtof(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
            return false;
        }

        value = parsed;
        return true;
    }
}

void WebDashboard::begin(
    TelemetryStore& store,
    const EnergyTotals& energyTotals,
    const Ina228Sensor& sensor,
    const CalibrationSettings& calibration,
    const AlarmSettings& alarms,
    const AlarmMonitor& alarmMonitor,
    FirmwareUpdateService& firmwareUpdate)
{
    store_ = &store;
    energyTotals_ = &energyTotals;
    sensor_ = &sensor;
    calibration_ = &calibration;
    alarms_ = &alarms;
    alarmMonitor_ = &alarmMonitor;
    firmwareUpdate_ = &firmwareUpdate;
    telemetryJson_.reserve(1400);

    startAccessPoint();

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/telemetry", HTTP_GET, [this]() { handleTelemetry(); });
    server_.on("/api/reset-extrema", HTTP_POST, [this]() { handleResetExtrema(); });
    server_.on("/api/reset-session", HTTP_POST, [this]() { handleResetSession(); });
    server_.on("/api/toggle-display", HTTP_POST, [this]() { handleToggleDisplay(); });
    server_.on("/api/calibration/save", HTTP_POST, [this]() { handleCalibrationSave(); });
    server_.on("/api/calibration/reset", HTTP_POST, [this]() { handleCalibrationReset(); });
    server_.on("/api/alarms/save", HTTP_POST, [this]() { handleAlarmSave(); });
    server_.on("/api/firmware", HTTP_POST,
        [this]() {
            if (firmwareUpdateSucceeded_) {
                server_.send(200, "application/json", "{\"ok\":true,\"message\":\"Firmware written; restarting\"}");
                restartAfterMs_ = millis() + 750;
            } else {
                String response = "{\"error\":\"";
                response += firmwareUpdateError_[0] ? firmwareUpdateError_ : "firmware update failed";
                response += "\"}";
                server_.send(400, "application/json", response);
            }
        },
        [this]() { handleFirmwareUpload(); });
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
        if (restartAfterMs_ != 0 && static_cast<int32_t>(millis() - restartAfterMs_) >= 0) {
            ESP.restart();
        }
    }
}

bool WebDashboard::consumeDisplayToggleRequested()
{
    return consumeCommand(PendingCommand::ToggleDisplay);
}

bool WebDashboard::consumeSessionResetRequested()
{
    return consumeCommand(PendingCommand::ResetSession);
}

bool WebDashboard::consumeCalibrationSaveRequested(CurrentCalibration& calibration)
{
    if (!consumeCommand(PendingCommand::SaveCalibration)) {
        return false;
    }

    calibration = pendingCalibration_;
    return true;
}

bool WebDashboard::consumeCalibrationResetRequested()
{
    return consumeCommand(PendingCommand::ResetCalibration);
}

bool WebDashboard::consumeAlarmSaveRequested(DeviceAlarmSettings& settings)
{
    if (!consumeCommand(PendingCommand::SaveAlarms)) return false;
    settings = pendingAlarms_;
    return true;
}

void WebDashboard::setCalibrationStatus(const char* status)
{
    snprintf(
        calibrationStatus_,
        sizeof(calibrationStatus_),
        "%s",
        status != nullptr ? status : "unknown"
    );
}

bool WebDashboard::queueCommand(PendingCommand command)
{
    if (pendingCommand_ != PendingCommand::None) {
        return false;
    }

    pendingCommand_ = command;
    return true;
}

bool WebDashboard::consumeCommand(PendingCommand command)
{
    if (pendingCommand_ != command) {
        return false;
    }

    pendingCommand_ = PendingCommand::None;
    return true;
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
    if (store_ == nullptr || energyTotals_ == nullptr || sensor_ == nullptr ||
        calibration_ == nullptr || alarms_ == nullptr || alarmMonitor_ == nullptr) {
        server_.send(503, "application/json", "{\"error\":\"telemetry unavailable\"}");
        return;
    }

    const Telemetry& t = store_->current();

    String& json = telemetryJson_;
    json = "{\"firmwareVersion\":\"";
    json += Config::FIRMWARE_VERSION;
    json += "\",\"firmwareImageMarker\":\"";
    json += Config::FIRMWARE_IMAGE_MARKER;
    json += "\",";

    appendMetric(json, "voltage", t.voltageValid(), t.voltage, store_->voltageStats(), 6);
    json += ",";
    appendMetric(json, "shuntVoltage", t.shuntVoltageValid(), t.shuntVoltage, store_->shuntStats(), 9);
    json += ",";
    appendMetric(json, "current", t.currentValid(), t.current, store_->currentStats(), 6);
    json += ",";
    appendMetric(json, "power", t.powerOK(), t.power, store_->powerStats(), 6);
    json += ",";
    appendMetric(json, "temperature", t.temperatureValid(), t.temperature, store_->temperatureStats(), 3);

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

    const Ina228ConfigurationStatus& configuration = sensor_->configuration();
    const CurrentCalibration& calibration = calibration_->current();
    json += ",\"measurement\":{\"ina228Configured\":";
    json += configuration.configured ? "true" : "false";
    json += ",\"ina228ReadbackValid\":";
    json += configuration.readbackValid ? "true" : "false";
    json += ",\"wideShuntRange\":";
    json += configuration.wideShuntRange ? "true" : "false";
    json += ",\"conversionTimeUs\":";
    appendUnsigned(json, configuration.conversionTimeUs);
    json += ",\"averages\":";
    appendUnsigned(json, configuration.averages);
    json += ",\"configRegister\":";
    appendUnsigned(json, configuration.configRegister);
    json += ",\"adcConfigRegister\":";
    appendUnsigned(json, configuration.adcConfigRegister);
    json += ",\"calibration\":{\"stored\":";
    json += calibration_->loadedFromStorage() ? "true" : "false";
    json += ",\"shuntResistanceOhms\":";
    appendNullableFloat(json, true, calibration.shuntResistanceOhms, 7);
    json += ",\"shuntOffsetMicrovolts\":";
    appendNullableFloat(json, true, calibration.shuntOffsetVolts * 1.0e6f, 2);
    json += ",\"currentGain\":";
    appendNullableFloat(json, true, calibration.currentGain, 6);
    json += ",\"status\":\"";
    json += calibrationStatus_;
    json += "\"";
    json += "}}";

    const DeviceAlarmSettings& alarms = alarms_->current();
    json += ",\"alarms\":{\"enabledFlags\":";
    appendUnsigned(json, (alarms.lowVoltageEnabled ? 1 : 0) | (alarms.highVoltageEnabled ? 2 : 0) | (alarms.currentEnabled ? 4 : 0) | (alarms.temperatureEnabled ? 8 : 0) | (alarms.sensorHealthEnabled ? 16 : 0));
    json += ",\"activeFlags\":";
    appendUnsigned(json, alarmMonitor_->state().activeFlags);
    json += ",\"lowVoltage\":"; appendNullableFloat(json, true, alarms.lowVoltage, 3);
    json += ",\"highVoltage\":"; appendNullableFloat(json, true, alarms.highVoltage, 3);
    json += ",\"maxCurrent\":"; appendNullableFloat(json, true, alarms.maxAbsoluteCurrent, 3);
    json += ",\"maxTemperature\":"; appendNullableFloat(json, true, alarms.maxTemperature, 1);
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
    if (!queueCommand(PendingCommand::ResetSession)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }

    Serial.println("Energy session reset requested from web UI.");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleToggleDisplay()
{
    if (!queueCommand(PendingCommand::ToggleDisplay)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }

    Serial.println("Display toggle requested from web UI.");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleCalibrationSave()
{
    CurrentCalibration requested;
    if (!parseFiniteFloat(server_, "resistance", requested.shuntResistanceOhms) ||
        !parseFiniteFloat(server_, "offset", requested.shuntOffsetVolts) ||
        !parseFiniteFloat(server_, "gain", requested.currentGain) ||
        !CalibrationSettings::isValid(requested)) {
        server_.send(400, "application/json", "{\"error\":\"invalid calibration\"}");
        return;
    }

    if (!queueCommand(PendingCommand::SaveCalibration)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }

    pendingCalibration_ = requested;
    setCalibrationStatus("save pending");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleCalibrationReset()
{
    if (!queueCommand(PendingCommand::ResetCalibration)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }

    setCalibrationStatus("reset pending");
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleAlarmSave()
{
    DeviceAlarmSettings requested;
    float flagsValue = 0.0f;
    if (!parseFiniteFloat(server_, "flags", flagsValue) || !parseFiniteFloat(server_, "low", requested.lowVoltage) ||
        !parseFiniteFloat(server_, "high", requested.highVoltage) || !parseFiniteFloat(server_, "current", requested.maxAbsoluteCurrent) ||
        !parseFiniteFloat(server_, "temperature", requested.maxTemperature)) {
        server_.send(400, "application/json", "{\"error\":\"invalid alarms\"}"); return;
    }
    const uint8_t flags = static_cast<uint8_t>(flagsValue);
    requested.lowVoltageEnabled = (flags & 1) != 0;
    requested.highVoltageEnabled = (flags & 2) != 0;
    requested.currentEnabled = (flags & 4) != 0;
    requested.temperatureEnabled = (flags & 8) != 0;
    requested.sensorHealthEnabled = (flags & 16) != 0;
    if (!AlarmSettings::isValid(requested) || !queueCommand(PendingCommand::SaveAlarms)) {
        server_.send(400, "application/json", "{\"error\":\"alarms rejected\"}"); return;
    }
    pendingAlarms_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleFirmwareUpload()
{
    HTTPUpload& upload = server_.upload();
    switch (upload.status) {
    case UPLOAD_FILE_START:
        firmwareUpdateSucceeded_ = false;
        firmwareUpdateError_[0] = '\0';
        if (!upload.filename.endsWith(".bin") || firmwareUpdate_ == nullptr ||
            !firmwareUpdate_->beginWebUpdate()) {
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "update start rejected");
        } else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "update start rejected");
        }
        break;
    case UPLOAD_FILE_WRITE:
        if (firmwareUpdateError_[0] == '\0' &&
            Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.abort();
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "firmware write failed");
        }
        break;
    case UPLOAD_FILE_END:
        if (firmwareUpdateError_[0] == '\0' && Update.end(true)) {
            firmwareUpdateSucceeded_ = true;
        } else if (firmwareUpdateError_[0] == '\0') {
            Update.abort();
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "firmware verification failed");
        }
        break;
    case UPLOAD_FILE_ABORTED:
        Update.abort();
        if (firmwareUpdate_ != nullptr) firmwareUpdate_->abandonWebUpdate();
        snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "upload aborted");
        break;
    default:
        break;
    }
}

void WebDashboard::handleNotFound()
{
    server_.send(404, "text/plain", "Not found");
}
