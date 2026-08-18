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

// Off by default: session Ah/Wh have always reset every power cycle, so
// persisting them across a reboot would silently surprise anyone reading a
// "session" total that quietly survived one. Persisting is opt-in.
struct EnergyPersistenceConfig
{
    bool enabled = false;
};

class EnergyPersistenceSettings
{
public:
    void begin();
    bool save(const EnergyPersistenceConfig& settings);
    const EnergyPersistenceConfig& current() const { return current_; }

private:
    EnergyPersistenceConfig current_;
};

class EnergyAccumulator
{
public:
    // Restores persisted totals from NVS only when settings.enabled; the
    // caller must have already loaded settings via EnergyPersistenceSettings.
    void begin(const EnergyPersistenceConfig& settings);
    void update(const Telemetry& sample, const EnergyPersistenceConfig& settings);
    void reset(const EnergyPersistenceConfig& settings);

    const EnergyTotals& totals() const { return totals_; }

private:
    static void accumulateDirectional(float start, float end, float hours,
                                      float& positive, float& negative);
    void persistIfDue(uint32_t nowMs, bool force);

    Telemetry previous_;
    EnergyTotals totals_;
    bool hasPrevious_ = false;
    uint32_t lastPersistMs_ = 0;
    bool dirty_ = false;
};
