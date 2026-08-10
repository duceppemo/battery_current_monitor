#pragma once

#include <Arduino.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "energy/EnergyAccumulator.h"
#include "telemetry/TelemetryStore.h"

class BleTelemetryService
{
public:
    BleTelemetryService();

    void begin();
    void publish(const TelemetryStore& store, const EnergyTotals& energy,
                 uint32_t failedSamples, uint8_t wifiClients);
    void maintain();

    bool connected() const { return connected_; }
    bool advertising() const { return advertising_; }

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
    void startAdvertising();

    ServerCallbacks callbacks_;
    BLEServer* server_ = nullptr;
    BLECharacteristic* voltageCharacteristic_ = nullptr;
    BLECharacteristic* currentCharacteristic_ = nullptr;
    BLECharacteristic* powerCharacteristic_ = nullptr;
    BLECharacteristic* temperatureCharacteristic_ = nullptr;
    BLECharacteristic* ampHourCharacteristic_ = nullptr;
    BLECharacteristic* wattHourCharacteristic_ = nullptr;
    BLECharacteristic* statusCharacteristic_ = nullptr;
    BLECharacteristic* telemetryCharacteristic_ = nullptr;

    bool connected_ = false;
    bool previouslyConnected_ = false;
    bool advertising_ = false;
};
