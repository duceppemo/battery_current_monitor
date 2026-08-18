#include "mqtt/MqttPublisher.h"

#include <Preferences.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>

#include "AppConfig.h"

namespace
{
    constexpr char NS[] = "bm_mqtt";
    constexpr char KEY[] = "settings";

    struct MqttEntityDescriptor
    {
        const char* objectId;
        const char* name;
        const char* deviceClass; // nullptr if none applies
        const char* unit;        // nullptr for a binary_sensor
        const char* valueTemplate;
        bool isBinarySensor;
    };

    // value_template uses single quotes internally so it drops cleanly into
    // the double-quoted JSON string built in publishDiscovery() below.
    constexpr MqttEntityDescriptor ENTITIES[] = {
        {"voltage", "Voltage", "voltage", "V", "{{ value_json.voltage }}", false},
        {"current", "Current", "current", "A", "{{ value_json.current }}", false},
        {"power", "Power", "power", "W", "{{ value_json.power }}", false},
        {"temperature", "Temperature", "temperature", "°" "C", "{{ value_json.temperature }}", false},
        {"net_ah", "Net Charge", nullptr, "Ah", "{{ value_json.net_ah }}", false},
        {"net_wh", "Net Energy", nullptr, "Wh", "{{ value_json.net_wh }}", false},
        {"state_of_charge", "State of Charge", "battery", "%", "{{ value_json.soc_percent }}", false},
        {"time_to_empty", "Time to Empty", nullptr, "min", "{{ value_json.time_to_empty_min }}", false},
        {"sensor_problem", "Sensor Problem", "problem", nullptr,
            "{{ 'OFF' if value_json.sensor_ok else 'ON' }}", true},
        {"alarm_active", "Alarm Active", "problem", nullptr,
            "{{ 'ON' if value_json.alarm_active else 'OFF' }}", true},
    };
}

void MqttSettings::begin()
{
    Preferences p;
    if (!p.begin(NS, true)) return;
    MqttBrokerSettings stored;
    if (p.getBytesLength(KEY) == sizeof(stored) &&
        p.getBytes(KEY, &stored, sizeof(stored)) == sizeof(stored) &&
        isValid(stored)) {
        current_ = stored;
    }
    p.end();
}

bool MqttSettings::save(const MqttBrokerSettings& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(NS, false)) return false;
    const bool saved = p.putBytes(KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool MqttSettings::isValid(const MqttBrokerSettings& settings)
{
    // An empty host is valid (it just means "disabled"); everything else
    // must be well-formed if supplied.
    if (settings.port == 0) return false;
    const size_t hostLength = strnlen(settings.host, sizeof(settings.host));
    const size_t userLength = strnlen(settings.username, sizeof(settings.username));
    const size_t passLength = strnlen(settings.password, sizeof(settings.password));
    if (hostLength >= sizeof(settings.host)) return false;
    if (userLength >= sizeof(settings.username)) return false;
    if (passLength >= sizeof(settings.password)) return false;
    if (settings.enabled && hostLength == 0) return false;
    return true;
}

void MqttPublisher::ensureDeviceId()
{
    if (hasDeviceId_) return;

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    deviceId_ = "battery-monitor-" + mac;
    availabilityTopic_ = "batterymonitor/" + deviceId_ + "/availability";
    stateTopic_ = "batterymonitor/" + deviceId_ + "/state";
    hasDeviceId_ = true;
}

void MqttPublisher::update(
    uint32_t nowMs,
    const MqttBrokerSettings& settings,
    const Telemetry& telemetry,
    const EnergyTotals& energy,
    const BatteryProfileSettings& batteryProfile,
    const StateOfChargeEstimator& stateOfCharge,
    const DeviceAlarmState& alarmState)
{
    if (!settings.enabled || settings.host[0] == '\0') {
        if (client_.connected()) client_.disconnect();
        discoveryPublished_ = false;
        return;
    }

    ensureDeviceId();
    client_.setServer(settings.host, settings.port);
    client_.loop();

    if (!client_.connected()) {
        discoveryPublished_ = false;
        connectIfDue(settings, nowMs);
        return;
    }

    if (!discoveryPublished_) {
        publishDiscovery();
        discoveryPublished_ = true;
    }

    if (nowMs - lastPublishMs_ < Config::MQTT_PUBLISH_INTERVAL_MS) return;
    lastPublishMs_ = nowMs;
    publishState(telemetry, energy, batteryProfile, stateOfCharge, alarmState);
}

void MqttPublisher::connectIfDue(const MqttBrokerSettings& settings, uint32_t nowMs)
{
    if (nowMs - lastConnectAttemptMs_ < Config::MQTT_RECONNECT_INTERVAL_MS) return;
    lastConnectAttemptMs_ = nowMs;

    snprintf(lastHost_, sizeof(lastHost_), "%s", settings.host);
    lastPort_ = settings.port;

    const char* user = settings.username[0] != '\0' ? settings.username : nullptr;
    const char* pass = settings.password[0] != '\0' ? settings.password : nullptr;
    const bool connected = client_.connect(
        deviceId_.c_str(), user, pass,
        availabilityTopic_.c_str(), 0, true, "offline");

    if (connected) {
        client_.publish(availabilityTopic_.c_str(), "online", true);
        Serial.printf("MQTT connected to %s:%u\n", settings.host, settings.port);
    } else {
        Serial.printf("WARNING: MQTT connect to %s:%u failed (state %d).\n",
            settings.host, settings.port, client_.state());
    }
}

void MqttPublisher::publishDiscovery()
{
    char deviceBlock[256];
    snprintf(deviceBlock, sizeof(deviceBlock),
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Battery Monitor\","
        "\"model\":\"XIAO ESP32-C3 + INA228\",\"manufacturer\":\"DIY\",\"sw_version\":\"%s\"}",
        deviceId_.c_str(), Config::FIRMWARE_VERSION);

    for (const auto& entity : ENTITIES) {
        char topic[96];
        snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
            entity.isBinarySensor ? "binary_sensor" : "sensor",
            deviceId_.c_str(), entity.objectId);

        String payload = "{\"name\":\"";
        payload += entity.name;
        payload += "\",\"unique_id\":\"";
        payload += deviceId_;
        payload += "_";
        payload += entity.objectId;
        payload += "\",\"state_topic\":\"";
        payload += stateTopic_;
        payload += "\",\"availability_topic\":\"";
        payload += availabilityTopic_;
        payload += "\",\"value_template\":\"";
        payload += entity.valueTemplate;
        payload += "\"";
        if (entity.deviceClass != nullptr) {
            payload += ",\"device_class\":\"";
            payload += entity.deviceClass;
            payload += "\"";
        }
        if (!entity.isBinarySensor) {
            payload += ",\"unit_of_measurement\":\"";
            payload += entity.unit;
            payload += "\",\"state_class\":\"measurement\"";
        }
        payload += ",";
        payload += deviceBlock;
        payload += "}";

        client_.publish(topic, payload.c_str(), true);
    }
}

void MqttPublisher::publishState(
    const Telemetry& telemetry,
    const EnergyTotals& energy,
    const BatteryProfileSettings& batteryProfile,
    const StateOfChargeEstimator& stateOfCharge,
    const DeviceAlarmState& alarmState)
{
    // Skip a cycle rather than publish zeros/garbage before the sensor has a
    // valid reading; the next interval will try again.
    if (!telemetry.powerOK()) return;

    char socField[16];
    if (stateOfCharge.known()) {
        snprintf(socField, sizeof(socField), "%.1f",
            static_cast<double>(stateOfCharge.percent(batteryProfile)));
    } else {
        snprintf(socField, sizeof(socField), "null");
    }

    char timeToEmptyField[16];
    if (stateOfCharge.hasTimeToEmpty()) {
        snprintf(timeToEmptyField, sizeof(timeToEmptyField), "%.1f",
            static_cast<double>(stateOfCharge.timeToEmptySeconds()) / 60.0);
    } else {
        snprintf(timeToEmptyField, sizeof(timeToEmptyField), "null");
    }

    char payload[400];
    snprintf(payload, sizeof(payload),
        "{\"voltage\":%.3f,\"current\":%.6f,\"power\":%.6f,\"temperature\":%.1f,"
        "\"net_ah\":%.6f,\"net_wh\":%.6f,\"soc_percent\":%s,\"time_to_empty_min\":%s,"
        "\"sensor_ok\":%s,\"alarm_active\":%s}",
        static_cast<double>(telemetry.voltage),
        static_cast<double>(telemetry.current),
        static_cast<double>(telemetry.power),
        static_cast<double>(telemetry.temperature),
        static_cast<double>(energy.netAh),
        static_cast<double>(energy.netWh),
        socField,
        timeToEmptyField,
        telemetry.sensorOK() ? "true" : "false",
        alarmState.active() ? "true" : "false");

    client_.publish(stateTopic_.c_str(), payload);
}
