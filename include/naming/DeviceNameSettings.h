#pragma once

#include <cstddef>
#include <cstdint>

struct DeviceNameConfig
{
    // Empty means "not customized" -- callers resolve a default from the
    // stable per-chip ID (see DeviceNameSettings::computeEffectiveName)
    // rather than storing a generated value, so the default stays correct
    // even if it's ever derived differently in a future firmware version.
    char name[33] = "";
};

/// Persists an optional user-assigned device name, analogous to
/// MqttSettings/NtfySettings. Device-side and authoritative: both the Web
/// Dashboard and the BLE app can set it, and both read it back from the
/// same source (the Web Dashboard via /api telemetry, the app via the
/// NAME= field in Device Information) rather than each keeping their own
/// local label.
class DeviceNameSettings
{
public:
    void begin();
    bool save(const DeviceNameConfig& settings);
    const DeviceNameConfig& current() const { return current_; }
    static bool isValid(const DeviceNameConfig& settings);

    // Resolves the name actually shown to a user: the stored name if one
    // was set, otherwise "Battery Monitor <last 4 hex of the eFuse MAC>" --
    // the same fragment/convention the app uses for a not-yet-renamed
    // saved monitor, so a fresh device's default name is identical whether
    // read from the app or the Web Dashboard.
    static void computeEffectiveName(const DeviceNameConfig& settings, char* out, size_t outSize);

private:
    DeviceNameConfig current_;
};
