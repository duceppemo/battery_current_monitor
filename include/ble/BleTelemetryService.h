#pragma once

#include <atomic>

#include <Arduino.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "energy/EnergyAccumulator.h"
#include "measurement/CalibrationSettings.h"
#include "sensors/Ina228Sensor.h"
#include "telemetry/TelemetryStore.h"

class BleTelemetryService
{
public:
    BleTelemetryService();

    void begin();
    void publish(const TelemetryStore& store, const EnergyTotals& energy,
                 const Ina228Sensor& sensor, const CurrentCalibration& calibration,
                 bool calibrationStored, bool displayOn, bool accessPointReady,
                 const char* resetReason, uint8_t wifiClients);
    void maintain();

    bool consumeResetExtremaRequested();
    bool consumeSessionResetRequested();
    bool consumeDisplayToggleRequested();
    bool consumeCalibrationSaveRequested(CurrentCalibration& calibration);
    bool consumeCalibrationResetRequested();

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

    enum class PendingCommand : uint8_t {
        None,
        ResetExtrema,
        ResetSession,
        ToggleDisplay,
        SaveCalibration,
        ResetCalibration
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
    void startAdvertising();
    void publishDashboardPackets(
        const TelemetryStore& store,
        const EnergyTotals& energy,
        const Ina228Sensor& sensor,
        const CurrentCalibration& calibration,
        bool calibrationStored,
        bool displayOn,
        bool accessPointReady,
        const char* resetReason,
        uint8_t wifiClients,
        bool notify
    );
    bool consumeCommand(PendingCommand command);

    ServerCallbacks callbacks_;
    ControlCallbacks controlCallbacks_;
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

    std::atomic_bool connected_{false};
    std::atomic_bool advertising_{false};
    std::atomic_bool connectionStateChanged_{false};
    std::atomic_bool advertisingRestartRequested_{false};
    std::atomic_uint8_t pendingCommand_{static_cast<uint8_t>(PendingCommand::None)};
    std::atomic_int32_t pendingResistanceMicroOhms_{0};
    std::atomic_int32_t pendingOffsetNanoVolts_{0};
    std::atomic_int32_t pendingGainPpm_{0};
    uint8_t dashboardPacketIndex_ = 0;
    bool loggedConnected_ = false;
};
