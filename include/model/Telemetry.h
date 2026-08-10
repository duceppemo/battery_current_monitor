#pragma once

#include <cstdint>
#include <cmath>

struct Telemetry
{
    // Assigned by the application when a complete sensor polling pass ends.
    // These fields give consumers a stable basis for freshness checks and,
    // later, elapsed-time energy integration.
    uint32_t sequence = 0;
    uint32_t sampledAtMs = 0;

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

    bool powerOK() const
    {
        return voltageOK && shuntOK;
    }
};
