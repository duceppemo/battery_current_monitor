#include "web/WebDashboard.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>

#include "AppConfig.h"
#include "web/DashboardHtml.h"

namespace
{
    constexpr uint32_t ACCESS_POINT_HEALTH_CHECK_MS = 5000;
    constexpr uint32_t STATION_CONNECT_TIMEOUT_MS = 15000;
    constexpr uint32_t STATION_RETRY_INTERVAL_MS = 30000;

    bool parseFiniteFloat(WebServer& server, const char* name, float& value)
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

    bool parseWifiSettings(WebServer& server, WifiStationSettings& settings)
    {
        if (!server.hasArg("ssid") || !server.hasArg("password")) return false;
        const String ssid = server.arg("ssid");
        const String password = server.arg("password");
        if (ssid.length() >= sizeof(settings.ssid) || password.length() >= sizeof(settings.password)) {
            return false;
        }
        snprintf(settings.ssid, sizeof(settings.ssid), "%s", ssid.c_str());
        snprintf(settings.password, sizeof(settings.password), "%s", password.c_str());
        return WifiSettings::isValid(settings);
    }

    bool parseMqttSettings(WebServer& server, MqttBrokerSettings& settings)
    {
        if (!server.hasArg("host") || !server.hasArg("port")) return false;
        const String host = server.arg("host");
        const String username = server.arg("username");
        const String password = server.arg("password");
        const String portText = server.arg("port");
        if (host.length() >= sizeof(settings.host) ||
            username.length() >= sizeof(settings.username) ||
            password.length() >= sizeof(settings.password)) {
            return false;
        }

        char* end = nullptr;
        const long port = strtol(portText.c_str(), &end, 10);
        if (end == portText.c_str() || *end != '\0' || port <= 0 || port > 65535) return false;

        settings.enabled = server.hasArg("enabled") && server.arg("enabled") == "1";
        settings.port = static_cast<uint16_t>(port);
        snprintf(settings.host, sizeof(settings.host), "%s", host.c_str());
        snprintf(settings.username, sizeof(settings.username), "%s", username.c_str());
        snprintf(settings.password, sizeof(settings.password), "%s", password.c_str());
        return MqttSettings::isValid(settings);
    }

    bool parseNtfySettings(WebServer& server, NtfyConfig& settings)
    {
        if (!server.hasArg("server") || !server.hasArg("topic")) return false;
        const String serverUrl = server.arg("server");
        const String topic = server.arg("topic");
        if (serverUrl.length() >= sizeof(settings.server) ||
            topic.length() >= sizeof(settings.topic)) {
            return false;
        }

        settings.enabled = server.hasArg("enabled") && server.arg("enabled") == "1";
        snprintf(settings.server, sizeof(settings.server), "%s", serverUrl.c_str());
        snprintf(settings.topic, sizeof(settings.topic), "%s", topic.c_str());
        return NtfySettings::isValid(settings);
    }

    bool parseDeviceNameSettings(WebServer& server, DeviceNameConfig& settings)
    {
        if (!server.hasArg("name")) return false;
        const String name = server.arg("name");
        if (name.length() >= sizeof(settings.name)) return false;
        snprintf(settings.name, sizeof(settings.name), "%s", name.c_str());
        return DeviceNameSettings::isValid(settings);
    }

    bool parseLoadProtectionSettings(WebServer& server, LoadProtectionConfig& settings)
    {
        if (!parseFiniteFloat(server, "lowVoltage", settings.lowVoltageThreshold) ||
            !parseFiniteFloat(server, "lowSocPercent", settings.lowSocPercentThreshold)) {
            return false;
        }
        settings.enabled = server.hasArg("enabled") && server.arg("enabled") == "1";
        return LoadProtectionSettings::isValid(settings);
    }

    void parseEnergyPersistenceSettings(WebServer& server, EnergyPersistenceConfig& settings)
    {
        settings.enabled = server.hasArg("enabled") && server.arg("enabled") == "1";
    }
}

void WebDashboard::begin(
    TelemetryStore& store,
    const EnergyTotals& energyTotals,
    const Ina228Sensor& sensor,
    const CalibrationSettings& calibration,
    const AlarmSettings& alarms,
    const AlarmMonitor& alarmMonitor,
    FirmwareUpdateService& firmwareUpdate,
    const BatteryProfile& batteryProfile,
    const StateOfChargeEstimator& stateOfCharge,
    const MqttSettings& mqttSettings,
    MqttPublisher& mqttPublisher,
    const LoadProtectionSettings& loadProtectionSettings,
    LoadProtectionMonitor& loadProtectionMonitor,
    const EnergyPersistenceSettings& energyPersistenceSettings,
    const NtfySettings& ntfySettings,
    const DeviceNameSettings& deviceNameSettings)
{
    store_ = &store;
    energyTotals_ = &energyTotals;
    sensor_ = &sensor;
    calibration_ = &calibration;
    alarms_ = &alarms;
    alarmMonitor_ = &alarmMonitor;
    firmwareUpdate_ = &firmwareUpdate;
    batteryProfile_ = &batteryProfile;
    stateOfCharge_ = &stateOfCharge;
    mqttSettings_ = &mqttSettings;
    mqttPublisher_ = &mqttPublisher;
    loadProtectionSettings_ = &loadProtectionSettings;
    loadProtectionMonitor_ = &loadProtectionMonitor;
    energyPersistenceSettings_ = &energyPersistenceSettings;
    ntfySettings_ = &ntfySettings;
    deviceNameSettings_ = &deviceNameSettings;
    telemetryJson_.reserve(1400);

    WiFi.persistent(false);
    wifiSettings_.begin();
    startAccessPoint();
    if (wifiSettings_.configured()) startStation(millis());

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/telemetry", HTTP_GET, [this]() { handleTelemetry(); });
    // Every state-changing route goes through authorizeMutation() first;
    // see that method for the cross-site request forgery reasoning.
    auto post = [this](const char* path, void (WebDashboard::*handler)()) {
        server_.on(path, HTTP_POST, [this, handler]() {
            if (!authorizeMutation()) return;
            (this->*handler)();
        });
    };
    post("/api/reset-extrema", &WebDashboard::handleResetExtrema);
    post("/api/reset-session", &WebDashboard::handleResetSession);
    post("/api/toggle-display", &WebDashboard::handleToggleDisplay);
    post("/api/calibration/save", &WebDashboard::handleCalibrationSave);
    post("/api/calibration/reset", &WebDashboard::handleCalibrationReset);
    post("/api/alarms/save", &WebDashboard::handleAlarmSave);
    post("/api/wifi/save", &WebDashboard::handleWifiSave);
    post("/api/wifi/clear", &WebDashboard::handleWifiClear);
    post("/api/battery/save", &WebDashboard::handleBatterySave);
    post("/api/battery/sync", &WebDashboard::handleBatterySync);
    post("/api/battery/reset-history", &WebDashboard::handleBatteryHistoryReset);
    post("/api/mqtt/save", &WebDashboard::handleMqttSave);
    post("/api/ntfy/save", &WebDashboard::handleNtfySave);
    post("/api/device-name/save", &WebDashboard::handleDeviceNameSave);
    post("/api/protection/save", &WebDashboard::handleLoadProtectionSave);
    post("/api/protection/reconnect", &WebDashboard::handleLoadProtectionReconnect);
    post("/api/protection/test-disconnect", &WebDashboard::handleLoadProtectionTestDisconnect);
    post("/api/protection/test-connect", &WebDashboard::handleLoadProtectionTestConnect);
    post("/api/energy-persistence/save", &WebDashboard::handleEnergyPersistenceSave);
    server_.on("/api/firmware", HTTP_POST,
        [this]() {
            if (!authorizeMutation()) return;
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
    // WebServer only retains headers it was told to collect.
    static const char* COLLECTED_HEADERS[] = {"X-Requested-With"};
    server_.collectHeaders(COLLECTED_HEADERS, 1);
    server_.begin();

    webSocket_.begin();
    webSocket_.onEvent([this](uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
        (void)payload;
        (void)length;
        if (type != WStype_CONNECTED) return;
        // Send an immediate snapshot so a newly connected client doesn't
        // wait up to one broadcast interval for its first update.
        if (buildTelemetryJson(telemetryJson_)) webSocket_.sendTXT(clientId, telemetryJson_);
    });

    running_ = true;
}

bool WebDashboard::startAccessPoint()
{
    // Boot in single-radio AP mode unless a station is actually going to be
    // used. WIFI_AP_STA draws more current during Wi-Fi/BLE bring-up, and on
    // a cold power-on that extra inrush has been observed to glitch the
    // SSD1309 (no hardware reset line) even though the ESP32 itself and the
    // INA228 tolerate it fine.
    WiFi.mode(wifiSettings_.configured() ? WIFI_AP_STA : WIFI_AP);
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
        webSocket_.loop();
        maintainAccessPoint(millis());
        maintainStation(millis());
        broadcastTelemetryIfDue(millis());
        if (restartAfterMs_ != 0 && static_cast<int32_t>(millis() - restartAfterMs_) >= 0) {
            ESP.restart();
        }
    }
}

void WebDashboard::startStation(uint32_t nowMs)
{
    if (!wifiSettings_.configured()) return;

    if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
    }

    const WifiStationSettings& settings = wifiSettings_.current();
    if (mdnsReady_) {
        MDNS.end();
        mdnsReady_ = false;
    }
    stationConnected_ = false;
    stationAttemptStartedMs_ = nowMs;
    lastStationAttemptMs_ = nowMs;
    WiFi.disconnect(false, false);
    WiFi.begin(settings.ssid, settings.password);
    Serial.printf("Wi-Fi station connecting to: %s\n", settings.ssid);
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

bool WebDashboard::consumeBatteryProfileSaveRequested(BatteryProfileSettings& settings)
{
    if (!consumeCommand(PendingCommand::SaveBatteryProfile)) return false;
    settings = pendingBatteryProfile_;
    return true;
}

bool WebDashboard::consumeBatterySyncRequested()
{
    return consumeCommand(PendingCommand::SyncBatteryFull);
}

bool WebDashboard::consumeBatteryHistoryResetRequested()
{
    return consumeCommand(PendingCommand::ResetBatteryHistory);
}

bool WebDashboard::consumeMqttSettingsSaveRequested(MqttBrokerSettings& settings)
{
    if (!consumeCommand(PendingCommand::SaveMqttSettings)) return false;
    settings = pendingMqttSettings_;
    return true;
}

bool WebDashboard::consumeNtfySettingsSaveRequested(NtfyConfig& settings)
{
    if (!consumeCommand(PendingCommand::SaveNtfySettings)) return false;
    settings = pendingNtfySettings_;
    return true;
}

bool WebDashboard::consumeDeviceNameSaveRequested(DeviceNameConfig& settings)
{
    if (!consumeCommand(PendingCommand::SaveDeviceName)) return false;
    settings = pendingDeviceName_;
    return true;
}

bool WebDashboard::consumeLoadProtectionSaveRequested(LoadProtectionConfig& settings)
{
    if (!consumeCommand(PendingCommand::SaveLoadProtection)) return false;
    settings = pendingLoadProtection_;
    return true;
}

bool WebDashboard::consumeLoadProtectionReconnectRequested()
{
    return consumeCommand(PendingCommand::ReconnectLoad);
}

bool WebDashboard::consumeLoadProtectionTestDisconnectRequested()
{
    return consumeCommand(PendingCommand::TestDisconnectLoad);
}

bool WebDashboard::consumeLoadProtectionTestConnectRequested()
{
    return consumeCommand(PendingCommand::TestConnectLoad);
}

bool WebDashboard::consumeEnergyPersistenceSaveRequested(EnergyPersistenceConfig& settings)
{
    if (!consumeCommand(PendingCommand::SaveEnergyPersistence)) return false;
    settings = pendingEnergyPersistence_;
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

bool WebDashboard::mutationAuthorized()
{
    // A browser only attaches a custom header to a same-origin script
    // request; a cross-origin one triggers a CORS preflight this server
    // never answers. So a form, image or script on any other page the
    // operator's browser visits (on the same LAN or not) cannot drive
    // these endpoints -- only this dashboard's own JavaScript can.
    return server_.header("X-Requested-With") == "BatteryMonitor";
}

bool WebDashboard::authorizeMutation()
{
    if (mutationAuthorized()) return true;
    server_.send(403, "application/json",
        "{\"error\":\"missing X-Requested-With: BatteryMonitor header\"}");
    return false;
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

    const wifi_mode_t mode = WiFi.getMode();
    const bool modeIsAccessPoint = mode == WIFI_AP || mode == WIFI_AP_STA;
    const bool hasAccessPointIp = WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
    accessPointReady_ = modeIsAccessPoint && hasAccessPointIp;

    if (!accessPointReady_) {
        Serial.println("WARNING: Wi-Fi AP unavailable; restarting it.");
        WiFi.softAPdisconnect(true);
        startAccessPoint();
    }
}

void WebDashboard::maintainStation(uint32_t nowMs)
{
    if (!wifiSettings_.configured()) {
        stationConnected_ = false;
        if (mdnsReady_) {
            MDNS.end();
            mdnsReady_ = false;
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!stationConnected_) {
            stationConnected_ = true;
            Serial.printf("Wi-Fi station IP: %s\n", WiFi.localIP().toString().c_str());
        }
        if (!mdnsReady_) {
            mdnsReady_ = MDNS.begin(Config::WIFI_MDNS_HOSTNAME);
            if (mdnsReady_) {
                MDNS.addService("http", "tcp", 80);
                Serial.printf("mDNS: http://%s.local/\n", Config::WIFI_MDNS_HOSTNAME);
            } else {
                Serial.println("WARNING: mDNS start failed.");
            }
        }
        return;
    }

    if (stationConnected_) {
        stationConnected_ = false;
        if (mdnsReady_) {
            MDNS.end();
            mdnsReady_ = false;
        }
        Serial.println("WARNING: Wi-Fi station disconnected; AP fallback remains available.");
    }

    const bool attemptTimedOut = stationAttemptStartedMs_ != 0 &&
        (nowMs - stationAttemptStartedMs_) >= STATION_CONNECT_TIMEOUT_MS;
    if ((attemptTimedOut && (nowMs - lastStationAttemptMs_) >= STATION_RETRY_INTERVAL_MS) ||
        (stationAttemptStartedMs_ == 0 && (nowMs - lastStationAttemptMs_) >= STATION_RETRY_INTERVAL_MS)) {
        startStation(nowMs);
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

void WebDashboard::appendJsonString(String& json, const char* value)
{
    json += '"';
    if (value != nullptr) {
        for (const char* character = value; *character != '\0'; ++character) {
            switch (*character) {
            case '"': json += "\\\""; break;
            case '\\': json += "\\\\"; break;
            case '\n': json += "\\n"; break;
            case '\r': json += "\\r"; break;
            case '\t': json += "\\t"; break;
            default:
                if (static_cast<unsigned char>(*character) >= 0x20) json += *character;
                break;
            }
        }
    }
    json += '"';
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

bool WebDashboard::buildTelemetryJson(String& json)
{
    if (store_ == nullptr || energyTotals_ == nullptr || sensor_ == nullptr ||
        calibration_ == nullptr || alarms_ == nullptr || alarmMonitor_ == nullptr ||
        batteryProfile_ == nullptr || stateOfCharge_ == nullptr ||
        mqttSettings_ == nullptr || mqttPublisher_ == nullptr ||
        loadProtectionSettings_ == nullptr || loadProtectionMonitor_ == nullptr ||
        energyPersistenceSettings_ == nullptr || ntfySettings_ == nullptr ||
        deviceNameSettings_ == nullptr) {
        return false;
    }

    const Telemetry& t = store_->current();

    char deviceNameBuffer[33];
    DeviceNameSettings::computeEffectiveName(
        deviceNameSettings_->current(), deviceNameBuffer, sizeof(deviceNameBuffer));

    json = "{\"firmwareVersion\":\"";
    json += Config::FIRMWARE_VERSION;
    json += "\",\"firmwareImageMarker\":\"";
    json += Config::FIRMWARE_IMAGE_MARKER;
    json += "\",\"deviceName\":";
    appendJsonString(json, deviceNameBuffer);
    json += ",\"deviceNameCustomized\":";
    json += deviceNameSettings_->current().name[0] != '\0' ? "true" : "false";
    json += ",";

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
    json += ",\"persistEnabled\":";
    json += energyPersistenceSettings_->current().enabled ? "true" : "false";
    json += "}";

    const BatteryProfileSettings& batteryProfile = batteryProfile_->current();
    json += ",\"battery\":{\"capacityAh\":";
    appendNullableFloat(json, true, batteryProfile.capacityAh, 3);
    json += ",\"chargedVoltage\":";
    appendNullableFloat(json, true, batteryProfile.chargedVoltage, 3);
    json += ",\"known\":";
    json += stateOfCharge_->known() ? "true" : "false";
    json += ",\"percent\":";
    appendNullableFloat(json, stateOfCharge_->known(), stateOfCharge_->percent(batteryProfile), 1);
    json += ",\"remainingAh\":";
    appendNullableFloat(json, stateOfCharge_->known(), stateOfCharge_->remainingAh(), 3);
    json += ",\"hasTimeToEmpty\":";
    json += stateOfCharge_->hasTimeToEmpty() ? "true" : "false";
    json += ",\"timeToEmptySeconds\":";
    appendUnsigned(json, stateOfCharge_->hasTimeToEmpty() ? stateOfCharge_->timeToEmptySeconds() : 0);
    json += ",\"deepestDischargePercent\":";
    appendNullableFloat(json, true, stateOfCharge_->deepestDischargePercent(), 1);
    json += ",\"fullChargeCycles\":";
    appendUnsigned(json, stateOfCharge_->fullChargeCycles());
    json += ",\"averageDischargeDepthPercent\":";
    appendNullableFloat(json, stateOfCharge_->fullChargeCycles() > 0,
                         stateOfCharge_->averageDischargeDepthPercent(), 1);
    json += "}";

    const MqttBrokerSettings& mqtt = mqttSettings_->current();
    json += ",\"mqtt\":{\"enabled\":";
    json += mqtt.enabled ? "true" : "false";
    json += ",\"connected\":";
    json += mqttPublisher_->connected() ? "true" : "false";
    json += ",\"host\":";
    appendJsonString(json, mqtt.host);
    json += ",\"port\":";
    appendUnsigned(json, mqtt.port);
    json += ",\"username\":";
    appendJsonString(json, mqtt.username);
    json += "}";

    const NtfyConfig& ntfy = ntfySettings_->current();
    json += ",\"ntfy\":{\"enabled\":";
    json += ntfy.enabled ? "true" : "false";
    json += ",\"server\":";
    appendJsonString(json, ntfy.server);
    json += ",\"topic\":";
    appendJsonString(json, ntfy.topic);
    json += "}";

    const LoadProtectionConfig& protection = loadProtectionSettings_->current();
    json += ",\"protection\":{\"enabled\":";
    json += protection.enabled ? "true" : "false";
    json += ",\"lowVoltageThreshold\":";
    appendNullableFloat(json, true, protection.lowVoltageThreshold, 3);
    json += ",\"lowSocPercentThreshold\":";
    appendNullableFloat(json, true, protection.lowSocPercentThreshold, 1);
    json += ",\"relayEngaged\":";
    json += loadProtectionMonitor_->relayEngaged() ? "true" : "false";
    json += ",\"tripped\":";
    json += loadProtectionMonitor_->tripped() ? "true" : "false";
    json += ",\"tripFlags\":";
    appendUnsigned(json, loadProtectionMonitor_->tripFlags());
    json += ",\"breachFlags\":";
    appendUnsigned(json, LoadProtectionMonitor::evaluateBreach(protection, t, *stateOfCharge_, batteryProfile));
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
    const WifiStationSettings& wifi = wifiSettings_.current();
    json += ",\"wifi\":{\"accessPointSsid\":";
    appendJsonString(json, Config::WIFI_AP_SSID);
    json += ",\"accessPointIp\":";
    appendJsonString(json, WiFi.softAPIP().toString().c_str());
    json += ",\"stationConfigured\":";
    json += wifiSettings_.configured() ? "true" : "false";
    json += ",\"stationConnected\":";
    json += stationConnected_ ? "true" : "false";
    json += ",\"stationSsid\":";
    appendJsonString(json, wifi.ssid);
    json += ",\"stationIp\":";
    appendJsonString(json, stationConnected_ ? WiFi.localIP().toString().c_str() : "");
    json += ",\"mdnsHostname\":";
    appendJsonString(json, mdnsReady_ ? Config::WIFI_MDNS_HOSTNAME : "");
    json += "}";
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

    return true;
}

void WebDashboard::handleTelemetry()
{
    if (!buildTelemetryJson(telemetryJson_)) {
        server_.send(503, "application/json", "{\"error\":\"telemetry unavailable\"}");
        return;
    }

    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", telemetryJson_);
}

void WebDashboard::broadcastTelemetryIfDue(uint32_t nowMs)
{
    if (webSocket_.connectedClients() == 0) return;
    if (nowMs - lastWebSocketBroadcastMs_ < Config::MEASUREMENT_INTERVAL_MS) return;
    lastWebSocketBroadcastMs_ = nowMs;

    // telemetryJson_ is reserve()d once in begin(); reusing it here avoids a
    // fresh ~1.4 KB allocation twice a second for the life of the device.
    if (buildTelemetryJson(telemetryJson_)) webSocket_.broadcastTXT(telemetryJson_);
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
    // Range-check before narrowing: float -> uint8_t is undefined for
    // negative or out-of-range values.
    if (flagsValue < 0.0f || flagsValue > 31.0f || flagsValue != floorf(flagsValue)) {
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

void WebDashboard::handleBatterySave()
{
    BatteryProfileSettings requested;
    if (!parseFiniteFloat(server_, "capacity", requested.capacityAh) ||
        !parseFiniteFloat(server_, "chargedVoltage", requested.chargedVoltage)) {
        server_.send(400, "application/json", "{\"error\":\"invalid battery profile\"}"); return;
    }
    if (!BatteryProfile::isValid(requested) || !queueCommand(PendingCommand::SaveBatteryProfile)) {
        server_.send(400, "application/json", "{\"error\":\"battery profile rejected\"}"); return;
    }
    pendingBatteryProfile_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleBatterySync()
{
    if (!queueCommand(PendingCommand::SyncBatteryFull)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleBatteryHistoryReset()
{
    if (!queueCommand(PendingCommand::ResetBatteryHistory)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleMqttSave()
{
    MqttBrokerSettings requested;
    if (!parseMqttSettings(server_, requested) || !queueCommand(PendingCommand::SaveMqttSettings)) {
        server_.send(400, "application/json", "{\"error\":\"invalid MQTT settings\"}");
        return;
    }
    pendingMqttSettings_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleNtfySave()
{
    NtfyConfig requested;
    if (!parseNtfySettings(server_, requested) || !queueCommand(PendingCommand::SaveNtfySettings)) {
        server_.send(400, "application/json", "{\"error\":\"invalid ntfy settings\"}");
        return;
    }
    pendingNtfySettings_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleDeviceNameSave()
{
    DeviceNameConfig requested;
    if (!parseDeviceNameSettings(server_, requested) || !queueCommand(PendingCommand::SaveDeviceName)) {
        server_.send(400, "application/json", "{\"error\":\"invalid device name\"}");
        return;
    }
    pendingDeviceName_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleLoadProtectionSave()
{
    LoadProtectionConfig requested;
    if (!parseLoadProtectionSettings(server_, requested) || !queueCommand(PendingCommand::SaveLoadProtection)) {
        server_.send(400, "application/json", "{\"error\":\"invalid protection settings\"}");
        return;
    }
    pendingLoadProtection_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleLoadProtectionReconnect()
{
    if (!queueCommand(PendingCommand::ReconnectLoad)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleLoadProtectionTestDisconnect()
{
    if (!queueCommand(PendingCommand::TestDisconnectLoad)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleLoadProtectionTestConnect()
{
    if (!queueCommand(PendingCommand::TestConnectLoad)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleEnergyPersistenceSave()
{
    EnergyPersistenceConfig requested;
    parseEnergyPersistenceSettings(server_, requested);
    if (!queueCommand(PendingCommand::SaveEnergyPersistence)) {
        server_.send(409, "application/json", "{\"error\":\"command pending\"}");
        return;
    }
    pendingEnergyPersistence_ = requested;
    server_.send(202, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleWifiSave()
{
    WifiStationSettings requested;
    if (!parseWifiSettings(server_, requested)) {
        server_.send(400, "application/json", "{\"error\":\"invalid Wi-Fi settings\"}");
        return;
    }
    if (!saveWifiSettings(requested)) {
        server_.send(500, "application/json", "{\"error\":\"Wi-Fi settings were not saved\"}");
        return;
    }

    server_.send(202, "application/json", "{\"ok\":true,\"message\":\"station connecting; AP remains available\"}");
}

void WebDashboard::handleWifiClear()
{
    if (!clearWifiSettings()) {
        server_.send(500, "application/json", "{\"error\":\"Wi-Fi settings were not cleared\"}");
        return;
    }

    server_.send(200, "application/json", "{\"ok\":true}");
}

bool WebDashboard::saveWifiSettings(const WifiStationSettings& settings)
{
    if (!wifiSettings_.save(settings)) {
        return false;
    }

    startStation(millis());
    return true;
}

bool WebDashboard::clearWifiSettings()
{
    if (!wifiSettings_.clear()) {
        return false;
    }

    if (mdnsReady_) {
        MDNS.end();
        mdnsReady_ = false;
    }
    WiFi.disconnect(false, false);
    stationConnected_ = false;
    stationAttemptStartedMs_ = 0;
    Serial.println("Wi-Fi station credentials cleared; AP fallback remains available.");
    return true;
}

void WebDashboard::stationIpOctets(uint8_t octets[4]) const
{
    if (!stationConnected_) {
        memset(octets, 0, 4);
        return;
    }

    const IPAddress ip = WiFi.localIP();
    octets[0] = ip[0];
    octets[1] = ip[1];
    octets[2] = ip[2];
    octets[3] = ip[3];
}

void WebDashboard::handleFirmwareUpload()
{
    HTTPUpload& upload = server_.upload();

    // The dashboard sends the detached 64-byte signature as its own part,
    // ahead of the image, so it is already in hand when the image starts.
    if (upload.name == "signature") {
        if (upload.status == UPLOAD_FILE_START) {
            webSignatureLength_ = 0;
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            const size_t room = webSignatureLength_ <= sizeof(webSignature_)
                ? sizeof(webSignature_) - webSignatureLength_ : 0;
            const size_t take = upload.currentSize < room ? upload.currentSize : room;
            memcpy(webSignature_ + webSignatureLength_, upload.buf, take);
            // Anything past 64 bytes is not a valid raw r||s signature; mark
            // the length invalid so the image part is refused.
            webSignatureLength_ = take < upload.currentSize
                ? sizeof(webSignature_) + 1 : webSignatureLength_ + take;
        }
        return;
    }

    switch (upload.status) {
    case UPLOAD_FILE_START:
        firmwareUpdateSucceeded_ = false;
        firmwareUpdateError_[0] = '\0';
        if (!mutationAuthorized()) {
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "unauthorized");
        } else if (webSignatureLength_ != sizeof(webSignature_)) {
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "signature (.sig) file missing");
        } else if (!upload.filename.endsWith(".bin") || firmwareUpdate_ == nullptr ||
            !firmwareUpdate_->beginWebUpdate()) {
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "update start rejected");
        } else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "update start rejected");
        } else {
            mbedtls_sha256_init(&webSha256_);
            mbedtls_sha256_starts_ret(&webSha256_, 0);
            webSha256Active_ = true;
        }
        break;
    case UPLOAD_FILE_WRITE:
        if (firmwareUpdateError_[0] != '\0') break;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.abort();
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "firmware write failed");
            break;
        }
        mbedtls_sha256_update_ret(&webSha256_, upload.buf, upload.currentSize);
        break;
    case UPLOAD_FILE_END: {
        if (firmwareUpdateError_[0] != '\0') break;
        uint8_t digest[32];
        mbedtls_sha256_finish_ret(&webSha256_, digest);
        mbedtls_sha256_free(&webSha256_);
        webSha256Active_ = false;
        // Same rule as the BLE path: verify before Update.end(), which is
        // what marks the new slot bootable; abort() discards the write.
        if (!FirmwareUpdateService::verifyImageSignature(digest, webSignature_)) {
            Update.abort();
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "firmware signature verification failed");
        } else if (Update.end(true)) {
            firmwareUpdateSucceeded_ = true;
        } else {
            Update.abort();
            firmwareUpdate_->abandonWebUpdate();
            snprintf(firmwareUpdateError_, sizeof(firmwareUpdateError_), "firmware verification failed");
        }
        break;
    }
    case UPLOAD_FILE_ABORTED:
        Update.abort();
        if (firmwareUpdate_ != nullptr) firmwareUpdate_->abandonWebUpdate();
        if (webSha256Active_) {
            mbedtls_sha256_free(&webSha256_);
            webSha256Active_ = false;
        }
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
