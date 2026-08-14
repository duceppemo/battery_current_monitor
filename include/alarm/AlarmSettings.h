#pragma once

#include "model/Telemetry.h"

struct DeviceAlarmSettings
{
    bool lowVoltageEnabled = false;
    bool highVoltageEnabled = false;
    bool currentEnabled = false;
    bool temperatureEnabled = false;
    bool sensorHealthEnabled = false;
    float lowVoltage = 3.0f;
    float highVoltage = 4.25f;
    float maxAbsoluteCurrent = 5.0f;
    float maxTemperature = 60.0f;
};

struct DeviceAlarmState
{
    uint8_t activeFlags = 0;
    bool sensorHealthy = true;
    bool active() const { return activeFlags != 0; }
};

class AlarmSettings
{
public:
    void begin();
    bool save(const DeviceAlarmSettings& settings);
    const DeviceAlarmSettings& current() const { return current_; }
    static bool isValid(const DeviceAlarmSettings& settings);

private:
    DeviceAlarmSettings current_;
};

class AlarmMonitor
{
public:
    void update(const Telemetry& telemetry, const DeviceAlarmSettings& settings);
    const DeviceAlarmState& state() const { return state_; }

private:
    DeviceAlarmState state_;
};
