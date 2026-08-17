#pragma once

#include <Arduino.h>

struct WifiStationSettings
{
    char ssid[33] = "";
    char password[65] = "";
};

/// Owns the monitor's optional home-network credentials. The SoftAP is a
/// separate recovery transport and is never disabled by this setting.
class WifiSettings
{
public:
    void begin();
    bool save(const WifiStationSettings& settings);
    bool clear();

    const WifiStationSettings& current() const { return current_; }
    bool configured() const { return current_.ssid[0] != '\0'; }
    static bool isValid(const WifiStationSettings& settings);

private:
    WifiStationSettings current_;
};
