#include "energy/EnergyAccumulator.h"

#include "AppConfig.h"

namespace
{
    constexpr float MS_PER_HOUR = 3600000.0f;
}

void EnergyAccumulator::update(const Telemetry& sample)
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
        return;
    }

    const uint32_t elapsedMs = sample.sampledAtMs - previous_.sampledAtMs;
    if (elapsedMs == 0 || elapsedMs > Config::MAX_ENERGY_INTEGRATION_GAP_MS) {
        previous_ = sample;
        return;
    }

    const float elapsedHours = static_cast<float>(elapsedMs) / MS_PER_HOUR;
    totals_.netAh += (previous_.current + sample.current) * 0.5f * elapsedHours;
    totals_.netWh += (previous_.power + sample.power) * 0.5f * elapsedHours;

    accumulateDirectional(
        previous_.current,
        sample.current,
        elapsedHours,
        totals_.dischargedAh,
        totals_.chargedAh
    );
    accumulateDirectional(
        previous_.power,
        sample.power,
        elapsedHours,
        totals_.dischargedWh,
        totals_.chargedWh
    );

    previous_ = sample;
}

void EnergyAccumulator::reset()
{
    totals_ = EnergyTotals{};
    hasPrevious_ = false;
}

void EnergyAccumulator::accumulateDirectional(
    float start,
    float end,
    float hours,
    float& positive,
    float& negative)
{
    if (start >= 0.0f && end >= 0.0f) {
        positive += (start + end) * 0.5f * hours;
        return;
    }

    if (start <= 0.0f && end <= 0.0f) {
        negative += -(start + end) * 0.5f * hours;
        return;
    }

    // Split a linear sample interval exactly at its zero crossing so charging
    // and discharging totals never cancel each other during a sign change.
    const float positiveFraction = start > 0.0f
        ? start / (start - end)
        : -end / (start - end);
    const float positiveHours = hours * positiveFraction;
    const float negativeHours = hours - positiveHours;

    if (start > 0.0f) {
        positive += start * 0.5f * positiveHours;
        negative += -end * 0.5f * negativeHours;
    } else {
        negative += -start * 0.5f * negativeHours;
        positive += end * 0.5f * positiveHours;
    }
}
