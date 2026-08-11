#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "energy/EnergyAccumulator.h"
#include "measurement/CalibrationSettings.h"
#include "sensors/Ina228Sensor.h"
#include "telemetry/TelemetryStore.h"

class WebDashboard
{
public:
    void begin(
        TelemetryStore& store,
        const EnergyTotals& energyTotals,
        const Ina228Sensor& sensor,
        const CalibrationSettings& calibration
    );
    void update();
    bool consumeDisplayToggleRequested();
    bool consumeSessionResetRequested();
    bool consumeCalibrationSaveRequested(CurrentCalibration& calibration);
    bool consumeCalibrationResetRequested();
    void setCalibrationStatus(const char* status);

    void setRuntimeStatus(
        bool bleConnected,
        bool bleAdvertising,
        bool displayOn,
        const char* resetReason,
        uint32_t successfulSamples,
        uint32_t failedSamples
    );

    uint8_t clientCount() const;
    bool running() const { return running_; }
    bool accessPointReady() const { return accessPointReady_; }

private:
    void handleRoot();
    void handleTelemetry();
    void handleResetExtrema();
    void handleResetSession();
    void handleToggleDisplay();
    void handleCalibrationSave();
    void handleCalibrationReset();
    void handleNotFound();
    bool startAccessPoint();
    void maintainAccessPoint(uint32_t nowMs);

    static void appendNullableFloat(
        String& json,
        bool valid,
        float value,
        uint8_t decimals
    );
    static void appendUnsigned(String& json, uint32_t value);

    static void appendMetric(
        String& json,
        const char* name,
        bool valid,
        float value,
        const MetricStats& stats,
        uint8_t decimals
    );

    WebServer server_{80};
    TelemetryStore* store_ = nullptr;
    const EnergyTotals* energyTotals_ = nullptr;
    const Ina228Sensor* sensor_ = nullptr;
    const CalibrationSettings* calibration_ = nullptr;
    String telemetryJson_;

    bool running_ = false;
    bool bleConnected_ = false;
    bool bleAdvertising_ = false;
    bool displayOn_ = true;
    const char* resetReason_ = "unknown";
    bool accessPointReady_ = false;
    uint32_t lastAccessPointCheckMs_ = 0;
    bool displayToggleRequested_ = false;
    bool sessionResetRequested_ = false;
    bool calibrationSaveRequested_ = false;
    bool calibrationResetRequested_ = false;
    CurrentCalibration pendingCalibration_;
    const char* calibrationStatus_ = "unchanged";
    uint32_t successfulSamples_ = 0;
    uint32_t failedSamples_ = 0;
};
