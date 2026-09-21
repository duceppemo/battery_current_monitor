#include "energy/EnergyAccumulator.h"

#include <Preferences.h>
#include <cmath>

#include "AppConfig.h"

namespace
{
    constexpr double MS_PER_HOUR = 3600000.0;

    constexpr char SETTINGS_NS[] = "bm_energyp";
    constexpr char SETTINGS_ENABLED_KEY[] = "enabled";

    constexpr char STATE_NS[] = "bm_energy";
    constexpr char STATE_SCHEMA_KEY[] = "schema";
    constexpr char STATE_NET_AH_KEY[] = "netAh";
    constexpr char STATE_NET_WH_KEY[] = "netWh";
    constexpr char STATE_DISCHARGED_AH_KEY[] = "dischAh";
    constexpr char STATE_DISCHARGED_WH_KEY[] = "dischWh";
    constexpr char STATE_CHARGED_AH_KEY[] = "chgAh";
    constexpr char STATE_CHARGED_WH_KEY[] = "chgWh";
    constexpr uint32_t STATE_SCHEMA_VERSION = 1;
}

void EnergyPersistenceSettings::begin()
{
    Preferences p;
    if (!p.begin(SETTINGS_NS, true)) return;
    current_.enabled = p.getBool(SETTINGS_ENABLED_KEY, false);
    p.end();
}

bool EnergyPersistenceSettings::save(const EnergyPersistenceConfig& settings)
{
    Preferences p;
    if (!p.begin(SETTINGS_NS, false)) return false;
    const bool saved = p.putBool(SETTINGS_ENABLED_KEY, settings.enabled);
    p.end();
    if (saved) current_ = settings;
    return saved;
}

void EnergyAccumulator::begin(const EnergyPersistenceConfig& settings)
{
    if (!settings.enabled) return;

    Preferences p;
    if (!p.begin(STATE_NS, true)) return;
    if (p.getUInt(STATE_SCHEMA_KEY, 0) == STATE_SCHEMA_VERSION) {
        running_.netAh = p.getFloat(STATE_NET_AH_KEY, 0.0f);
        running_.netWh = p.getFloat(STATE_NET_WH_KEY, 0.0f);
        running_.dischargedAh = p.getFloat(STATE_DISCHARGED_AH_KEY, 0.0f);
        running_.dischargedWh = p.getFloat(STATE_DISCHARGED_WH_KEY, 0.0f);
        running_.chargedAh = p.getFloat(STATE_CHARGED_AH_KEY, 0.0f);
        running_.chargedWh = p.getFloat(STATE_CHARGED_WH_KEY, 0.0f);
        lastPersistedNetAh_ = running_.netAh;
        publishTotals();
    }
    p.end();
}

void EnergyAccumulator::publishTotals()
{
    totals_.netAh = static_cast<float>(running_.netAh);
    totals_.netWh = static_cast<float>(running_.netWh);
    totals_.dischargedAh = static_cast<float>(running_.dischargedAh);
    totals_.dischargedWh = static_cast<float>(running_.dischargedWh);
    totals_.chargedAh = static_cast<float>(running_.chargedAh);
    totals_.chargedWh = static_cast<float>(running_.chargedWh);
}

void EnergyAccumulator::update(const Telemetry& sample, const EnergyPersistenceConfig& settings)
{
    // Ah and Wh are only meaningful when both voltage and shunt readings are
    // valid. Clearing the origin also prevents integration across a failed
    // sample or a later recovery.
    if (!sample.powerOK()) {
        hasPrevious_ = false;
        return;
    }

    if (!hasPrevious_) {
        previous_ = sample;
        hasPrevious_ = true;
        if (settings.enabled) persistIfDue(sample.sampledAtMs, false);
        return;
    }

    const uint32_t elapsedMs = sample.sampledAtMs - previous_.sampledAtMs;
    if (elapsedMs == 0 || elapsedMs > Config::MAX_ENERGY_INTEGRATION_GAP_MS) {
        previous_ = sample;
        if (settings.enabled) persistIfDue(sample.sampledAtMs, false);
        return;
    }

    const double elapsedHours = static_cast<double>(elapsedMs) / MS_PER_HOUR;
    running_.netAh += (static_cast<double>(previous_.current) + sample.current) * 0.5 * elapsedHours;
    running_.netWh += (static_cast<double>(previous_.power) + sample.power) * 0.5 * elapsedHours;

    accumulateDirectional(
        previous_.current,
        sample.current,
        elapsedHours,
        running_.dischargedAh,
        running_.chargedAh
    );
    accumulateDirectional(
        previous_.power,
        sample.power,
        elapsedHours,
        running_.dischargedWh,
        running_.chargedWh
    );
    publishTotals();

    previous_ = sample;
    // Same reasoning as StateOfChargeEstimator: gate on Ah actually moved,
    // not on "a sample happened", or a persistently powered monitor would
    // write to flash on every tick the interval allows.
    if (std::fabs(running_.netAh - lastPersistedNetAh_) >= Config::ENERGY_PERSIST_MIN_DELTA_AH) {
        dirty_ = true;
    }
    if (settings.enabled) persistIfDue(sample.sampledAtMs, false);
}

void EnergyAccumulator::reset(const EnergyPersistenceConfig& settings)
{
    running_ = RunningTotals{};
    totals_ = EnergyTotals{};
    hasPrevious_ = false;
    lastPersistedNetAh_ = 0.0;
    dirty_ = true;
    // Force-persist immediately (not just mark dirty for the next periodic
    // tick): otherwise a crash between now and the next tick would restore
    // the pre-reset totals on the following boot, silently undoing the
    // reset the operator just asked for.
    if (settings.enabled) persistIfDue(millis(), true);
}

void EnergyAccumulator::persistIfDue(uint32_t nowMs, bool force)
{
    if (!dirty_ && !force) return;
    if (!force && (nowMs - lastPersistMs_) < Config::ENERGY_PERSIST_INTERVAL_MS) return;

    Preferences p;
    if (!p.begin(STATE_NS, false)) return;
    // Invalidate the schema first and only republish it last, matching
    // StateOfChargeEstimator's pattern, so a power loss mid-write cannot
    // leave a partially written blob that still reads as schema-valid.
    const bool invalidated = p.putUInt(STATE_SCHEMA_KEY, 0) == sizeof(uint32_t);
    const bool saved = invalidated &&
        p.putFloat(STATE_NET_AH_KEY, totals_.netAh) == sizeof(float) &&
        p.putFloat(STATE_NET_WH_KEY, totals_.netWh) == sizeof(float) &&
        p.putFloat(STATE_DISCHARGED_AH_KEY, totals_.dischargedAh) == sizeof(float) &&
        p.putFloat(STATE_DISCHARGED_WH_KEY, totals_.dischargedWh) == sizeof(float) &&
        p.putFloat(STATE_CHARGED_AH_KEY, totals_.chargedAh) == sizeof(float) &&
        p.putFloat(STATE_CHARGED_WH_KEY, totals_.chargedWh) == sizeof(float) &&
        p.putUInt(STATE_SCHEMA_KEY, STATE_SCHEMA_VERSION) == sizeof(uint32_t);
    p.end();

    if (saved) {
        lastPersistMs_ = nowMs;
        dirty_ = false;
        lastPersistedNetAh_ = running_.netAh;
    }
}

void EnergyAccumulator::accumulateDirectional(
    float start,
    float end,
    double hours,
    double& positive,
    double& negative)
{
    const double startD = start;
    const double endD = end;
    if (start >= 0.0f && end >= 0.0f) {
        positive += (startD + endD) * 0.5 * hours;
        return;
    }

    if (start <= 0.0f && end <= 0.0f) {
        negative += -(startD + endD) * 0.5 * hours;
        return;
    }

    // Split a linear sample interval exactly at its zero crossing so charging
    // and discharging totals never cancel each other during a sign change.
    const double positiveFraction = start > 0.0f
        ? startD / (startD - endD)
        : -endD / (startD - endD);
    const double positiveHours = hours * positiveFraction;
    const double negativeHours = hours - positiveHours;

    if (start > 0.0f) {
        positive += startD * 0.5 * positiveHours;
        negative += -endD * 0.5 * negativeHours;
    } else {
        negative += -startD * 0.5 * negativeHours;
        positive += endD * 0.5 * positiveHours;
    }
}
