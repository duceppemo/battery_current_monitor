#include "sensors/Ina228Sensor.h"

#include "AppConfig.h"

namespace
{
    constexpr uint8_t MODE_CONTINUOUS_ALL = 0x0F;
    constexpr uint8_t CONVERSION_TIME_1052_US = 0x05;
    constexpr uint8_t AVERAGES_16 = 0x02;
    constexpr uint16_t EXPECTED_CONFIG = 0x0000;
    constexpr uint16_t EXPECTED_ADC_CONFIG =
        (static_cast<uint16_t>(MODE_CONTINUOUS_ALL) << 12) |
        (static_cast<uint16_t>(CONVERSION_TIME_1052_US) << 9) |
        (static_cast<uint16_t>(CONVERSION_TIME_1052_US) << 6) |
        (static_cast<uint16_t>(CONVERSION_TIME_1052_US) << 3) |
        AVERAGES_16;

    uint16_t conversionTimeUs(uint8_t setting)
    {
        constexpr uint16_t VALUES[] = {50, 84, 150, 280, 540, 1052, 2074, 4120};
        return setting < (sizeof(VALUES) / sizeof(VALUES[0])) ? VALUES[setting] : 0;
    }

    uint16_t averageCount(uint8_t setting)
    {
        constexpr uint16_t VALUES[] = {1, 4, 16, 64, 128, 256, 512, 1024};
        return setting < (sizeof(VALUES) / sizeof(VALUES[0])) ? VALUES[setting] : 0;
    }
}

void Ina228Sensor::begin(TwoWire& wire)
{
    wire_ = &wire;
}

void Ina228Sensor::setCalibration(const CurrentCalibration& calibration)
{
    calibration_ = calibration;
}

bool Ina228Sensor::readRegister(uint8_t reg, uint8_t* buffer, size_t length)
{
    if (wire_ == nullptr) {
        ++failedRegisterReads_;
        return false;
    }

    for (uint8_t attempt = 1; attempt <= MAX_I2C_RETRIES; ++attempt) {
        wire_->beginTransmission(Config::INA228_ADDRESS);
        wire_->write(reg);

        const uint8_t error = wire_->endTransmission(false);
        if (error != 0) {
            delay(2);
            continue;
        }

        const size_t received = wire_->requestFrom(
            Config::INA228_ADDRESS,
            static_cast<uint8_t>(length),
            static_cast<uint8_t>(true)
        );

        if (received != length) {
            while (wire_->available()) {
                wire_->read();
            }
            delay(2);
            continue;
        }

        for (size_t i = 0; i < length; ++i) {
            buffer[i] = static_cast<uint8_t>(wire_->read());
        }

        ++successfulRegisterReads_;
        return true;
    }

    ++failedRegisterReads_;
    return false;
}

bool Ina228Sensor::read16(uint8_t reg, uint16_t& value)
{
    uint8_t data[2];
    if (!readRegister(reg, data, sizeof(data))) {
        return false;
    }

    value = (static_cast<uint16_t>(data[0]) << 8) |
            static_cast<uint16_t>(data[1]);
    return true;
}

bool Ina228Sensor::write16(uint8_t reg, uint16_t value)
{
    if (wire_ == nullptr) {
        ++failedRegisterReads_;
        return false;
    }

    for (uint8_t attempt = 1; attempt <= MAX_I2C_RETRIES; ++attempt) {
        wire_->beginTransmission(Config::INA228_ADDRESS);
        wire_->write(reg);
        wire_->write(static_cast<uint8_t>(value >> 8));
        wire_->write(static_cast<uint8_t>(value));

        if (wire_->endTransmission(true) == 0) {
            ++successfulRegisterReads_;
            return true;
        }

        delay(2);
    }

    ++failedRegisterReads_;
    return false;
}

bool Ina228Sensor::read24(uint8_t reg, uint32_t& value)
{
    uint8_t data[3];
    if (!readRegister(reg, data, sizeof(data))) {
        return false;
    }

    value = (static_cast<uint32_t>(data[0]) << 16) |
            (static_cast<uint32_t>(data[1]) << 8) |
            static_cast<uint32_t>(data[2]);
    return true;
}

int32_t Ina228Sensor::signExtend20(uint32_t value)
{
    value &= 0xFFFFF;
    if ((value & 0x80000) != 0) {
        value |= 0xFFF00000;
    }
    return static_cast<int32_t>(value);
}

bool Ina228Sensor::readBusVoltage(float& volts)
{
    uint32_t raw = 0;
    if (!read24(REG_VBUS, raw)) {
        return false;
    }

    volts = static_cast<float>(raw >> 4) * VBUS_LSB_VOLTS;
    return true;
}

bool Ina228Sensor::readShuntVoltage(float& volts)
{
    uint32_t raw = 0;
    if (!read24(REG_VSHUNT, raw)) {
        return false;
    }

    const int32_t value = signExtend20(raw >> 4);
    volts = static_cast<float>(value) * VSHUNT_LSB_VOLTS;
    return true;
}

bool Ina228Sensor::readTemperature(float& temperatureC)
{
    uint16_t raw = 0;
    if (!read16(REG_DIETEMP, raw)) {
        return false;
    }

    temperatureC = static_cast<int16_t>(raw) * TEMP_LSB_C;
    return true;
}

bool Ina228Sensor::identify()
{
    uint16_t manufacturer = 0;
    uint16_t device = 0;

    const bool manufacturerOK = read16(REG_MANFID, manufacturer);
    const bool deviceOK = read16(REG_DEVICEID, device);

    Serial.println();
    Serial.println("INA228 identification");

    if (manufacturerOK) {
        Serial.printf("Manufacturer ID : 0x%04X\n", manufacturer);
    } else {
        Serial.println("Manufacturer ID : READ FAILED");
    }

    if (deviceOK) {
        Serial.printf("Device ID       : 0x%04X\n", device);
    } else {
        Serial.println("Device ID       : READ FAILED");
    }

    const bool valid = manufacturerOK &&
                       deviceOK &&
                       manufacturer == 0x5449 &&
                       (device >> 4) == 0x0228;

    Serial.println(valid ? "INA228 confirmed."
                         : "WARNING: INA228 identification failed.");
    return valid;
}

bool Ina228Sensor::configure()
{
    uint16_t configReadback = 0;
    uint16_t adcConfigReadback = 0;
    const bool writesSucceeded =
        write16(REG_CONFIG, EXPECTED_CONFIG) &&
        write16(REG_ADC_CONFIG, EXPECTED_ADC_CONFIG);
    const bool readbackValid = writesSucceeded &&
        read16(REG_CONFIG, configReadback) &&
        read16(REG_ADC_CONFIG, adcConfigReadback);

    configuration_ = Ina228ConfigurationStatus{};
    configuration_.configRegister = configReadback;
    configuration_.adcConfigRegister = adcConfigReadback;
    configuration_.readbackValid = readbackValid;
    if (readbackValid) {
        configuration_.wideShuntRange = (configReadback & (1U << 4)) == 0;
        configuration_.conversionTimeUs = conversionTimeUs(
            static_cast<uint8_t>((adcConfigReadback >> 9) & 0x07)
        );
        configuration_.averages = averageCount(
            static_cast<uint8_t>(adcConfigReadback & 0x07)
        );
    }
    configuration_.configured = readbackValid &&
        configReadback == EXPECTED_CONFIG &&
        adcConfigReadback == EXPECTED_ADC_CONFIG;

    return configuration_.configured;
}

bool Ina228Sensor::read(Telemetry& telemetry)
{
    // Never leave values from an earlier sample behind when a register read
    // fails. Each call produces a self-contained snapshot.
    telemetry = Telemetry{};

    telemetry.voltageOK = readBusVoltage(telemetry.voltage);
    telemetry.shuntOK = readShuntVoltage(telemetry.shuntVoltage);
    telemetry.temperatureOK = readTemperature(telemetry.temperature);

    if (telemetry.shuntOK && std::isfinite(telemetry.shuntVoltage) &&
        std::isfinite(calibration_.shuntResistanceOhms) &&
        calibration_.shuntResistanceOhms > 0.0f &&
        std::isfinite(calibration_.shuntOffsetVolts) &&
        std::isfinite(calibration_.currentGain)) {
        telemetry.current =
            (telemetry.shuntVoltage - calibration_.shuntOffsetVolts) *
            calibration_.currentGain / calibration_.shuntResistanceOhms;
    } else {
        telemetry.current = NAN;
    }

    if (telemetry.voltageValid() && telemetry.currentValid()) {
        telemetry.power = telemetry.voltage * telemetry.current;
    } else {
        telemetry.power = NAN;
    }

    if (telemetry.sensorOK()) {
        ++successfulSamples_;
    } else {
        ++failedSamples_;
    }

    return telemetry.sensorOK();
}
