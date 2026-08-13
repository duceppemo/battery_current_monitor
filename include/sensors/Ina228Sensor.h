#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "measurement/CalibrationSettings.h"
#include "model/Telemetry.h"

struct Ina228ConfigurationStatus
{
    bool configured = false;
    bool readbackValid = false;
    bool wideShuntRange = false;
    uint16_t conversionTimeUs = 0;
    uint16_t averages = 0;
    uint16_t configRegister = 0;
    uint16_t adcConfigRegister = 0;
};

class Ina228Sensor
{
public:
    void begin(TwoWire& wire);
    bool identify();
    bool configure();
    void setCalibration(const CurrentCalibration& calibration);
    bool read(Telemetry& telemetry);

    const Ina228ConfigurationStatus& configuration() const { return configuration_; }

    // A sample is one polling pass across voltage, shunt voltage and die
    // temperature. This is the health measure exposed to users.
    uint32_t successfulSamples() const { return successfulSamples_; }
    uint32_t failedSamples() const { return failedSamples_; }

    // Register counters include both low-level reads and configuration writes.
    uint32_t successfulRegisterReads() const { return successfulRegisterReads_; }
    uint32_t failedRegisterReads() const { return failedRegisterReads_; }

private:
    static constexpr uint8_t REG_CONFIG = 0x00;
    static constexpr uint8_t REG_ADC_CONFIG = 0x01;
    static constexpr uint8_t REG_VSHUNT = 0x04;
    static constexpr uint8_t REG_VBUS = 0x05;
    static constexpr uint8_t REG_DIETEMP = 0x06;
    static constexpr uint8_t REG_MANFID = 0x3E;
    static constexpr uint8_t REG_DEVICEID = 0x3F;

    static constexpr float VSHUNT_LSB_VOLTS = 312.5e-9f;
    static constexpr float VBUS_LSB_VOLTS = 195.3125e-6f;
    static constexpr float TEMP_LSB_C = 7.8125e-3f;
    static constexpr uint8_t MAX_I2C_RETRIES = 3;

    bool readRegister(uint8_t reg, uint8_t* buffer, size_t length);
    bool write16(uint8_t reg, uint16_t value);
    bool read16(uint8_t reg, uint16_t& value);
    bool read24(uint8_t reg, uint32_t& value);
    bool readBusVoltage(float& volts);
    bool readShuntVoltage(float& volts);
    bool readTemperature(float& temperatureC);
    static int32_t signExtend20(uint32_t value);

    TwoWire* wire_ = nullptr;
    uint32_t successfulSamples_ = 0;
    uint32_t failedSamples_ = 0;
    uint32_t successfulRegisterReads_ = 0;
    uint32_t failedRegisterReads_ = 0;
    CurrentCalibration calibration_;
    Ina228ConfigurationStatus configuration_;
};
