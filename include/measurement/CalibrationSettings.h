#pragma once

#include <Arduino.h>

struct CurrentCalibration
{
    float shuntResistanceOhms = 0.015f;
    float shuntOffsetVolts = 0.0f;
    float currentGain = 1.0f;

    CurrentCalibration() = default;
    CurrentCalibration(
        float shuntResistanceOhms,
        float shuntOffsetVolts,
        float currentGain)
        : shuntResistanceOhms(shuntResistanceOhms),
          shuntOffsetVolts(shuntOffsetVolts),
          currentGain(currentGain)
    {
    }
};

class CalibrationSettings
{
public:
    void begin();
    bool save(const CurrentCalibration& calibration);
    bool clear();

    const CurrentCalibration& current() const { return current_; }
    bool loadedFromStorage() const { return loadedFromStorage_; }

    static bool isValid(const CurrentCalibration& calibration);

private:
    CurrentCalibration current_;
    bool loadedFromStorage_ = false;
};
