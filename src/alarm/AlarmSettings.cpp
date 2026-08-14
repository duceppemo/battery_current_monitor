#include "alarm/AlarmSettings.h"

#include <Preferences.h>
#include <cmath>

namespace { constexpr char NS[]="bm_alarms"; constexpr char KEY[]="settings"; }

void AlarmSettings::begin()
{
    Preferences p;
    if (!p.begin(NS, true)) return;
    DeviceAlarmSettings stored;
    if (p.getBytesLength(KEY) == sizeof(stored) &&
        p.getBytes(KEY, &stored, sizeof(stored)) == sizeof(stored) && isValid(stored)) current_ = stored;
    p.end();
}

bool AlarmSettings::save(const DeviceAlarmSettings& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(NS, false)) return false;
    const bool saved = p.putBytes(KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool AlarmSettings::isValid(const DeviceAlarmSettings& s)
{
    return std::isfinite(s.lowVoltage) && std::isfinite(s.highVoltage) &&
           std::isfinite(s.maxAbsoluteCurrent) && std::isfinite(s.maxTemperature) &&
           s.lowVoltage >= 0.0f && s.lowVoltage < s.highVoltage && s.highVoltage <= 100.0f &&
           s.maxAbsoluteCurrent > 0.0f && s.maxAbsoluteCurrent <= 200.0f &&
           s.maxTemperature > -40.0f && s.maxTemperature <= 125.0f;
}

void AlarmMonitor::update(const Telemetry& t, const DeviceAlarmSettings& s)
{
    uint8_t flags = 0;
    if (s.lowVoltageEnabled && t.voltageValid() && t.voltage < s.lowVoltage) flags |= 1;
    if (s.highVoltageEnabled && t.voltageValid() && t.voltage > s.highVoltage) flags |= 2;
    if (s.currentEnabled && t.currentValid() && fabsf(t.current) > s.maxAbsoluteCurrent) flags |= 4;
    if (s.temperatureEnabled && t.temperatureValid() && t.temperature > s.maxTemperature) flags |= 8;
    if (s.sensorHealthEnabled && !t.sensorOK()) flags |= 16;
    state_.activeFlags = flags;
    state_.sensorHealthy = t.sensorOK();
}
