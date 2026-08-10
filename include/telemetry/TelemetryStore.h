#pragma once

#include "model/MetricStats.h"
#include "model/Telemetry.h"

class TelemetryStore
{
public:
    void update(const Telemetry& telemetry);
    void resetExtrema();

    const Telemetry& current() const { return current_; }

    const MetricStats& voltageStats() const { return voltageStats_; }
    const MetricStats& shuntStats() const { return shuntStats_; }
    const MetricStats& currentStats() const { return currentStats_; }
    const MetricStats& powerStats() const { return powerStats_; }
    const MetricStats& temperatureStats() const { return temperatureStats_; }

private:
    void updateExtrema();

    Telemetry current_;

    MetricStats voltageStats_;
    MetricStats shuntStats_;
    MetricStats currentStats_;
    MetricStats powerStats_;
    MetricStats temperatureStats_;
};
