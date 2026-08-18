#pragma once

#include <Arduino.h>

#include "alarm/AlarmSettings.h"

#include "ble/BleTelemetryService.h"
#include "display/OledDisplay.h"
#include "energy/EnergyAccumulator.h"
#include "energy/StateOfChargeEstimator.h"
#include "input/DebouncedButton.h"
#include "measurement/CalibrationSettings.h"
#include "mqtt/MqttPublisher.h"
#include "notify/NtfyNotifier.h"
#include "ota/FirmwareUpdateService.h"
#include "protection/LoadProtection.h"
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
    void updateMqtt(uint32_t nowMs);
    void updateNtfy(uint32_t nowMs);
    void updateSerial(uint32_t nowMs);
    void printDiagnostics() const;
    void resetPhysicalSessionState();

    Ina228Sensor sensor_;
    CalibrationSettings calibration_;
    AlarmSettings alarms_;
    AlarmMonitor alarmMonitor_;
    TelemetryStore telemetry_;
    EnergyAccumulator energy_;
    BatteryProfile batteryProfile_;
    StateOfChargeEstimator stateOfCharge_;
    OledDisplay display_;
    BleTelemetryService ble_;
    FirmwareUpdateService firmwareUpdate_;
    WebDashboard web_;
    MqttSettings mqttSettings_;
    MqttPublisher mqttPublisher_;
    NtfySettings ntfySettings_;
    NtfyNotifier ntfyNotifier_;
    LoadProtectionSettings loadProtectionSettings_;
    LoadProtectionMonitor loadProtectionMonitor_;
    EnergyPersistenceSettings energyPersistenceSettings_;

    DebouncedButton resetExtremaButton_;
    DebouncedButton displayToggleButton_;

    uint32_t lastMeasurementMs_ = 0;
    uint32_t lastDisplayMs_ = 0;
    uint32_t lastBleMs_ = 0;
    uint32_t lastSerialMs_ = 0;
    uint32_t firmwareRestartAtMs_ = 0;
    uint32_t measurementSequence_ = 0;
    const char* resetReason_ = "unknown";
};
