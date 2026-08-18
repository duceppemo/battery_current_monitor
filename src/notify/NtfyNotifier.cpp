#include "notify/NtfyNotifier.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstring>

#include "AppConfig.h"

namespace { constexpr char NS[] = "bm_ntfy"; constexpr char KEY[] = "settings"; }

void NtfySettings::begin()
{
    Preferences p;
    if (!p.begin(NS, true)) return;
    NtfyConfig stored;
    if (p.getBytesLength(KEY) == sizeof(stored) &&
        p.getBytes(KEY, &stored, sizeof(stored)) == sizeof(stored) &&
        isValid(stored)) {
        current_ = stored;
    }
    p.end();
}

bool NtfySettings::save(const NtfyConfig& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(NS, false)) return false;
    const bool saved = p.putBytes(KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool NtfySettings::isValid(const NtfyConfig& settings)
{
    const size_t serverLength = strnlen(settings.server, sizeof(settings.server));
    const size_t topicLength = strnlen(settings.topic, sizeof(settings.topic));
    if (serverLength >= sizeof(settings.server)) return false;
    if (topicLength >= sizeof(settings.topic)) return false;
    // Only HTTPS servers are supported (see NtfyNotifier's header comment for
    // why); an empty/disabled config is still valid so save() can persist
    // "not configured yet".
    if (serverLength > 0 && strncmp(settings.server, "https://", 8) != 0) return false;
    if (settings.enabled && (serverLength == 0 || topicLength == 0)) return false;
    return true;
}

void NtfyNotifier::update(
    const NtfyConfig& settings,
    const Telemetry& telemetry,
    const DeviceAlarmSettings& thresholds,
    const DeviceAlarmState& alarmState)
{
    const uint8_t newlyActive = alarmState.activeFlags & static_cast<uint8_t>(~lastActiveFlags_);
    lastActiveFlags_ = alarmState.activeFlags;

    if (newlyActive == 0 || !settings.enabled || settings.topic[0] == '\0') {
        return;
    }

    char message[96];
    if (newlyActive & 1U) {
        snprintf(message, sizeof(message), "Voltage %.2fV is below the %.2fV threshold.",
            static_cast<double>(telemetry.voltage), static_cast<double>(thresholds.lowVoltage));
        notify(settings, "Battery Monitor: Low voltage", message, "warning");
    }
    if (newlyActive & 2U) {
        snprintf(message, sizeof(message), "Voltage %.2fV is above the %.2fV threshold.",
            static_cast<double>(telemetry.voltage), static_cast<double>(thresholds.highVoltage));
        notify(settings, "Battery Monitor: High voltage", message, "warning");
    }
    if (newlyActive & 4U) {
        snprintf(message, sizeof(message), "Current %.2fA exceeds the %.2fA threshold.",
            static_cast<double>(fabsf(telemetry.current)), static_cast<double>(thresholds.maxAbsoluteCurrent));
        notify(settings, "Battery Monitor: Overcurrent", message, "warning");
    }
    if (newlyActive & 8U) {
        snprintf(message, sizeof(message), "Temperature %.1fC exceeds the %.1fC threshold.",
            static_cast<double>(telemetry.temperature), static_cast<double>(thresholds.maxTemperature));
        notify(settings, "Battery Monitor: Overtemperature", message, "warning");
    }
    if (newlyActive & 16U) {
        notify(settings, "Battery Monitor: Sensor fault",
            "The INA228 sensor is reporting an unhealthy status.", "rotating_light");
    }
}

void NtfyNotifier::notify(const NtfyConfig& settings, const char* title, const char* message, const char* tags)
{
    String url = settings.server;
    while (url.endsWith("/")) url.remove(url.length() - 1);
    url += "/";
    url += settings.topic;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(Config::NTFY_HTTP_TIMEOUT_MS);
    if (!https.begin(client, url)) {
        Serial.println("WARNING: ntfy request setup failed.");
        return;
    }
    https.addHeader("Title", title);
    https.addHeader("Tags", tags);
    https.addHeader("Priority", "high");
    const int status = https.POST(message);
    if (status <= 0) {
        Serial.printf("WARNING: ntfy notification failed (%s).\n", HTTPClient::errorToString(status).c_str());
    } else if (status >= 300) {
        Serial.printf("WARNING: ntfy notification rejected (HTTP %d).\n", status);
    }
    https.end();
}
