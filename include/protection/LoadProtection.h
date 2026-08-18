#pragma once

#include <Arduino.h>

#include "energy/StateOfChargeEstimator.h"
#include "model/Telemetry.h"

struct LoadProtectionConfig
{
    // Off by default: this feature must stay a complete no-op (relay always
    // engaged) until the operator has reviewed and explicitly saved both
    // thresholds below, so an unset/default threshold can never silently
    // disconnect a load the operator never asked to protect.
    bool enabled = false;
    float lowVoltageThreshold = 3.0f;
    float lowSocPercentThreshold = 20.0f;
};

class LoadProtectionSettings
{
public:
    void begin();
    bool save(const LoadProtectionConfig& settings);
    const LoadProtectionConfig& current() const { return current_; }
    static bool isValid(const LoadProtectionConfig& settings);

private:
    LoadProtectionConfig current_;
};

// Bit flags identifying which configured condition(s) are breached, either
// live (see evaluateBreach) or latched at the moment of a trip.
constexpr uint8_t LOAD_PROTECTION_LOW_VOLTAGE = 1;
constexpr uint8_t LOAD_PROTECTION_LOW_SOC = 2;
// Set when the relay was opened by the manual "Test disconnect" button
// rather than an automatic threshold trip.
constexpr uint8_t LOAD_PROTECTION_MANUAL = 4;

/// Drives a relay/SSR control GPIO to disconnect the load once voltage or
/// state of charge drops below a configured threshold, latching disconnected
/// until an explicit reconnect (never auto-reconnects) so the relay cannot
/// chatter if a reading hovers right at the threshold under load.
class LoadProtectionMonitor
{
public:
    void begin();
    void update(
        const LoadProtectionConfig& settings,
        const Telemetry& telemetry,
        const StateOfChargeEstimator& stateOfCharge,
        const BatteryProfileSettings& batteryProfile
    );

    bool relayEngaged() const { return relayEngaged_; }
    bool tripped() const { return tripped_; }
    uint8_t tripFlags() const { return tripFlags_; }

    enum class ReconnectResult : uint8_t { Reconnected, NotTripped, ConditionStillActive };
    ReconnectResult reconnect(
        const LoadProtectionConfig& settings,
        const Telemetry& telemetry,
        const StateOfChargeEstimator& stateOfCharge,
        const BatteryProfileSettings& batteryProfile
    );

    // Bench-test hooks for verifying the relay/SSR wiring itself. These
    // bypass the enabled flag and any threshold check entirely, on purpose:
    // you need to confirm the GPIO actually switches the hardware before you
    // can trust the automatic logic above to do the right thing with it.
    void testDisconnect();
    void testConnect();

    // Flags reflecting the *current* reading against settings, independent of
    // the latched trip state above; used both internally and by the Web
    // Dashboard status JSON so a client can tell whether a reconnect would be
    // accepted right now.
    static uint8_t evaluateBreach(
        const LoadProtectionConfig& settings,
        const Telemetry& telemetry,
        const StateOfChargeEstimator& stateOfCharge,
        const BatteryProfileSettings& batteryProfile
    );

private:
    void setRelay(bool engaged);

    bool relayEngaged_ = true;
    bool tripped_ = false;
    uint8_t tripFlags_ = 0;
    // Tracks enabled/disabled across calls so update() only auto-restores
    // the load on the enabled->disabled edge, not on every loop tick, which
    // would otherwise fight the manual test buttons whenever the automatic
    // feature is off.
    bool enabledLastTick_ = false;
};
