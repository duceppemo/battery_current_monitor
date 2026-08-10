#pragma once

#include <cmath>

struct Telemetry
{
    float voltage = NAN;
    float shuntVoltage = NAN;
    float current = NAN;
    float power = NAN;
    float temperature = NAN;

    bool voltageOK = false;
    bool shuntOK = false;
    bool temperatureOK = false;

    bool sensorOK() const
    {
        return voltageOK && shuntOK && temperatureOK;
    }
};
