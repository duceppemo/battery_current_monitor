#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "model/Telemetry.h"

class Ina228Sensor
{
public:
    void begin(TwoWire& wire);
    bool identify();
    bool read(Telemetry& telemetry);

    uint32_t goodReads() const { return goodReads_; }
    uint32_t failedReads() const { return failedReads_; }

private:
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
    bool read16(uint8_t reg, uint16_t& value);
    bool read24(uint8_t reg, uint32_t& value);
    bool readBusVoltage(float& volts);
    bool readShuntVoltage(float& volts);
    bool readTemperature(float& temperatureC);
    static int32_t signExtend20(uint32_t value);

    TwoWire* wire_ = nullptr;
    uint32_t goodReads_ = 0;
    uint32_t failedReads_ = 0;
};
