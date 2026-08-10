#pragma once

#include <Arduino.h>

#include "ble/BleTelemetryService.h"
#include "display/OledDisplay.h"
#include "energy/EnergyAccumulator.h"
#include "input/DebouncedButton.h"
#include "sensors/Ina228Sensor.h"
#include "telemetry/TelemetryStore.h"
#include "web/WebDashboard.h"

class BatteryMonitorApp
{
public:
    BatteryMonitorApp();

    void begin();
    void update();

private:
    void scanI2C();
    void updateButtons(uint32_t nowMs);
    void updateMeasurement(uint32_t nowMs);
    void updateDisplay(uint32_t nowMs);
    void updateBle(uint32_t nowMs);
    void updateSerial(uint32_t nowMs);
    void printDiagnostics() const;
    void resetPhysicalSessionState();

    Ina228Sensor sensor_;
    TelemetryStore telemetry_;
    EnergyAccumulator energy_;
    OledDisplay display_;
    BleTelemetryService ble_;
    WebDashboard web_;

    DebouncedButton resetExtremaButton_;
    DebouncedButton displayToggleButton_;

    uint32_t lastMeasurementMs_ = 0;
    uint32_t lastDisplayMs_ = 0;
    uint32_t lastBleMs_ = 0;
    uint32_t lastSerialMs_ = 0;
    uint32_t measurementSequence_ = 0;
    const char* resetReason_ = "unknown";
    bool displayPageChangedOnPress_ = false;
};
