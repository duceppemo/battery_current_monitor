#include "measurement/CalibrationSettings.h"

#include <Preferences.h>
#include <cmath>

#include "AppConfig.h"

namespace
{
    constexpr char NVS_NAMESPACE[] = "battery_mon";
    constexpr char SCHEMA_KEY[] = "cal_schema";
    constexpr char SHUNT_KEY[] = "shunt_ohm";
    constexpr char OFFSET_KEY[] = "shunt_off";
    constexpr char GAIN_KEY[] = "current_gain";
    constexpr uint32_t SCHEMA_VERSION = 1;
}

void CalibrationSettings::begin()
{
    current_ = CurrentCalibration{
        Config::SHUNT_RESISTANCE_OHMS,
        0.0f,
        1.0f
    };
    loadedFromStorage_ = false;

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        return;
    }

    if (preferences.getUInt(SCHEMA_KEY, 0) == SCHEMA_VERSION) {
        const CurrentCalibration stored{
            preferences.getFloat(SHUNT_KEY, NAN),
            preferences.getFloat(OFFSET_KEY, NAN),
            preferences.getFloat(GAIN_KEY, NAN)
        };

        if (isValid(stored)) {
            current_ = stored;
            loadedFromStorage_ = true;
        }
    }

    preferences.end();
}

bool CalibrationSettings::save(const CurrentCalibration& calibration)
{
    if (!isValid(calibration)) {
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    // Invalidate first and commit the schema marker last. If power is removed
    // during one of the field writes, the next boot rejects the partial
    // profile and uses the known-safe compile-time default instead.
    const bool invalidated =
        preferences.putUInt(SCHEMA_KEY, 0) == sizeof(uint32_t);
    const bool saved = invalidated &&
        preferences.putFloat(SHUNT_KEY, calibration.shuntResistanceOhms) == sizeof(float) &&
        preferences.putFloat(OFFSET_KEY, calibration.shuntOffsetVolts) == sizeof(float) &&
        preferences.putFloat(GAIN_KEY, calibration.currentGain) == sizeof(float) &&
        preferences.putUInt(SCHEMA_KEY, SCHEMA_VERSION) == sizeof(SCHEMA_VERSION);
    preferences.end();

    if (saved) {
        current_ = calibration;
        loadedFromStorage_ = true;
    }

    return saved;
}

bool CalibrationSettings::clear()
{
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    // This namespace is owned exclusively by CalibrationSettings. Clearing it
    // first means an interrupted reset cannot leave a valid-looking profile.
    const bool cleared = preferences.clear();
    preferences.end();

    if (cleared) {
        current_ = CurrentCalibration{
            Config::SHUNT_RESISTANCE_OHMS,
            0.0f,
            1.0f
        };
        loadedFromStorage_ = false;
    }

    return cleared;
}

bool CalibrationSettings::isValid(const CurrentCalibration& calibration)
{
    return std::isfinite(calibration.shuntResistanceOhms) &&
           calibration.shuntResistanceOhms >= 0.00005f &&
           calibration.shuntResistanceOhms <= 0.100f &&
           std::isfinite(calibration.shuntOffsetVolts) &&
           std::fabs(calibration.shuntOffsetVolts) <= 0.010f &&
           std::isfinite(calibration.currentGain) &&
           calibration.currentGain >= 0.5f &&
           calibration.currentGain <= 1.5f;
}
