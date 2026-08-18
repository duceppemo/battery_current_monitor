#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "alarm/AlarmSettings.h"
#include "energy/EnergyAccumulator.h"
#include "energy/StateOfChargeEstimator.h"
#include "model/Telemetry.h"

struct MqttBrokerSettings
{
    bool enabled = false;
    char host[65] = "";
    uint16_t port = 1883;
    char username[33] = "";
    char password[65] = "";
};

/// Persists the MQTT broker connection, analogous to BatteryProfile/WifiSettings.
class MqttSettings
{
public:
    void begin();
    bool save(const MqttBrokerSettings& settings);
    const MqttBrokerSettings& current() const { return current_; }
    static bool isValid(const MqttBrokerSettings& settings);

private:
    MqttBrokerSettings current_;
};

/// Publishes telemetry to an MQTT broker with Home Assistant MQTT discovery,
/// entirely optional and no-op unless a broker is configured. Never blocks
/// measurement polling: connection attempts and publishes are both
/// interval-gated non-blocking calls driven from the application loop, same
/// as the Web/BLE transports.
class MqttPublisher
{
public:
    void update(
        uint32_t nowMs,
        const MqttBrokerSettings& settings,
        const Telemetry& telemetry,
        const EnergyTotals& energy,
        const BatteryProfileSettings& batteryProfile,
        const StateOfChargeEstimator& stateOfCharge,
        const DeviceAlarmState& alarmState
    );

    bool connected() { return client_.connected(); }
    const char* brokerHost() const { return lastHost_; }

private:
    void ensureDeviceId();
    void connectIfDue(const MqttBrokerSettings& settings, uint32_t nowMs);
    void publishDiscovery();
    void publishState(
        const Telemetry& telemetry,
        const EnergyTotals& energy,
        const BatteryProfileSettings& batteryProfile,
        const StateOfChargeEstimator& stateOfCharge,
        const DeviceAlarmState& alarmState
    );

    WiFiClient wifiClient_;
    PubSubClient client_{wifiClient_};
    String deviceId_;
    String stateTopic_;
    String availabilityTopic_;
    bool hasDeviceId_ = false;
    bool discoveryPublished_ = false;
    char lastHost_[65] = "";
    uint16_t lastPort_ = 0;
    uint32_t lastConnectAttemptMs_ = 0;
    uint32_t lastPublishMs_ = 0;
};
