#pragma once

#include <Arduino.h>

#include "alarm/AlarmSettings.h"
#include "model/Telemetry.h"

struct NtfyConfig
{
    bool enabled = false;
    char server[65] = "https://ntfy.sh";
    char topic[65] = "";
};

/// Persists the ntfy push-notification endpoint, analogous to MqttSettings.
class NtfySettings
{
public:
    void begin();
    bool save(const NtfyConfig& settings);
    const NtfyConfig& current() const { return current_; }
    static bool isValid(const NtfyConfig& settings);

private:
    NtfyConfig current_;
};

/// Sends a push notification via ntfy (https://ntfy.sh or a self-hosted
/// instance behind HTTPS) on the rising edge of each alarm condition --
/// entirely optional and no-op unless a topic is configured. Requires
/// station mode with real internet access, same as MQTT and the GitHub
/// release check; Web Dashboard only, no BLE surface, for the same reason.
///
/// Each notification is a blocking HTTPS POST (TLS handshake can take a
/// second or more) bounded by a short timeout. This only runs on an
/// alarm's rising edge, not every loop, so an occasional multi-second
/// stall across BLE/Web servicing when an alarm actually trips is an
/// accepted trade-off, not deferred to the main loop the way OTA signature
/// verification is -- that was a genuine BLE-task stack-overflow crash risk,
/// this is rare, bounded latency.
class NtfyNotifier
{
public:
    void update(
        const NtfyConfig& settings,
        const Telemetry& telemetry,
        const DeviceAlarmSettings& thresholds,
        const DeviceAlarmState& alarmState
    );

private:
    void notify(const NtfyConfig& settings, const char* title, const char* message, const char* tags);

    uint8_t lastActiveFlags_ = 0;
};
