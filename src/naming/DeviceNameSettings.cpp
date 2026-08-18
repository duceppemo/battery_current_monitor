#include "naming/DeviceNameSettings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace { constexpr char NS[] = "bm_name"; constexpr char KEY[] = "settings"; }

void DeviceNameSettings::begin()
{
    Preferences p;
    if (!p.begin(NS, true)) return;
    DeviceNameConfig stored;
    if (p.getBytesLength(KEY) == sizeof(stored) &&
        p.getBytes(KEY, &stored, sizeof(stored)) == sizeof(stored) &&
        isValid(stored)) {
        current_ = stored;
    }
    p.end();
}

bool DeviceNameSettings::save(const DeviceNameConfig& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(NS, false)) return false;
    const bool saved = p.putBytes(KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool DeviceNameSettings::isValid(const DeviceNameConfig& settings)
{
    const size_t length = strnlen(settings.name, sizeof(settings.name));
    if (length >= sizeof(settings.name)) return false;
    // Printable ASCII only, and no ';' -- the name is embedded in the
    // semicolon-delimited BLE Device Information string.
    for (size_t i = 0; i < length; ++i) {
        const char c = settings.name[i];
        if (c < 0x20 || c > 0x7E || c == ';') return false;
    }
    return true;
}

void DeviceNameSettings::computeEffectiveName(const DeviceNameConfig& settings, char* out, size_t outSize)
{
    if (settings.name[0] != '\0') {
        snprintf(out, outSize, "%s", settings.name);
        return;
    }
    char idHex[16];
    snprintf(idHex, sizeof(idHex), "%012llX",
        static_cast<unsigned long long>(ESP.getEfuseMac()));
    snprintf(out, outSize, "Battery Monitor %s", idHex + 8);
}
