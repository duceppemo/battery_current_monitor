#pragma once

#include <atomic>

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "energy/EnergyAccumulator.h"
#include "energy/StateOfChargeEstimator.h"
#include "alarm/AlarmSettings.h"
#include "measurement/CalibrationSettings.h"
#include "network/WifiSettings.h"
#include "ota/FirmwareUpdateService.h"
#include "protection/LoadProtection.h"
#include "sensors/Ina228Sensor.h"
#include "telemetry/TelemetryStore.h"

class BleTelemetryService
{
public:
    enum class ControlResult : uint8_t {
        Applied = 1,
        Rejected = 2,
        Failed = 3,
    };

    BleTelemetryService();

    void begin(FirmwareUpdateService& firmwareUpdate);
    void publish(const TelemetryStore& store, const EnergyTotals& energy,
                 const Ina228Sensor& sensor, const CurrentCalibration& calibration,
                 const DeviceAlarmSettings& alarms, const DeviceAlarmState& alarmState,
                 bool calibrationStored, bool displayOn, bool accessPointReady,
                 const char* resetReason, uint8_t wifiClients,
                 bool stationConfigured, bool stationConnected, bool mdnsReady,
                 const uint8_t stationIp[4],
                 const BatteryProfileSettings& batteryProfile,
                 const StateOfChargeEstimator& stateOfCharge,
                 const LoadProtectionConfig& loadProtection,
                 const LoadProtectionMonitor& loadProtectionMonitor);
    void maintain();

    bool consumeResetExtremaRequested(uint16_t& requestId);
    bool consumeSessionResetRequested(uint16_t& requestId);
    bool consumeDisplayToggleRequested(uint16_t& requestId);
    bool consumeCalibrationSaveRequested(CurrentCalibration& calibration, uint16_t& requestId);
    bool consumeCalibrationResetRequested(uint16_t& requestId);
    bool consumeAlarmSaveRequested(DeviceAlarmSettings& settings, uint16_t& requestId);
    bool consumeWifiSaveRequested(WifiStationSettings& settings, uint16_t& requestId);
    bool consumeWifiClearRequested(uint16_t& requestId);
    bool consumeBatteryProfileSaveRequested(BatteryProfileSettings& settings, uint16_t& requestId);
    bool consumeBatterySyncRequested(uint16_t& requestId);
    bool consumeBatteryHistoryResetRequested(uint16_t& requestId);
    bool consumeLoadProtectionSaveRequested(LoadProtectionConfig& settings, uint16_t& requestId);
    bool consumeLoadProtectionReconnectRequested(uint16_t& requestId);
    bool consumeLoadProtectionTestConnectRequested(uint16_t& requestId);
    bool consumeLoadProtectionTestDisconnectRequested(uint16_t& requestId);
    void reportControlResult(uint8_t command, uint16_t requestId, ControlResult result);

    bool connected() const { return connected_.load(); }
    bool advertising() const { return advertising_.load(); }

private:
    class ServerCallbacks : public BLEServerCallbacks
    {
    public:
        explicit ServerCallbacks(BleTelemetryService& owner) : owner_(owner) {}
        void onConnect(BLEServer* server) override;
        void onDisconnect(BLEServer* server) override;

    private:
        BleTelemetryService& owner_;
    };

    class ControlCallbacks : public BLECharacteristicCallbacks
    {
    public:
        explicit ControlCallbacks(BleTelemetryService& owner) : owner_(owner) {}
        void onWrite(BLECharacteristic* characteristic) override;

    private:
        BleTelemetryService& owner_;
    };

    class FirmwareTransferCallbacks : public BLECharacteristicCallbacks
    {
    public:
        explicit FirmwareTransferCallbacks(BleTelemetryService& owner) : owner_(owner) {}
        void onWrite(BLECharacteristic* characteristic) override;

    private:
        BleTelemetryService& owner_;
    };

    enum class PendingCommand : uint8_t {
        None,
        ResetExtrema,
        ResetSession,
        ToggleDisplay,
        SaveCalibration,
        ResetCalibration,
        SaveAlarms,
        SaveWifi,
        ClearWifi,
        SaveBatteryProfile,
        SyncBatteryFull,
        ResetBatteryHistory,
        SaveLoadProtection,
        ReconnectLoad,
        TestConnectLoad,
        TestDisconnectLoad,
        Writing = 255
    };

    static BLECharacteristic* createCharacteristic(
        BLEService* service,
        const char* uuid,
        const char* description
    );

    static void updateCharacteristic(
        BLECharacteristic* characteristic,
        const char* value,
        bool notify
    );
    static void updateBinaryCharacteristic(
        BLECharacteristic* characteristic,
        const uint8_t* value,
        size_t length,
        bool notify
    );
    static BLECharacteristic* createControlCharacteristic(BLEService* service);
    static BLECharacteristic* createControlStatusCharacteristic(BLEService* service);
    static BLECharacteristic* createFirmwareTransferCharacteristic(BLEService* service);
    void startAdvertising();
    void publishDashboardPackets(
        const TelemetryStore& store,
        const EnergyTotals& energy,
        const Ina228Sensor& sensor,
        const CurrentCalibration& calibration,
        const DeviceAlarmSettings& alarms,
        const DeviceAlarmState& alarmState,
        bool calibrationStored,
        bool displayOn,
        bool accessPointReady,
        const char* resetReason,
        uint8_t wifiClients,
        bool stationConfigured,
        bool stationConnected,
        bool mdnsReady,
        const uint8_t stationIp[4],
        const BatteryProfileSettings& batteryProfile,
        const StateOfChargeEstimator& stateOfCharge,
        const LoadProtectionConfig& loadProtection,
        const LoadProtectionMonitor& loadProtectionMonitor,
        bool notify
    );
    void publishFirmwareUpdateStatus(bool notify);
    void publishControlStatus(bool notify);
    bool consumeCommand(PendingCommand command, uint16_t& requestId);

    ServerCallbacks callbacks_;
    ControlCallbacks controlCallbacks_;
    FirmwareTransferCallbacks firmwareTransferCallbacks_;
    FirmwareUpdateService* firmwareUpdate_ = nullptr;
    BLEServer* server_ = nullptr;
    BLECharacteristic* voltageCharacteristic_ = nullptr;
    BLECharacteristic* currentCharacteristic_ = nullptr;
    BLECharacteristic* powerCharacteristic_ = nullptr;
    BLECharacteristic* temperatureCharacteristic_ = nullptr;
    BLECharacteristic* ampHourCharacteristic_ = nullptr;
    BLECharacteristic* wattHourCharacteristic_ = nullptr;
    BLECharacteristic* statusCharacteristic_ = nullptr;
    BLECharacteristic* telemetryCharacteristic_ = nullptr;
    BLECharacteristic* binaryTelemetryCharacteristic_ = nullptr;
    BLECharacteristic* dashboardCharacteristic_ = nullptr;
    BLECharacteristic* controlCharacteristic_ = nullptr;
    BLECharacteristic* controlStatusCharacteristic_ = nullptr;
    BLECharacteristic* deviceInfoCharacteristic_ = nullptr;
    BLECharacteristic* firmwareTransferCharacteristic_ = nullptr;
    BLECharacteristic* firmwareUpdateStatusCharacteristic_ = nullptr;

    std::atomic_bool connected_{false};
    std::atomic_bool advertising_{false};
    std::atomic_bool connectionStateChanged_{false};
    std::atomic_bool advertisingRestartRequested_{false};
    std::atomic_uint8_t pendingCommand_{static_cast<uint8_t>(PendingCommand::None)};
    std::atomic_uint16_t pendingRequestId_{0};
    std::atomic_int32_t pendingResistanceMicroOhms_{0};
    std::atomic_int32_t pendingOffsetNanoVolts_{0};
    std::atomic_int32_t pendingGainPpm_{0};
    std::atomic_uint8_t pendingAlarmFlags_{0};
    std::atomic_int32_t pendingLowVoltageMv_{0};
    std::atomic_int32_t pendingHighVoltageMv_{0};
    std::atomic_int32_t pendingCurrentMa_{0};
    std::atomic_int32_t pendingTemperatureDeciC_{0};
    // Guarded by the pendingCommand_ Writing/SaveWifi transition, same as the
    // atomic pending* fields above; a plain struct is fine here because that
    // transition already provides the needed happens-before edge between the
    // BLE callback and the consuming main-loop read.
    WifiStationSettings pendingWifiSettings_;
    BatteryProfileSettings pendingBatteryProfile_;
    std::atomic_uint8_t pendingProtectionEnabled_{0};
    std::atomic_int32_t pendingProtectionLowVoltageMv_{0};
    std::atomic_int32_t pendingProtectionLowSocDeciPercent_{0};
    std::atomic_uint8_t controlStatusCommand_{0};
    std::atomic_uint16_t controlStatusRequestId_{0};
    std::atomic_uint8_t controlStatusResult_{0};
    std::atomic_bool controlStatusDirty_{false};
    uint8_t dashboardPacketIndex_ = 0;
    bool loggedConnected_ = false;
};
