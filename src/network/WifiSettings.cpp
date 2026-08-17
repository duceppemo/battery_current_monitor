#include "network/WifiSettings.h"

#include <Preferences.h>
#include <cstring>

namespace
{
    constexpr char NVS_NAMESPACE[] = "bm_wifi";
    constexpr char SCHEMA_KEY[] = "schema";
    constexpr char SSID_KEY[] = "ssid";
    constexpr char PASSWORD_KEY[] = "password";
    constexpr uint32_t SCHEMA_VERSION = 1;
}

void WifiSettings::begin()
{
    current_ = WifiStationSettings{};

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true)) return;
    if (preferences.getUInt(SCHEMA_KEY, 0) == SCHEMA_VERSION) {
        WifiStationSettings stored;
        const String ssid = preferences.getString(SSID_KEY, "");
        const String password = preferences.getString(PASSWORD_KEY, "");
        snprintf(stored.ssid, sizeof(stored.ssid), "%s", ssid.c_str());
        snprintf(stored.password, sizeof(stored.password), "%s", password.c_str());
        if (isValid(stored)) current_ = stored;
    }
    preferences.end();
}

bool WifiSettings::save(const WifiStationSettings& settings)
{
    if (!isValid(settings)) return false;

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) return false;

    // Publish the schema version only after every value is written, so a
    // power loss cannot leave a partially valid station profile.
    const bool invalidated = preferences.putUInt(SCHEMA_KEY, 0) == sizeof(uint32_t);
    const bool saved = invalidated &&
        preferences.putString(SSID_KEY, settings.ssid) == strlen(settings.ssid) &&
        preferences.putString(PASSWORD_KEY, settings.password) == strlen(settings.password) &&
        preferences.putUInt(SCHEMA_KEY, SCHEMA_VERSION) == sizeof(uint32_t);
    preferences.end();

    if (saved) current_ = settings;
    return saved;
}

bool WifiSettings::clear()
{
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) return false;
    // This namespace belongs only to station credentials. Clearing it also
    // recovers cleanly from an interrupted or partly-written profile.
    const bool cleared = preferences.clear();
    preferences.end();

    if (cleared) current_ = WifiStationSettings{};
    return cleared;
}

bool WifiSettings::isValid(const WifiStationSettings& settings)
{
    const size_t ssidLength = strnlen(settings.ssid, sizeof(settings.ssid));
    const size_t passwordLength = strnlen(settings.password, sizeof(settings.password));
    return ssidLength > 0 && ssidLength < sizeof(settings.ssid) &&
        passwordLength < sizeof(settings.password) &&
        (passwordLength == 0 || passwordLength >= 8);
}
