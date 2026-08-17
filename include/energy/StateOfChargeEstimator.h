#pragma once

#include <Arduino.h>

#include "model/Telemetry.h"

struct BatteryProfileSettings
{
    float capacityAh = 100.0f;
    float chargedVoltage = 14.4f;
};

/// Persists the user-supplied battery profile (capacity and the voltage that
/// indicates a full charge). Separate from BatteryProfileSettings' NVS
/// namespace, StateOfChargeEstimator below persists its own running state.
class BatteryProfile
{
public:
    void begin();
    bool save(const BatteryProfileSettings& settings);
    const BatteryProfileSettings& current() const { return current_; }
    static bool isValid(const BatteryProfileSettings& settings);

private:
    BatteryProfileSettings current_;
};

/// Coulomb-counted state of charge, expressed as remaining amp-hours against
/// the configured capacity. Unlike EnergyAccumulator's per-power-on-session
/// Ah/Wh totals (which intentionally reset every boot), this must survive
/// reboots to be useful as a fuel gauge, so it persists periodically to its
/// own NVS namespace. It has no notion of "correct" until a full-charge sync
/// happens at least once, either detected automatically (sustained voltage
/// at or above the profile's charged voltage with a near-zero/charging
/// current) or triggered manually from the app or Web Dashboard.
class StateOfChargeEstimator
{
public:
    void begin();
    void update(const Telemetry& sample, const BatteryProfileSettings& profile, uint32_t nowMs);
    void syncToFull(const BatteryProfileSettings& profile);

    bool known() const { return synced_; }
    float percent(const BatteryProfileSettings& profile) const;
    float remainingAh() const { return remainingAh_; }
    bool hasTimeToEmpty() const;
    uint32_t timeToEmptySeconds() const;

private:
    void checkAutoSync(const Telemetry& sample, const BatteryProfileSettings& profile, uint32_t nowMs);
    void persistIfDue(uint32_t nowMs, bool force);

    Telemetry previous_;
    bool hasPrevious_ = false;
    float remainingAh_ = 0.0f;
    bool synced_ = false;
    float averageCurrentA_ = NAN;
    uint32_t fullChargeConditionStartMs_ = 0;
    bool fullChargeConditionActive_ = false;
    uint32_t lastPersistMs_ = 0;
    bool dirty_ = false;
};
