#include "ble/BleTelemetryService.h"

#include "AppConfig.h"

namespace
{
    constexpr char SERVICE_UUID[] =
        "7d9f0000-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char VOLTAGE_UUID[] =
        "7d9f0001-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char CURRENT_UUID[] =
        "7d9f0002-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char POWER_UUID[] =
        "7d9f0003-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char TEMPERATURE_UUID[] =
        "7d9f0004-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char AMP_HOUR_UUID[] =
        "7d9f0007-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char WATT_HOUR_UUID[] =
        "7d9f0008-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char STATUS_UUID[] =
        "7d9f0005-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char TELEMETRY_UUID[] =
        "7d9f0006-9c65-4d3d-bdd5-8f4c6b2e1000";
}

BleTelemetryService::BleTelemetryService()
    : callbacks_(*this)
{
}

void BleTelemetryService::ServerCallbacks::onConnect(BLEServer*)
{
    owner_.connected_.store(true);
    owner_.advertising_.store(false);
    owner_.advertisingRestartRequested_.store(false);
    owner_.connectionStateChanged_.store(true);
}

void BleTelemetryService::ServerCallbacks::onDisconnect(BLEServer*)
{
    owner_.connected_.store(false);
    owner_.advertising_.store(false);
    owner_.advertisingRestartRequested_.store(true);
    owner_.connectionStateChanged_.store(true);
}

BLECharacteristic* BleTelemetryService::createCharacteristic(
    BLEService* service,
    const char* uuid,
    const char* description)
{
    BLECharacteristic* characteristic = service->createCharacteristic(
        uuid,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    BLEDescriptor* userDescription = new BLEDescriptor("2901");
    userDescription->setValue(
        reinterpret_cast<const uint8_t*>(description),
        strlen(description)
    );
    characteristic->addDescriptor(userDescription);

    return characteristic;
}

void BleTelemetryService::begin()
{
    BLEDevice::init(Config::BLE_DEVICE_NAME);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(&callbacks_);

    BLEService* service = server_->createService(SERVICE_UUID);

    voltageCharacteristic_ = createCharacteristic(service, VOLTAGE_UUID, "Battery Voltage (V)");
    currentCharacteristic_ = createCharacteristic(service, CURRENT_UUID, "Current (A)");
    powerCharacteristic_ = createCharacteristic(service, POWER_UUID, "Power (W)");
    temperatureCharacteristic_ = createCharacteristic(service, TEMPERATURE_UUID, "INA228 Temperature (C)");
    ampHourCharacteristic_ = createCharacteristic(service, AMP_HOUR_UUID, "Net Session Charge (Ah; positive = discharge)");
    wattHourCharacteristic_ = createCharacteristic(service, WATT_HOUR_UUID, "Net Session Energy (Wh; positive = discharge)");
    statusCharacteristic_ = createCharacteristic(service, STATUS_UUID, "Monitor Status");
    telemetryCharacteristic_ = createCharacteristic(service, TELEMETRY_UUID, "Combined Telemetry");

    voltageCharacteristic_->setValue("0.000");
    currentCharacteristic_->setValue("0.000000");
    powerCharacteristic_->setValue("0.000000");
    temperatureCharacteristic_->setValue("0.0");
    ampHourCharacteristic_->setValue("0.000000");
    wattHourCharacteristic_->setValue("0.000000");
    statusCharacteristic_->setValue("BOOT");
    telemetryCharacteristic_->setValue("V=0;I=0;P=0;T=0;Ah=0;Wh=0");

    service->start();

    startAdvertising();
}

void BleTelemetryService::startAdvertising()
{
    if (connected_.load()) {
        return;
    }

    BLEAdvertising* advertising = BLEDevice::getAdvertising();

    // A 128-bit service UUID and device name do not both fit in the 31-byte
    // primary advertising packet. Put the name in the primary packet so every
    // scanner can identify the device; expose the custom service in the scan
    // response.
    BLEAdvertisementData advertisementData;
    advertisementData.setName(Config::BLE_DEVICE_NAME);
    advertising->setAdvertisementData(advertisementData);

    BLEAdvertisementData scanResponseData;
    scanResponseData.setCompleteServices(BLEUUID(SERVICE_UUID));
    advertising->setScanResponseData(scanResponseData);
    advertising->setScanResponse(true);

    BLEDevice::startAdvertising();
    advertising_.store(true);
    advertisingRestartRequested_.store(false);
    Serial.printf("BLE advertising as \"%s\"\n", Config::BLE_DEVICE_NAME);
}

void BleTelemetryService::updateCharacteristic(
    BLECharacteristic* characteristic,
    const char* value,
    bool notify)
{
    characteristic->setValue(value);
    if (notify) {
        characteristic->notify();
    }
}

void BleTelemetryService::publish(
    const TelemetryStore& store,
    const EnergyTotals& energy,
    uint32_t failedSamples,
    uint8_t wifiClients)
{
    const Telemetry& telemetry = store.current();
    const bool notify = connected();
    char buffer[128];

    if (telemetry.voltageValid()) {
        snprintf(buffer, sizeof(buffer), "%.3f", telemetry.voltage);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(voltageCharacteristic_, buffer, notify);

    if (telemetry.currentValid()) {
        snprintf(buffer, sizeof(buffer), "%.6f", telemetry.current);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(currentCharacteristic_, buffer, notify);

    if (telemetry.powerOK()) {
        snprintf(buffer, sizeof(buffer), "%.6f", telemetry.power);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(powerCharacteristic_, buffer, notify);

    if (telemetry.temperatureValid()) {
        snprintf(buffer, sizeof(buffer), "%.1f", telemetry.temperature);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(temperatureCharacteristic_, buffer, notify);

    snprintf(buffer, sizeof(buffer), "%.6f", energy.netAh);
    updateCharacteristic(ampHourCharacteristic_, buffer, notify);

    snprintf(buffer, sizeof(buffer), "%.6f", energy.netWh);
    updateCharacteristic(wattHourCharacteristic_, buffer, notify);

    snprintf(
        buffer,
        sizeof(buffer),
        "sensor=%s;failedSamples=%lu;wifi=%u",
        telemetry.sensorOK() ? "OK" : "ERR",
        static_cast<unsigned long>(failedSamples),
        static_cast<unsigned>(wifiClients)
    );
    // These values can exceed the initial ATT notification payload. Keep them
    // readable without sending MTU-dependent truncated notifications.
    updateCharacteristic(statusCharacteristic_, buffer, false);

    if (telemetry.sensorOK()) {
        snprintf(
            buffer,
            sizeof(buffer),
            "V=%.3f;I=%.6f;P=%.6f;T=%.1f;Ah=%.6f;Wh=%.6f;F=%lu",
            telemetry.voltage,
            telemetry.current,
            telemetry.power,
            telemetry.temperature,
            energy.netAh,
            energy.netWh,
            static_cast<unsigned long>(failedSamples)
        );
    } else {
        snprintf(
            buffer,
            sizeof(buffer),
            "SENSOR_ERROR;F=%lu",
            static_cast<unsigned long>(failedSamples)
        );
    }
    updateCharacteristic(telemetryCharacteristic_, buffer, false);
}

void BleTelemetryService::maintain()
{
    const bool connected = connected_.load();
    if (connectionStateChanged_.exchange(false) && connected != loggedConnected_) {
        Serial.println(connected ? "BLE client connected." : "BLE client disconnected.");
        loggedConnected_ = connected;
    }

    // A very short connect/disconnect can happen entirely between two loop
    // passes. The callback-owned restart request preserves that event so BLE
    // advertising cannot remain stopped after the client leaves.
    if (advertisingRestartRequested_.exchange(false) && !connected_.load() &&
        server_ != nullptr) {
        startAdvertising();
    }
}
