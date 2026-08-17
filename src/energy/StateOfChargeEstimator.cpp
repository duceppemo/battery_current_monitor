#include "energy/StateOfChargeEstimator.h"

#include <Preferences.h>
#include <cmath>
#include <limits>

#include "AppConfig.h"

namespace
{
    constexpr char PROFILE_NS[] = "bm_battprof";
    constexpr char PROFILE_KEY[] = "settings";

    constexpr char STATE_NS[] = "bm_soc";
    constexpr char STATE_SCHEMA_KEY[] = "schema";
    constexpr char STATE_REMAINING_KEY[] = "remainAh";
    constexpr char STATE_SYNCED_KEY[] = "synced";
    constexpr uint32_t STATE_SCHEMA_VERSION = 1;

    constexpr float MS_PER_HOUR = 3600000.0f;
}

void BatteryProfile::begin()
{
    Preferences p;
    if (!p.begin(PROFILE_NS, true)) return;
    BatteryProfileSettings stored;
    if (p.getBytesLength(PROFILE_KEY) == sizeof(stored) &&
        p.getBytes(PROFILE_KEY, &stored, sizeof(stored)) == sizeof(stored) &&
        isValid(stored)) {
        current_ = stored;
    }
    p.end();
}

bool BatteryProfile::save(const BatteryProfileSettings& settings)
{
    if (!isValid(settings)) return false;
    Preferences p;
    if (!p.begin(PROFILE_NS, false)) return false;
    const bool saved = p.putBytes(PROFILE_KEY, &settings, sizeof(settings)) == sizeof(settings);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

bool BatteryProfile::isValid(const BatteryProfileSettings& settings)
{
    return std::isfinite(settings.capacityAh) && settings.capacityAh > 0.0f &&
        settings.capacityAh <= 10000.0f &&
        std::isfinite(settings.chargedVoltage) && settings.chargedVoltage > 0.0f &&
        settings.chargedVoltage <= 100.0f;
}

void StateOfChargeEstimator::begin()
{
    Preferences p;
    if (!p.begin(STATE_NS, true)) return;
    if (p.getUInt(STATE_SCHEMA_KEY, 0) == STATE_SCHEMA_VERSION) {
        remainingAh_ = p.getFloat(STATE_REMAINING_KEY, 0.0f);
        synced_ = p.getBool(STATE_SYNCED_KEY, false);
    }
    p.end();
}

void StateOfChargeEstimator::update(
    const Telemetry& sample,
    const BatteryProfileSettings& profile,
    uint32_t nowMs)
{
    if (!sample.powerOK()) {
        hasPrevious_ = false;
        return;
    }

    if (!hasPrevious_) {
        previous_ = sample;
        hasPrevious_ = true;
        averageCurrentA_ = sample.current;
        checkAutoSync(sample, profile, nowMs);
        persistIfDue(nowMs, false);
        return;
    }

    const uint32_t elapsedMs = sample.sampledAtMs - previous_.sampledAtMs;
    if (elapsedMs == 0 || elapsedMs > Config::MAX_ENERGY_INTEGRATION_GAP_MS) {
        previous_ = sample;
        checkAutoSync(sample, profile, nowMs);
        persistIfDue(nowMs, false);
        return;
    }

    const float elapsedHours = static_cast<float>(elapsedMs) / MS_PER_HOUR;
    // Positive current is discharge by this project's convention, so it
    // subtracts from the remaining capacity.
    const float dischargedAh = (previous_.current + sample.current) * 0.5f * elapsedHours;
    const float clampedCapacity = profile.capacityAh > 0.0f ? profile.capacityAh : 0.0f;
    const float updated = remainingAh_ - dischargedAh;
    remainingAh_ = updated < 0.0f ? 0.0f : (updated > clampedCapacity ? clampedCapacity : updated);
    dirty_ = true;

    averageCurrentA_ = std::isnan(averageCurrentA_)
        ? sample.current
        : averageCurrentA_ * (1.0f - Config::SOC_CURRENT_SMOOTHING_ALPHA) +
              sample.current * Config::SOC_CURRENT_SMOOTHING_ALPHA;

    previous_ = sample;
    checkAutoSync(sample, profile, nowMs);
    persistIfDue(nowMs, false);
}

void StateOfChargeEstimator::checkAutoSync(
    const Telemetry& sample,
    const BatteryProfileSettings& profile,
    uint32_t nowMs)
{
    if (profile.capacityAh <= 0.0f) {
        fullChargeConditionActive_ = false;
        return;
    }

    const float tailCurrentA = profile.capacityAh * Config::SOC_TAIL_CURRENT_CAPACITY_FRACTION;
    const bool conditionMet = sample.voltageValid() && sample.voltage >= profile.chargedVoltage &&
        sample.currentValid() && sample.current <= tailCurrentA;

    if (!conditionMet) {
        fullChargeConditionActive_ = false;
        return;
    }

    if (!fullChargeConditionActive_) {
        fullChargeConditionActive_ = true;
        fullChargeConditionStartMs_ = nowMs;
        return;
    }

    if (nowMs - fullChargeConditionStartMs_ >= Config::SOC_FULL_CHARGE_SUSTAIN_MS) {
        syncToFull(profile);
        fullChargeConditionActive_ = false;
    }
}

void StateOfChargeEstimator::syncToFull(const BatteryProfileSettings& profile)
{
    remainingAh_ = profile.capacityAh > 0.0f ? profile.capacityAh : 0.0f;
    synced_ = true;
    dirty_ = true;
    persistIfDue(millis(), true);
}

void StateOfChargeEstimator::persistIfDue(uint32_t nowMs, bool force)
{
    if (!dirty_ && !force) return;
    if (!force && (nowMs - lastPersistMs_) < Config::SOC_PERSIST_INTERVAL_MS) return;

    Preferences p;
    if (!p.begin(STATE_NS, false)) return;
    // Publish the schema version only after every value is written, matching
    // WifiSettings' pattern so a power loss cannot leave a partially written,
    // schema-valid-looking state.
    const bool invalidated = p.putUInt(STATE_SCHEMA_KEY, 0) == sizeof(uint32_t);
    const bool saved = invalidated &&
        p.putFloat(STATE_REMAINING_KEY, remainingAh_) == sizeof(float) &&
        p.putBool(STATE_SYNCED_KEY, synced_) &&
        p.putUInt(STATE_SCHEMA_KEY, STATE_SCHEMA_VERSION) == sizeof(uint32_t);
    p.end();

    if (saved) {
        lastPersistMs_ = nowMs;
        dirty_ = false;
    }
}

float StateOfChargeEstimator::percent(const BatteryProfileSettings& profile) const
{
    if (!synced_ || profile.capacityAh <= 0.0f) return NAN;
    const float value = remainingAh_ / profile.capacityAh * 100.0f;
    return value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : value);
}

bool StateOfChargeEstimator::hasTimeToEmpty() const
{
    return synced_ && !std::isnan(averageCurrentA_) && averageCurrentA_ > 0.01f;
}

uint32_t StateOfChargeEstimator::timeToEmptySeconds() const
{
    if (!hasTimeToEmpty()) return 0;
    const float hours = remainingAh_ / averageCurrentA_;
    const float seconds = hours * 3600.0f;
    if (!std::isfinite(seconds) || seconds <= 0.0f) return 0;
    return seconds > 4294967295.0f
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(seconds);
}
