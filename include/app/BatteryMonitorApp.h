#pragma once

#include <Arduino.h>

#include "ble/BleTelemetryService.h"
#include "display/OledDisplay.h"
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

    Ina228Sensor sensor_;
    TelemetryStore telemetry_;
    OledDisplay display_;
    BleTelemetryService ble_;
    WebDashboard web_;

    DebouncedButton resetExtremaButton_;
    DebouncedButton displayToggleButton_;

    uint32_t lastMeasurementMs_ = 0;
    uint32_t lastDisplayMs_ = 0;
    uint32_t lastBleMs_ = 0;
    uint32_t lastSerialMs_ = 0;
};
