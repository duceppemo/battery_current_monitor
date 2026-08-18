#include "ble/BleTelemetryService.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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
    constexpr char DASHBOARD_UUID[] =
        "7d9f000a-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char CONTROL_UUID[] =
        "7d9f000b-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char DEVICE_INFO_UUID[] =
        "7d9f000c-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char FIRMWARE_TRANSFER_UUID[] =
        "7d9f000d-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char FIRMWARE_UPDATE_STATUS_UUID[] =
        "7d9f000e-9c65-4d3d-bdd5-8f4c6b2e1000";
    constexpr char CONTROL_STATUS_UUID[] =
        "7d9f000f-9c65-4d3d-bdd5-8f4c6b2e1000";

    // This is deliberately limited to the universally supported initial ATT
    // notification payload (20 bytes). It avoids making first connection and
    // live updates depend on the phone negotiating a larger MTU.
    constexpr size_t BINARY_TELEMETRY_SIZE = 20;
    constexpr uint8_t BINARY_TELEMETRY_VERSION = 1;
    constexpr uint8_t FLAG_VOLTAGE_VALID = 1 << 0;
    constexpr uint8_t FLAG_CURRENT_VALID = 1 << 1;
    constexpr uint8_t FLAG_POWER_VALID = 1 << 2;
    constexpr uint8_t FLAG_TEMPERATURE_VALID = 1 << 3;
    constexpr uint8_t DASHBOARD_EXTREMA = 0x11;
    constexpr uint8_t DASHBOARD_ENERGY = 0x12;
    constexpr uint8_t DASHBOARD_STATE = 0x13;
    constexpr uint8_t DASHBOARD_CALIBRATION = 0x14;
    constexpr uint8_t DASHBOARD_SHUNT = 0x15;
    constexpr uint8_t DASHBOARD_ALARMS = 0x16;
    constexpr uint8_t DASHBOARD_WIFI = 0x17;
    constexpr uint8_t DASHBOARD_SOC = 0x18;
    constexpr uint8_t DASHBOARD_PROTECTION = 0x19;
    constexpr uint8_t CONTROL_RESET_EXTREMA = 1;
    constexpr uint8_t CONTROL_RESET_SESSION = 2;
    constexpr uint8_t CONTROL_TOGGLE_DISPLAY = 3;
    constexpr uint8_t CONTROL_SAVE_CALIBRATION = 4;
    constexpr uint8_t CONTROL_RESET_CALIBRATION = 5;
    constexpr uint8_t CONTROL_SAVE_ALARMS = 6;
    constexpr uint8_t CONTROL_SAVE_WIFI = 7;
    constexpr uint8_t CONTROL_CLEAR_WIFI = 8;
    constexpr uint8_t CONTROL_SAVE_BATTERY_PROFILE = 9;
    constexpr uint8_t CONTROL_SYNC_BATTERY_FULL = 10;
    constexpr uint8_t CONTROL_RESET_BATTERY_HISTORY = 11;
    constexpr uint8_t CONTROL_SAVE_LOAD_PROTECTION = 12;
    constexpr uint8_t CONTROL_RECONNECT_LOAD = 13;
    constexpr uint8_t CONTROL_TEST_CONNECT_LOAD = 14;
    constexpr uint8_t CONTROL_TEST_DISCONNECT_LOAD = 15;
    constexpr uint8_t CONTROL_SAVE_ENERGY_PERSISTENCE = 16;
    constexpr size_t FIRMWARE_UPDATE_STATUS_SIZE = 12;
    constexpr size_t CONTROL_STATUS_SIZE = 6;

    // GATT attribute handle budget for the service below. Each NOTIFY
    // characteristic costs 4 handles (declaration + value + auto CCCD +
    // User Description descriptor); each write-only one costs 3 (no CCCD).
    // These counts must match the createCharacteristic()/create*Characteristic()
    // calls in begin() below. This is inherently a hand-maintained tally, not
    // a build-time guarantee, so verifyCharacteristicsRegistered() is the
    // actual safety net: it checks every characteristic's live GATT handle
    // after registration and fails loudly if this budget was ever wrong.
    constexpr uint32_t HANDLES_PER_NOTIFY_CHARACTERISTIC = 4;
    constexpr uint32_t HANDLES_PER_WRITE_ONLY_CHARACTERISTIC = 3;
    constexpr uint32_t NOTIFY_CHARACTERISTIC_COUNT = 13;
    constexpr uint32_t WRITE_ONLY_CHARACTERISTIC_COUNT = 2;
    constexpr uint32_t SERVICE_HANDLE_HEADROOM = 20;
    constexpr uint32_t SERVICE_HANDLE_COUNT =
        1 + // service declaration
        NOTIFY_CHARACTERISTIC_COUNT * HANDLES_PER_NOTIFY_CHARACTERISTIC +
        WRITE_ONLY_CHARACTERISTIC_COUNT * HANDLES_PER_WRITE_ONLY_CHARACTERISTIC +
        SERVICE_HANDLE_HEADROOM;
    constexpr uint16_t INVALID_GATT_HANDLE = 0xFFFF;

    bool verifyCharacteristicRegistered(const char* name, BLECharacteristic* characteristic)
    {
        if (characteristic == nullptr || characteristic->getHandle() == INVALID_GATT_HANDLE) {
            Serial.printf(
                "ERROR: BLE characteristic '%s' failed to register "
                "(GATT handle table exhausted?).\n",
                name
            );
            return false;
        }
        return true;
    }

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

    void writeUint32LE(uint8_t* destination, uint32_t value)
    {
        writeInt32LE(destination, static_cast<int32_t>(value));
    }

    uint32_t readUint32LE(const uint8_t* source)
    {
        return static_cast<uint32_t>(source[0]) |
               (static_cast<uint32_t>(source[1]) << 8) |
               (static_cast<uint32_t>(source[2]) << 16) |
               (static_cast<uint32_t>(source[3]) << 24);
    }

    int32_t readInt32LE(const uint8_t* source)
    {
        return static_cast<int32_t>(readUint32LE(source));
    }

    uint8_t resetReasonCode(const char* resetReason)
    {
        if (resetReason == nullptr) return 0;
        if (strcmp(resetReason, "power-on") == 0) return 1;
        if (strcmp(resetReason, "software reset") == 0) return 2;
        if (strcmp(resetReason, "external reset") == 0) return 3;
        if (strcmp(resetReason, "brownout") == 0) return 4;
        if (strcmp(resetReason, "panic") == 0) return 5;
        return 255;
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
    : callbacks_(*this), controlCallbacks_(*this), firmwareTransferCallbacks_(*this)
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

    // This library never auto-creates a CCCD for NOTIFY characteristics
    // (only descriptors added here via addDescriptor() get registered), so
    // without this a central can't find anything to write to enable
    // notifications.
    characteristic->addDescriptor(new BLE2902());

    BLEDescriptor* userDescription = new BLEDescriptor("2901");
    userDescription->setValue(
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(description)),
        strlen(description)
    );
    characteristic->addDescriptor(userDescription);

    return characteristic;
}

BLECharacteristic* BleTelemetryService::createControlCharacteristic(BLEService* service)
{
    BLECharacteristic* characteristic = service->createCharacteristic(
        CONTROL_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );

    BLEDescriptor* userDescription = new BLEDescriptor("2901");
    userDescription->setValue("Dashboard Control v1");
    characteristic->addDescriptor(userDescription);
    return characteristic;
}

BLECharacteristic* BleTelemetryService::createControlStatusCharacteristic(BLEService* service)
{
    return createCharacteristic(service, CONTROL_STATUS_UUID, "Control Result v1");
}

BLECharacteristic* BleTelemetryService::createFirmwareTransferCharacteristic(BLEService* service)
{
    BLECharacteristic* characteristic = service->createCharacteristic(
        FIRMWARE_TRANSFER_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );

    BLEDescriptor* userDescription = new BLEDescriptor("2901");
    userDescription->setValue("Firmware Transfer v1 (sequential write)");
    characteristic->addDescriptor(userDescription);
    return characteristic;
}

void BleTelemetryService::begin(FirmwareUpdateService& firmwareUpdate)
{
    firmwareUpdate_ = &firmwareUpdate;
    BLEDevice::init(Config::BLE_DEVICE_NAME);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(&callbacks_);

    // The default single-argument overload allocates only 15 GATT attribute
    // handles, far fewer than SERVICE_HANDLE_COUNT needs. Anything created
    // once a too-small table fills up never actually registers in the live
    // GATT database, even though the local BLECharacteristic object still
    // exists and looks fine to firmware.
    BLEService* service = server_->createService(BLEUUID(SERVICE_UUID), SERVICE_HANDLE_COUNT, 0);

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
    dashboardCharacteristic_ = createCharacteristic(
        service, DASHBOARD_UUID, "Dashboard Data v1 (20-byte LE)");
    controlCharacteristic_ = createControlCharacteristic(service);
    controlCharacteristic_->setCallbacks(&controlCallbacks_);
    controlStatusCharacteristic_ = createControlStatusCharacteristic(service);
    deviceInfoCharacteristic_ = createCharacteristic(
        service, DEVICE_INFO_UUID, "Device Information");
    firmwareTransferCharacteristic_ = createFirmwareTransferCharacteristic(service);
    firmwareTransferCharacteristic_->setCallbacks(&firmwareTransferCallbacks_);
    firmwareUpdateStatusCharacteristic_ = createCharacteristic(
        service, FIRMWARE_UPDATE_STATUS_UUID, "Firmware Update Status v1 (12-byte LE)");

    voltageCharacteristic_->setValue("0.000");
    currentCharacteristic_->setValue("0.000000");
    powerCharacteristic_->setValue("0.000000");
    temperatureCharacteristic_->setValue("0.0");
    ampHourCharacteristic_->setValue("0.000000");
    wattHourCharacteristic_->setValue("0.000000");
    statusCharacteristic_->setValue("BOOT");
    telemetryCharacteristic_->setValue("V=0;I=0;P=0;T=0;Ah=0;Wh=0");
    uint8_t initialBinaryTelemetry[BINARY_TELEMETRY_SIZE] = {
        static_cast<uint8_t>(BINARY_TELEMETRY_VERSION << 4)
    };
    binaryTelemetryCharacteristic_->setValue(initialBinaryTelemetry, BINARY_TELEMETRY_SIZE);
    uint8_t initialDashboard[BINARY_TELEMETRY_SIZE] = {DASHBOARD_EXTREMA};
    dashboardCharacteristic_->setValue(initialDashboard, BINARY_TELEMETRY_SIZE);
    uint8_t initialFirmwareStatus[FIRMWARE_UPDATE_STATUS_SIZE] = {};
    firmwareUpdateStatusCharacteristic_->setValue(
        initialFirmwareStatus, sizeof(initialFirmwareStatus));
    uint8_t initialControlStatus[CONTROL_STATUS_SIZE] = {1};
    controlStatusCharacteristic_->setValue(initialControlStatus, sizeof(initialControlStatus));
    // ESP.getEfuseMac() is the factory-programmed base MAC burned into the
    // chip's eFuse at manufacture; unlike WiFi.macAddress() it never depends
    // on which radio interface (AP/STA) queried it, and it survives NVS
    // erases. That makes it a stable per-chip identity independent of a
    // phone's own BLE address for the peripheral, which iOS in particular
    // exposes as a privacy-scoped identifier that can change over time.
    char idHex[16];
    snprintf(idHex, sizeof(idHex), "%012llX",
        static_cast<unsigned long long>(ESP.getEfuseMac()));
    char deviceInfo[128];
    snprintf(
        deviceInfo,
        sizeof(deviceInfo),
        "FW=%s;HW=%s;ID=%s;BLE=telemetry1,dashboard1,ota1,control1,wifi1,soc1,protection1,energyp1",
        Config::FIRMWARE_VERSION,
        Config::HARDWARE_REVISION,
        idHex
    );
    deviceInfoCharacteristic_->setValue(deviceInfo);

    service->start();

    // & (not &&) so every characteristic is checked and logged even if an
    // earlier one already failed, rather than short-circuiting.
    const bool allCharacteristicsRegistered =
        verifyCharacteristicRegistered("voltage", voltageCharacteristic_) &
        verifyCharacteristicRegistered("current", currentCharacteristic_) &
        verifyCharacteristicRegistered("power", powerCharacteristic_) &
        verifyCharacteristicRegistered("temperature", temperatureCharacteristic_) &
        verifyCharacteristicRegistered("ampHour", ampHourCharacteristic_) &
        verifyCharacteristicRegistered("wattHour", wattHourCharacteristic_) &
        verifyCharacteristicRegistered("status", statusCharacteristic_) &
        verifyCharacteristicRegistered("telemetry", telemetryCharacteristic_) &
        verifyCharacteristicRegistered("binaryTelemetry", binaryTelemetryCharacteristic_) &
        verifyCharacteristicRegistered("dashboard", dashboardCharacteristic_) &
        verifyCharacteristicRegistered("control", controlCharacteristic_) &
        verifyCharacteristicRegistered("controlStatus", controlStatusCharacteristic_) &
        verifyCharacteristicRegistered("deviceInfo", deviceInfoCharacteristic_) &
        verifyCharacteristicRegistered("firmwareTransfer", firmwareTransferCharacteristic_) &
        verifyCharacteristicRegistered("firmwareUpdateStatus", firmwareUpdateStatusCharacteristic_);
    if (!allCharacteristicsRegistered) {
        Serial.println(
            "ERROR: one or more BLE characteristics failed to register; "
            "increase SERVICE_HANDLE_COUNT in BleTelemetryService.cpp."
        );
    }

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
    // Arduino-ESP32 3.2 exposes only a mutable-pointer overload even though
    // setValue copies the bytes synchronously.
    characteristic->setValue(const_cast<uint8_t*>(value), length);
    if (notify) {
        characteristic->notify();
    }
}

void BleTelemetryService::publish(
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
    const EnergyPersistenceConfig& energyPersistence)
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
        static_cast<unsigned long>(sensor.failedSamples()),
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
            static_cast<unsigned long>(sensor.failedSamples())
        );
    } else {
        snprintf(
            buffer,
            sizeof(buffer),
            "SENSOR_ERROR;F=%lu",
            static_cast<unsigned long>(sensor.failedSamples())
        );
    }
    updateCharacteristic(telemetryCharacteristic_, buffer, false);

    uint8_t binaryTelemetry[BINARY_TELEMETRY_SIZE] = {};
    encodeBinaryTelemetry(binaryTelemetry, telemetry, energy);
    updateBinaryCharacteristic(binaryTelemetryCharacteristic_, binaryTelemetry,
                               sizeof(binaryTelemetry), notify);
    publishDashboardPackets(store, energy, sensor, calibration, alarms, alarmState, calibrationStored,
                            displayOn, accessPointReady, resetReason, wifiClients,
                            stationConfigured, stationConnected, mdnsReady, stationIp,
                            batteryProfile, stateOfCharge, loadProtection, loadProtectionMonitor,
                            energyPersistence, notify);

    publishFirmwareUpdateStatus(notify);
}

void BleTelemetryService::publishFirmwareUpdateStatus(bool notify)
{
    // This compact status is available even before MTU negotiation. The app
    // uses it to distinguish a transport acknowledgement from a verified OTA
    // image; telemetry continues normally while the transfer is in progress.
    uint8_t firmwareStatus[FIRMWARE_UPDATE_STATUS_SIZE] = {};
    if (firmwareUpdate_ != nullptr) {
        firmwareStatus[0] = 1; // Firmware Update Status protocol version.
        firmwareStatus[1] = static_cast<uint8_t>(firmwareUpdate_->state());
        writeUint32LE(firmwareStatus + 2, firmwareUpdate_->receivedBytes());
        writeUint32LE(firmwareStatus + 6, firmwareUpdate_->expectedBytes());
        firmwareStatus[10] = static_cast<uint8_t>(firmwareUpdate_->error());
    }
    updateBinaryCharacteristic(firmwareUpdateStatusCharacteristic_, firmwareStatus,
                               sizeof(firmwareStatus), notify);
}

void BleTelemetryService::publishControlStatus(bool notify)
{
    uint8_t status[CONTROL_STATUS_SIZE] = {1};
    status[1] = controlStatusCommand_.load();
    writeUint16LE(status + 2, controlStatusRequestId_.load());
    status[4] = controlStatusResult_.load();
    updateBinaryCharacteristic(controlStatusCharacteristic_, status, sizeof(status), notify);
}

void BleTelemetryService::reportControlResult(
    uint8_t command,
    uint16_t requestId,
    ControlResult result)
{
    controlStatusCommand_.store(command);
    controlStatusRequestId_.store(requestId);
    controlStatusResult_.store(static_cast<uint8_t>(result));
    controlStatusDirty_.store(true);
}

void BleTelemetryService::publishDashboardPackets(
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
    const EnergyPersistenceConfig& energyPersistence,
    bool notify)
{
    // Cycle one compact data page per scheduled BLE update. This keeps the
    // link responsive on the ESP32-C3 while a newly connected app has a
    // complete dashboard within roughly nine seconds.
    uint8_t packet[BINARY_TELEMETRY_SIZE] = {};
    const Telemetry& telemetry = store.current();
    const Ina228ConfigurationStatus& config = sensor.configuration();
    const uint8_t page = dashboardPacketIndex_++ % 9;

    switch (page) {
    case 0: {
        packet[0] = DASHBOARD_EXTREMA;
        const MetricStats& voltage = store.voltageStats();
        const MetricStats& current = store.currentStats();
        const MetricStats& power = store.powerStats();
        const MetricStats& temperature = store.temperatureStats();
        uint8_t valid = 0;
        if (voltage.initialized) valid |= FLAG_VOLTAGE_VALID;
        if (current.initialized) valid |= FLAG_CURRENT_VALID;
        if (power.initialized) valid |= FLAG_POWER_VALID;
        if (temperature.initialized) valid |= FLAG_TEMPERATURE_VALID;
        writeUint16LE(packet + 1, static_cast<uint16_t>(roundAndClamp(
            voltage.minimum, 1000.0f, 0, std::numeric_limits<uint16_t>::max())));
        writeUint16LE(packet + 3, static_cast<uint16_t>(roundAndClamp(
            voltage.maximum, 1000.0f, 0, std::numeric_limits<uint16_t>::max())));
        writeInt24LE(packet + 5, roundAndClamp(current.minimum, 1000.0f, -8388608, 8388607));
        writeInt24LE(packet + 8, roundAndClamp(current.maximum, 1000.0f, -8388608, 8388607));
        writeInt24LE(packet + 11, roundAndClamp(power.minimum, 1000.0f, -8388608, 8388607));
        writeInt24LE(packet + 14, roundAndClamp(power.maximum, 1000.0f, -8388608, 8388607));
        packet[17] = static_cast<uint8_t>(roundAndClamp(temperature.minimum, 1.0f, -128, 127));
        packet[18] = static_cast<uint8_t>(roundAndClamp(temperature.maximum, 1.0f, -128, 127));
        packet[19] = valid;
        break;
    }
    case 1:
        packet[0] = DASHBOARD_ENERGY;
        writeInt32LE(packet + 1, roundAndClamp(energy.dischargedAh, 1000.0f,
                                                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 5, roundAndClamp(energy.chargedAh, 1000.0f,
                                                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 9, roundAndClamp(energy.dischargedWh, 1000.0f,
                                                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 13, roundAndClamp(energy.chargedWh, 1000.0f,
                                                 std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        packet[17] = energyPersistence.enabled ? 1 : 0;
        break;
    case 2: {
        packet[0] = DASHBOARD_STATE;
        uint8_t flags = 0;
        if (telemetry.sensorOK()) flags |= 1 << 0;
        if (displayOn) flags |= 1 << 1;
        if (config.configured) flags |= 1 << 2;
        if (config.readbackValid) flags |= 1 << 3;
        if (config.wideShuntRange) flags |= 1 << 4;
        if (accessPointReady) flags |= 1 << 5;
        if (calibrationStored) flags |= 1 << 6;
        packet[1] = flags;
        writeUint16LE(packet + 2, static_cast<uint16_t>(telemetry.sequence));
        writeUint32LE(packet + 4, millis() / 1000UL);
        writeUint32LE(packet + 8, sensor.successfulSamples());
        writeUint32LE(packet + 12, sensor.failedSamples());
        packet[16] = wifiClients;
        packet[17] = resetReasonCode(resetReason);
        writeUint16LE(packet + 18, config.conversionTimeUs);
        break;
    }
    case 3:
        packet[0] = DASHBOARD_CALIBRATION;
        packet[1] = calibrationStored ? 1 : 0;
        writeUint32LE(packet + 2, static_cast<uint32_t>(roundAndClamp(
            calibration.shuntResistanceOhms, 1.0e6f, 0, std::numeric_limits<int32_t>::max())));
        writeInt32LE(packet + 6, roundAndClamp(
            calibration.shuntOffsetVolts, 1.0e9f,
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 10, roundAndClamp(
            calibration.currentGain, 1.0e6f,
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 14, roundAndClamp(
            telemetry.shuntVoltage, 1.0e9f,
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        packet[18] = telemetry.shuntVoltageValid() ? 1 : 0;
        break;
    case 4: {
        packet[0] = DASHBOARD_SHUNT;
        const MetricStats& shunt = store.shuntStats();
        const MetricStats& temperature = store.temperatureStats();
        writeInt32LE(packet + 1, roundAndClamp(shunt.minimum, 1.0e9f,
                                                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeInt32LE(packet + 5, roundAndClamp(shunt.maximum, 1.0e9f,
                                                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        writeUint16LE(packet + 9, config.configRegister);
        writeUint16LE(packet + 11, config.adcConfigRegister);
        writeUint16LE(packet + 13, config.averages);
        writeUint16LE(packet + 15, config.conversionTimeUs);
        packet[17] = static_cast<uint8_t>(roundAndClamp(temperature.minimum, 1.0f, -128, 127));
        packet[18] = static_cast<uint8_t>(roundAndClamp(temperature.maximum, 1.0f, -128, 127));
        packet[19] = (shunt.initialized ? 1 : 0) | (temperature.initialized ? 2 : 0);
        break;
    }
    case 5:
        packet[0] = DASHBOARD_ALARMS;
        packet[1] = (alarms.lowVoltageEnabled ? 1 : 0) |
                    (alarms.highVoltageEnabled ? 2 : 0) |
                    (alarms.currentEnabled ? 4 : 0) |
                    (alarms.temperatureEnabled ? 8 : 0) |
                    (alarms.sensorHealthEnabled ? 16 : 0);
        packet[2] = alarmState.activeFlags;
        writeUint16LE(packet + 3, static_cast<uint16_t>(roundAndClamp(alarms.lowVoltage, 1000.0f, 0, 65535)));
        writeUint16LE(packet + 5, static_cast<uint16_t>(roundAndClamp(alarms.highVoltage, 1000.0f, 0, 65535)));
        writeInt24LE(packet + 7, roundAndClamp(alarms.maxAbsoluteCurrent, 1000.0f, 0, 8388607));
        writeInt32LE(packet + 10, roundAndClamp(alarms.maxTemperature, 10.0f, -1280, 1270));
        break;
    case 6:
        packet[0] = DASHBOARD_WIFI;
        packet[1] = (stationConfigured ? 1 : 0) |
                    (stationConnected ? 2 : 0) |
                    (mdnsReady ? 4 : 0);
        packet[2] = stationIp[0];
        packet[3] = stationIp[1];
        packet[4] = stationIp[2];
        packet[5] = stationIp[3];
        break;
    case 7: {
        packet[0] = DASHBOARD_PROTECTION;
        packet[1] = (loadProtection.enabled ? 1 : 0) |
                    (loadProtectionMonitor.relayEngaged() ? 2 : 0) |
                    (loadProtectionMonitor.tripped() ? 4 : 0);
        packet[2] = loadProtectionMonitor.tripFlags();
        packet[3] = LoadProtectionMonitor::evaluateBreach(
            loadProtection, telemetry, stateOfCharge, batteryProfile);
        writeUint16LE(packet + 4, static_cast<uint16_t>(roundAndClamp(
            loadProtection.lowVoltageThreshold, 1000.0f, 0, 65535)));
        writeUint16LE(packet + 6, static_cast<uint16_t>(roundAndClamp(
            loadProtection.lowSocPercentThreshold, 10.0f, 0, 1000)));
        break;
    }
    default: {
        packet[0] = DASHBOARD_SOC;
        packet[1] = (stateOfCharge.known() ? 1 : 0) | (stateOfCharge.hasTimeToEmpty() ? 2 : 0);
        writeUint16LE(packet + 2, static_cast<uint16_t>(roundAndClamp(
            stateOfCharge.percent(batteryProfile), 10.0f, 0, 1000)));
        writeUint32LE(packet + 4, stateOfCharge.hasTimeToEmpty()
            ? stateOfCharge.timeToEmptySeconds() : 0xFFFFFFFFUL);
        writeUint32LE(packet + 8, static_cast<uint32_t>(roundAndClamp(
            batteryProfile.capacityAh, 1000.0f, 0, std::numeric_limits<int32_t>::max())));
        writeUint16LE(packet + 12, static_cast<uint16_t>(roundAndClamp(
            batteryProfile.chargedVoltage, 1000.0f, 0, 65535)));
        writeUint16LE(packet + 14, static_cast<uint16_t>(roundAndClamp(
            stateOfCharge.deepestDischargePercent(), 10.0f, 0, 1000)));
        writeUint16LE(packet + 16, static_cast<uint16_t>(
            stateOfCharge.fullChargeCycles() > 65535 ? 65535 : stateOfCharge.fullChargeCycles()));
        writeUint16LE(packet + 18, static_cast<uint16_t>(roundAndClamp(
            stateOfCharge.averageDischargeDepthPercent(), 10.0f, 0, 1000)));
        break;
    }
    }

    updateBinaryCharacteristic(dashboardCharacteristic_, packet, sizeof(packet), notify);
}

void BleTelemetryService::ControlCallbacks::onWrite(BLECharacteristic* characteristic)
{
    const auto value = characteristic->getValue();
    if (value.length() == 0) {
        return;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(value.c_str());
    const uint16_t requestId = value.length() >= 3
        ? static_cast<uint16_t>(data[value.length() - 2]) |
              (static_cast<uint16_t>(data[value.length() - 1]) << 8)
        : 0;
    PendingCommand command = PendingCommand::None;
    switch (data[0]) {
    case CONTROL_RESET_EXTREMA: command = PendingCommand::ResetExtrema; break;
    case CONTROL_RESET_SESSION: command = PendingCommand::ResetSession; break;
    case CONTROL_TOGGLE_DISPLAY: command = PendingCommand::ToggleDisplay; break;
    case CONTROL_RESET_CALIBRATION: command = PendingCommand::ResetCalibration; break;
    case CONTROL_SAVE_CALIBRATION:
        if (value.length() < 13) {
            return;
        }
        command = PendingCommand::SaveCalibration;
        break;
    case CONTROL_SAVE_ALARMS:
        if (value.length() < 13) return;
        command = PendingCommand::SaveAlarms;
        break;
    case CONTROL_CLEAR_WIFI: command = PendingCommand::ClearWifi; break;
    case CONTROL_SYNC_BATTERY_FULL: command = PendingCommand::SyncBatteryFull; break;
    case CONTROL_RESET_BATTERY_HISTORY: command = PendingCommand::ResetBatteryHistory; break;
    case CONTROL_SAVE_BATTERY_PROFILE:
        // command(1) + capacity milli-Ah u32(4) + charged voltage mV u16(2) + requestId(2)
        if (value.length() < 9) return;
        command = PendingCommand::SaveBatteryProfile;
        break;
    case CONTROL_SAVE_LOAD_PROTECTION:
        // command(1) + enabledFlag(1) + lowVoltage mV u16(2) + lowSocPercent
        // deci-percent u16(2) + requestId(2)
        if (value.length() < 8) return;
        command = PendingCommand::SaveLoadProtection;
        break;
    case CONTROL_RECONNECT_LOAD: command = PendingCommand::ReconnectLoad; break;
    case CONTROL_TEST_CONNECT_LOAD: command = PendingCommand::TestConnectLoad; break;
    case CONTROL_TEST_DISCONNECT_LOAD: command = PendingCommand::TestDisconnectLoad; break;
    case CONTROL_SAVE_ENERGY_PERSISTENCE:
        // command(1) + enabledFlag(1) + requestId(2)
        if (value.length() < 4) return;
        command = PendingCommand::SaveEnergyPersistence;
        break;
    case CONTROL_SAVE_WIFI: {
        // command(1) + ssidLength(1) + ssid + passwordLength(1) + password + requestId(2)
        if (value.length() < 6) return;
        const uint8_t ssidLength = data[1];
        if (ssidLength == 0 || ssidLength >= sizeof(WifiStationSettings::ssid)) return;
        if (value.length() < static_cast<size_t>(3 + ssidLength)) return;
        const uint8_t passwordLength = data[2 + ssidLength];
        if (passwordLength >= sizeof(WifiStationSettings::password)) return;
        if (value.length() != static_cast<size_t>(3 + ssidLength + passwordLength + 2)) return;
        command = PendingCommand::SaveWifi;
        break;
    }
    default:
        return;
    }

    uint8_t expected = static_cast<uint8_t>(PendingCommand::None);
    if (owner_.pendingCommand_.compare_exchange_strong(
            expected, static_cast<uint8_t>(PendingCommand::Writing))) {
        owner_.pendingRequestId_.store(requestId);
        if (command == PendingCommand::SaveCalibration) {
            owner_.pendingResistanceMicroOhms_.store(static_cast<int32_t>(readUint32LE(data + 1)));
            owner_.pendingOffsetNanoVolts_.store(readInt32LE(data + 5));
            owner_.pendingGainPpm_.store(readInt32LE(data + 9));
        } else if (command == PendingCommand::SaveAlarms) {
            owner_.pendingAlarmFlags_.store(data[1]);
            owner_.pendingLowVoltageMv_.store(data[2] | (data[3] << 8));
            owner_.pendingHighVoltageMv_.store(data[4] | (data[5] << 8));
            owner_.pendingCurrentMa_.store(
                static_cast<int32_t>(data[6]) | (static_cast<int32_t>(data[7]) << 8) | (static_cast<int32_t>(data[8]) << 16));
            owner_.pendingTemperatureDeciC_.store(readInt32LE(data + 9));
        } else if (command == PendingCommand::SaveWifi) {
            const uint8_t ssidLength = data[1];
            const uint8_t passwordLength = data[2 + ssidLength];
            memcpy(owner_.pendingWifiSettings_.ssid, data + 2, ssidLength);
            owner_.pendingWifiSettings_.ssid[ssidLength] = '\0';
            memcpy(owner_.pendingWifiSettings_.password, data + 3 + ssidLength, passwordLength);
            owner_.pendingWifiSettings_.password[passwordLength] = '\0';
        } else if (command == PendingCommand::SaveBatteryProfile) {
            owner_.pendingBatteryProfile_.capacityAh =
                static_cast<float>(readUint32LE(data + 1)) * 1.0e-3f;
            owner_.pendingBatteryProfile_.chargedVoltage =
                static_cast<float>(data[5] | (data[6] << 8)) * 1.0e-3f;
        } else if (command == PendingCommand::SaveLoadProtection) {
            owner_.pendingProtectionEnabled_.store(data[1]);
            owner_.pendingProtectionLowVoltageMv_.store(data[2] | (data[3] << 8));
            owner_.pendingProtectionLowSocDeciPercent_.store(data[4] | (data[5] << 8));
        } else if (command == PendingCommand::SaveEnergyPersistence) {
            owner_.pendingEnergyPersistenceEnabled_.store(data[1]);
        }
        owner_.pendingCommand_.store(static_cast<uint8_t>(command));
    } else {
        owner_.reportControlResult(
            data[0], requestId, ControlResult::Rejected);
    }
}

void BleTelemetryService::FirmwareTransferCallbacks::onWrite(BLECharacteristic* characteristic)
{
    if (owner_.firmwareUpdate_ == nullptr) {
        return;
    }

    const auto value = characteristic->getValue();
    if (value.length() == 0) {
        return;
    }

    owner_.firmwareUpdate_->handleFrame(
        reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
    owner_.publishFirmwareUpdateStatus(owner_.connected());
}

bool BleTelemetryService::consumeCommand(PendingCommand command, uint16_t& requestId)
{
    uint8_t expected = static_cast<uint8_t>(command);
    if (!pendingCommand_.compare_exchange_strong(
            expected, static_cast<uint8_t>(PendingCommand::None))) {
        return false;
    }
    requestId = pendingRequestId_.load();
    return true;
}

bool BleTelemetryService::consumeResetExtremaRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ResetExtrema, requestId);
}

bool BleTelemetryService::consumeSessionResetRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ResetSession, requestId);
}

bool BleTelemetryService::consumeDisplayToggleRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ToggleDisplay, requestId);
}

bool BleTelemetryService::consumeCalibrationSaveRequested(
    CurrentCalibration& calibration,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveCalibration, requestId)) {
        return false;
    }

    calibration.shuntResistanceOhms = static_cast<float>(pendingResistanceMicroOhms_.load()) * 1.0e-6f;
    calibration.shuntOffsetVolts = static_cast<float>(pendingOffsetNanoVolts_.load()) * 1.0e-9f;
    calibration.currentGain = static_cast<float>(pendingGainPpm_.load()) * 1.0e-6f;
    return true;
}

bool BleTelemetryService::consumeCalibrationResetRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ResetCalibration, requestId);
}

bool BleTelemetryService::consumeAlarmSaveRequested(
    DeviceAlarmSettings& settings,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveAlarms, requestId)) return false;
    const uint8_t flags = pendingAlarmFlags_.load();
    settings.lowVoltageEnabled = (flags & 1) != 0;
    settings.highVoltageEnabled = (flags & 2) != 0;
    settings.currentEnabled = (flags & 4) != 0;
    settings.temperatureEnabled = (flags & 8) != 0;
    settings.sensorHealthEnabled = (flags & 16) != 0;
    settings.lowVoltage = pendingLowVoltageMv_.load() / 1000.0f;
    settings.highVoltage = pendingHighVoltageMv_.load() / 1000.0f;
    settings.maxAbsoluteCurrent = pendingCurrentMa_.load() / 1000.0f;
    settings.maxTemperature = pendingTemperatureDeciC_.load() / 10.0f;
    return true;
}

bool BleTelemetryService::consumeWifiSaveRequested(
    WifiStationSettings& settings,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveWifi, requestId)) return false;
    settings = pendingWifiSettings_;
    return true;
}

bool BleTelemetryService::consumeWifiClearRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ClearWifi, requestId);
}

bool BleTelemetryService::consumeBatteryProfileSaveRequested(
    BatteryProfileSettings& settings,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveBatteryProfile, requestId)) return false;
    settings = pendingBatteryProfile_;
    return true;
}

bool BleTelemetryService::consumeBatterySyncRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::SyncBatteryFull, requestId);
}

bool BleTelemetryService::consumeBatteryHistoryResetRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ResetBatteryHistory, requestId);
}

bool BleTelemetryService::consumeLoadProtectionSaveRequested(
    LoadProtectionConfig& settings,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveLoadProtection, requestId)) return false;
    settings.enabled = pendingProtectionEnabled_.load() != 0;
    settings.lowVoltageThreshold = pendingProtectionLowVoltageMv_.load() / 1000.0f;
    settings.lowSocPercentThreshold = pendingProtectionLowSocDeciPercent_.load() / 10.0f;
    return true;
}

bool BleTelemetryService::consumeLoadProtectionReconnectRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::ReconnectLoad, requestId);
}

bool BleTelemetryService::consumeLoadProtectionTestConnectRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::TestConnectLoad, requestId);
}

bool BleTelemetryService::consumeLoadProtectionTestDisconnectRequested(uint16_t& requestId)
{
    return consumeCommand(PendingCommand::TestDisconnectLoad, requestId);
}

bool BleTelemetryService::consumeEnergyPersistenceSaveRequested(
    EnergyPersistenceConfig& settings,
    uint16_t& requestId)
{
    if (!consumeCommand(PendingCommand::SaveEnergyPersistence, requestId)) return false;
    settings.enabled = pendingEnergyPersistenceEnabled_.load() != 0;
    return true;
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

    if (controlStatusDirty_.exchange(false) && connected_.load()) {
        publishControlStatus(true);
    }
}
