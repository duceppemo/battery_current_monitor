#include "ble/BleTelemetryService.h"

#include <cmath>
#include <cstdint>
#include <limits>

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
    constexpr char BINARY_TELEMETRY_UUID[] =
        "7d9f0009-9c65-4d3d-bdd5-8f4c6b2e1000";

    // This is deliberately limited to the universally supported initial ATT
    // notification payload (20 bytes). It avoids making first connection and
    // live updates depend on the phone negotiating a larger MTU.
    constexpr size_t BINARY_TELEMETRY_SIZE = 20;
    constexpr uint8_t BINARY_TELEMETRY_VERSION = 1;
    constexpr uint8_t FLAG_VOLTAGE_VALID = 1 << 0;
    constexpr uint8_t FLAG_CURRENT_VALID = 1 << 1;
    constexpr uint8_t FLAG_POWER_VALID = 1 << 2;
    constexpr uint8_t FLAG_TEMPERATURE_VALID = 1 << 3;

    int32_t roundAndClamp(float value, float scale, int32_t minimum, int32_t maximum)
    {
        if (!std::isfinite(value)) {
            return 0;
        }

        const float scaled = value * scale;
        if (scaled <= static_cast<float>(minimum)) {
            return minimum;
        }
        if (scaled >= static_cast<float>(maximum)) {
            return maximum;
        }
        return static_cast<int32_t>(lroundf(scaled));
    }

    void writeUint16LE(uint8_t* destination, uint16_t value)
    {
        destination[0] = static_cast<uint8_t>(value);
        destination[1] = static_cast<uint8_t>(value >> 8);
    }

    void writeInt24LE(uint8_t* destination, int32_t value)
    {
        const uint32_t encoded = static_cast<uint32_t>(value) & 0x00FFFFFFUL;
        destination[0] = static_cast<uint8_t>(encoded);
        destination[1] = static_cast<uint8_t>(encoded >> 8);
        destination[2] = static_cast<uint8_t>(encoded >> 16);
    }

    void writeInt32LE(uint8_t* destination, int32_t value)
    {
        const uint32_t encoded = static_cast<uint32_t>(value);
        destination[0] = static_cast<uint8_t>(encoded);
        destination[1] = static_cast<uint8_t>(encoded >> 8);
        destination[2] = static_cast<uint8_t>(encoded >> 16);
        destination[3] = static_cast<uint8_t>(encoded >> 24);
    }

    void encodeBinaryTelemetry(
        uint8_t* packet,
        const Telemetry& telemetry,
        const EnergyTotals& energy)
    {
        uint8_t flags = 0;
        if (telemetry.voltageValid()) {
            flags |= FLAG_VOLTAGE_VALID;
        }
        if (telemetry.currentValid()) {
            flags |= FLAG_CURRENT_VALID;
        }
        if (telemetry.powerOK()) {
            flags |= FLAG_POWER_VALID;
        }
        if (telemetry.temperatureValid()) {
            flags |= FLAG_TEMPERATURE_VALID;
        }

        packet[0] = static_cast<uint8_t>((BINARY_TELEMETRY_VERSION << 4) | flags);
        writeUint16LE(packet + 1, static_cast<uint16_t>(telemetry.sequence));
        writeUint16LE(packet + 3, static_cast<uint16_t>(roundAndClamp(
            telemetry.voltage, 1000.0f, 0, std::numeric_limits<uint16_t>::max())));
        writeInt24LE(packet + 5, roundAndClamp(
            telemetry.current, 1000.0f, -8388608, 8388607));
        writeInt24LE(packet + 8, roundAndClamp(
            telemetry.power, 1000.0f, -8388608, 8388607));
        packet[11] = static_cast<uint8_t>(roundAndClamp(
            telemetry.temperature, 1.0f, -128, 127));
        writeInt32LE(packet + 12, roundAndClamp(
            energy.netAh, 1000.0f,
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 16, roundAndClamp(
            energy.netWh, 1000.0f,
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
    }
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
    binaryTelemetryCharacteristic_ = createCharacteristic(
        service, BINARY_TELEMETRY_UUID, "Binary Telemetry v1 (20-byte LE)");

    voltageCharacteristic_->setValue("0.000");
    currentCharacteristic_->setValue("0.000000");
    powerCharacteristic_->setValue("0.000000");
    temperatureCharacteristic_->setValue("0.0");
    ampHourCharacteristic_->setValue("0.000000");
    wattHourCharacteristic_->setValue("0.000000");
    statusCharacteristic_->setValue("BOOT");
    telemetryCharacteristic_->setValue("V=0;I=0;P=0;T=0;Ah=0;Wh=0");
    const uint8_t initialBinaryTelemetry[BINARY_TELEMETRY_SIZE] = {
        static_cast<uint8_t>(BINARY_TELEMETRY_VERSION << 4)
    };
    binaryTelemetryCharacteristic_->setValue(initialBinaryTelemetry, BINARY_TELEMETRY_SIZE);

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

void BleTelemetryService::updateBinaryCharacteristic(
    BLECharacteristic* characteristic,
    const uint8_t* value,
    size_t length,
    bool notify)
{
    characteristic->setValue(value, length);
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

    uint8_t binaryTelemetry[BINARY_TELEMETRY_SIZE] = {};
    encodeBinaryTelemetry(binaryTelemetry, telemetry, energy);
    updateBinaryCharacteristic(binaryTelemetryCharacteristic_, binaryTelemetry,
                               sizeof(binaryTelemetry), notify);
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
