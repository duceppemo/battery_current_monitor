#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "alarm/AlarmSettings.h"
#include "energy/EnergyAccumulator.h"
#include "measurement/CalibrationSettings.h"
#include "network/WifiSettings.h"
#include "ota/FirmwareUpdateService.h"
#include "sensors/Ina228Sensor.h"
#include "telemetry/TelemetryStore.h"

class WebDashboard
{
public:
    void begin(
        TelemetryStore& store,
        const EnergyTotals& energyTotals,
        const Ina228Sensor& sensor,
        const CalibrationSettings& calibration,
        const AlarmSettings& alarms,
        const AlarmMonitor& alarmMonitor,
        FirmwareUpdateService& firmwareUpdate
    );
    void update();
    bool consumeDisplayToggleRequested();
    bool consumeSessionResetRequested();
    bool consumeCalibrationSaveRequested(CurrentCalibration& calibration);
    bool consumeCalibrationResetRequested();
    bool consumeAlarmSaveRequested(DeviceAlarmSettings& settings);
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
    enum class PendingCommand : uint8_t {
        None,
        ToggleDisplay,
        ResetSession,
        SaveCalibration,
        ResetCalibration,
        SaveAlarms
    };

    void handleRoot();
    void handleTelemetry();
    void handleResetExtrema();
    void handleResetSession();
    void handleToggleDisplay();
    void handleCalibrationSave();
    void handleCalibrationReset();
    void handleAlarmSave();
    void handleWifiSave();
    void handleWifiClear();
    void handleFirmwareUpload();
    void handleNotFound();
    bool startAccessPoint();
    void startStation(uint32_t nowMs);
    void maintainAccessPoint(uint32_t nowMs);
    void maintainStation(uint32_t nowMs);
    bool queueCommand(PendingCommand command);
    bool consumeCommand(PendingCommand command);

    static void appendNullableFloat(
        String& json,
        bool valid,
        float value,
        uint8_t decimals
    );
    static void appendUnsigned(String& json, uint32_t value);
    static void appendJsonString(String& json, const char* value);

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
    const AlarmSettings* alarms_ = nullptr;
    const AlarmMonitor* alarmMonitor_ = nullptr;
    FirmwareUpdateService* firmwareUpdate_ = nullptr;
    String telemetryJson_;

    bool running_ = false;
    bool bleConnected_ = false;
    bool bleAdvertising_ = false;
    bool displayOn_ = true;
    const char* resetReason_ = "unknown";
    bool accessPointReady_ = false;
    uint32_t lastAccessPointCheckMs_ = 0;
    WifiSettings wifiSettings_;
    bool stationConnected_ = false;
    bool mdnsReady_ = false;
    uint32_t stationAttemptStartedMs_ = 0;
    uint32_t lastStationAttemptMs_ = 0;
    PendingCommand pendingCommand_ = PendingCommand::None;
    CurrentCalibration pendingCalibration_;
    DeviceAlarmSettings pendingAlarms_;
    char calibrationStatus_[48] = "unchanged";
    uint32_t successfulSamples_ = 0;
    uint32_t failedSamples_ = 0;
    bool firmwareUpdateSucceeded_ = false;
    char firmwareUpdateError_[64] = "";
    uint32_t restartAfterMs_ = 0;
};
