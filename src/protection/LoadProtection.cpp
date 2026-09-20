#include "protection/LoadProtection.h"

#include <Preferences.h>
#include <cmath>

#include "AppConfig.h"

namespace
{
    constexpr char NS[] = "bm_prot";
    constexpr char KEY[] = "settings";
    // Latched automatic trip flags (0 = not tripped), same namespace.
    constexpr char TRIP_KEY[] = "trip";
}

void LoadProtectionSettings::begin()
{
    Preferences p;
    if (!p.begin(NS, true)) return;
    LoadProtectionConfig stored;
    if (p.getBytesLength(KEY) == sizeof(stored) &&
        p.getBytes(KEY, &stored, sizeof(stored)) == sizeof(stored) && isValid(stored)) {
        current_ = stored;
    }
    p.end();
}

bool LoadProtectionSettings::save(const LoadProtectionConfig& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(NS, false)) return false;
    const bool saved = p.putBytes(KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool LoadProtectionSettings::isValid(const LoadProtectionConfig& s)
{
    return std::isfinite(s.lowVoltageThreshold) && std::isfinite(s.lowSocPercentThreshold) &&
           s.lowVoltageThreshold >= 0.0f && s.lowVoltageThreshold <= 100.0f &&
           s.lowSocPercentThreshold >= 0.0f && s.lowSocPercentThreshold <= 100.0f;
}

void LoadProtectionMonitor::begin(const LoadProtectionConfig& settings)
{
    pinMode(Config::LOAD_PROTECTION_RELAY_PIN, OUTPUT);
    tripped_ = false;
    tripFlags_ = 0;

    // Without this, every boot reconnected the load until the first sample
    // was evaluated. At deep discharge that reconnect can brown the
    // regulator out and reboot the monitor, which reconnects the load
    // again -- a loop that keeps hammering the battery it was meant to
    // protect.
    uint8_t storedFlags = 0;
    Preferences p;
    if (p.begin(NS, true)) {
        storedFlags = p.getUChar(TRIP_KEY, 0);
        p.end();
    }
    if (settings.enabled && storedFlags != 0 && (storedFlags & LOAD_PROTECTION_MANUAL) == 0) {
        tripped_ = true;
        tripFlags_ = storedFlags;
        enabledLastTick_ = true;
        setRelay(false);
        return;
    }

    setRelay(true);
    if (storedFlags != 0) persistTripState();
}

void LoadProtectionMonitor::persistTripState()
{
    const uint8_t flags = tripped_ && (tripFlags_ & LOAD_PROTECTION_MANUAL) == 0 ? tripFlags_ : 0;
    Preferences p;
    if (!p.begin(NS, false)) return;
    if (p.getUChar(TRIP_KEY, 0) != flags) p.putUChar(TRIP_KEY, flags);
    p.end();
}

uint8_t LoadProtectionMonitor::evaluateBreach(
    const LoadProtectionConfig& settings,
    const Telemetry& telemetry,
    const StateOfChargeEstimator& stateOfCharge,
    const BatteryProfileSettings& batteryProfile)
{
    uint8_t flags = 0;
    if (telemetry.voltageValid() && telemetry.voltage < settings.lowVoltageThreshold) {
        flags |= LOAD_PROTECTION_LOW_VOLTAGE;
    }
    if (stateOfCharge.known() && stateOfCharge.percent(batteryProfile) < settings.lowSocPercentThreshold) {
        flags |= LOAD_PROTECTION_LOW_SOC;
    }
    return flags;
}

void LoadProtectionMonitor::update(
    const LoadProtectionConfig& settings,
    const Telemetry& telemetry,
    const StateOfChargeEstimator& stateOfCharge,
    const BatteryProfileSettings& batteryProfile)
{
    if (!settings.enabled) {
        // Only auto-restore the load on the enabled->disabled edge, not on
        // every tick while it stays disabled -- otherwise this would fight
        // the manual test buttons any time the automatic feature is off.
        if (enabledLastTick_ && !relayEngaged_) setRelay(true);
        if (enabledLastTick_) {
            tripped_ = false;
            tripFlags_ = 0;
            persistTripState();
        }
        enabledLastTick_ = false;
        return;
    }
    enabledLastTick_ = true;

    // Latched: once tripped, only an explicit reconnect() clears it, even if
    // the reading recovers on its own in the meantime.
    if (tripped_) return;

    const uint8_t flags = evaluateBreach(settings, telemetry, stateOfCharge, batteryProfile);
    if (flags != 0) {
        tripped_ = true;
        tripFlags_ = flags;
        setRelay(false);
        persistTripState();
    }
}

LoadProtectionMonitor::ReconnectResult LoadProtectionMonitor::reconnect(
    const LoadProtectionConfig& settings,
    const Telemetry& telemetry,
    const StateOfChargeEstimator& stateOfCharge,
    const BatteryProfileSettings& batteryProfile)
{
    if (!tripped_) return ReconnectResult::NotTripped;

    if (evaluateBreach(settings, telemetry, stateOfCharge, batteryProfile) != 0) {
        return ReconnectResult::ConditionStillActive;
    }

    tripped_ = false;
    tripFlags_ = 0;
    setRelay(true);
    persistTripState();
    return ReconnectResult::Reconnected;
}

void LoadProtectionMonitor::testDisconnect()
{
    tripped_ = true;
    tripFlags_ = LOAD_PROTECTION_MANUAL;
    setRelay(false);
    persistTripState();
}

void LoadProtectionMonitor::testConnect()
{
    tripped_ = false;
    tripFlags_ = 0;
    setRelay(true);
    persistTripState();
}

void LoadProtectionMonitor::setRelay(bool engaged)
{
    relayEngaged_ = engaged;
    digitalWrite(Config::LOAD_PROTECTION_RELAY_PIN, engaged ? HIGH : LOW);
}
