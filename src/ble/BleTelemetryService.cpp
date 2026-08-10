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
    owner_.connected_ = true;
    Serial.println("BLE client connected.");
}

void BleTelemetryService::ServerCallbacks::onDisconnect(BLEServer*)
{
    owner_.connected_ = false;
    Serial.println("BLE client disconnected.");
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
    statusCharacteristic_ = createCharacteristic(service, STATUS_UUID, "Monitor Status");
    telemetryCharacteristic_ = createCharacteristic(service, TELEMETRY_UUID, "Combined Telemetry");

    voltageCharacteristic_->setValue("0.000");
    currentCharacteristic_->setValue("0.000000");
    powerCharacteristic_->setValue("0.000000");
    temperatureCharacteristic_->setValue("0.0");
    statusCharacteristic_->setValue("BOOT");
    telemetryCharacteristic_->setValue("V=0;I=0;P=0;T=0;E=0");

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();

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
    uint32_t i2cErrors,
    uint8_t wifiClients)
{
    const Telemetry& telemetry = store.current();
    char buffer[128];

    if (telemetry.voltageOK) {
        snprintf(buffer, sizeof(buffer), "%.3f", telemetry.voltage);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(voltageCharacteristic_, buffer, connected_);

    if (telemetry.shuntOK) {
        snprintf(buffer, sizeof(buffer), "%.6f", telemetry.current);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(currentCharacteristic_, buffer, connected_);

    if (telemetry.voltageOK && telemetry.shuntOK) {
        snprintf(buffer, sizeof(buffer), "%.6f", telemetry.power);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(powerCharacteristic_, buffer, connected_);

    if (telemetry.temperatureOK) {
        snprintf(buffer, sizeof(buffer), "%.1f", telemetry.temperature);
    } else {
        snprintf(buffer, sizeof(buffer), "ERR");
    }
    updateCharacteristic(temperatureCharacteristic_, buffer, connected_);

    snprintf(
        buffer,
        sizeof(buffer),
        "sensor=%s;i2c=%lu;wifi=%u",
        telemetry.sensorOK() ? "OK" : "ERR",
        static_cast<unsigned long>(i2cErrors),
        static_cast<unsigned>(wifiClients)
    );
    updateCharacteristic(statusCharacteristic_, buffer, connected_);

    if (telemetry.sensorOK()) {
        snprintf(
            buffer,
            sizeof(buffer),
            "V=%.3f;I=%.6f;P=%.6f;T=%.1f;E=%lu",
            telemetry.voltage,
            telemetry.current,
            telemetry.power,
            telemetry.temperature,
            static_cast<unsigned long>(i2cErrors)
        );
    } else {
        snprintf(
            buffer,
            sizeof(buffer),
            "SENSOR_ERROR;E=%lu",
            static_cast<unsigned long>(i2cErrors)
        );
    }
    updateCharacteristic(telemetryCharacteristic_, buffer, connected_);
}

void BleTelemetryService::maintain()
{
    if (!connected_ && previouslyConnected_) {
        if (server_ != nullptr) {
            server_->startAdvertising();
        }
        Serial.println("BLE advertising restarted.");
        previouslyConnected_ = false;
    }

    if (connected_ && !previouslyConnected_) {
        previouslyConnected_ = true;
    }
}
