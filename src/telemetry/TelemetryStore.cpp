#include "telemetry/TelemetryStore.h"

void TelemetryStore::update(const Telemetry& telemetry)
{
    current_ = telemetry;
    updateExtrema();
}

void TelemetryStore::updateExtrema()
{
    if (current_.voltageValid()) {
        voltageStats_.update(current_.voltage);
    }

    if (current_.shuntVoltageValid()) {
        shuntStats_.update(current_.shuntVoltage);
    }

    if (current_.currentValid()) {
        currentStats_.update(current_.current);
    }

    if (current_.powerOK()) {
        powerStats_.update(current_.power);
    }

    if (current_.temperatureValid()) {
        temperatureStats_.update(current_.temperature);
    }
}

void TelemetryStore::resetExtrema()
{
    voltageStats_.reset();
    shuntStats_.reset();
    currentStats_.reset();
    powerStats_.reset();
    temperatureStats_.reset();

    // Seed the fresh extrema set from the current valid sample.
    updateExtrema();
}
