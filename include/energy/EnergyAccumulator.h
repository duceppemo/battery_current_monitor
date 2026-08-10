#pragma once

#include <Arduino.h>

#include "model/Telemetry.h"

struct EnergyTotals
{
    // Positive current is defined by this project as battery discharge.
    float netAh = 0.0f;
    float netWh = 0.0f;
    float dischargedAh = 0.0f;
    float dischargedWh = 0.0f;
    float chargedAh = 0.0f;
    float chargedWh = 0.0f;
};

class EnergyAccumulator
{
public:
    void update(const Telemetry& sample);
    void reset();

    const EnergyTotals& totals() const { return totals_; }

private:
    static void accumulateDirectional(float start, float end, float hours,
                                      float& positive, float& negative);

    Telemetry previous_;
    EnergyTotals totals_;
    bool hasPrevious_ = false;
};
