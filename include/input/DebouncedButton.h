#pragma once

#include <Arduino.h>

class DebouncedButton
{
public:
    DebouncedButton(uint8_t pin, uint32_t debounceMs, uint32_t longPressMs = 0);

    void begin();
    void update(uint32_t nowMs);
    bool consumeShortPress();
    bool consumeLongPress();

private:
    uint8_t pin_;
    uint32_t debounceMs_;
    uint32_t longPressMs_;

    int rawState_ = HIGH;
    int stableState_ = HIGH;
    uint32_t lastRawChangeMs_ = 0;
    uint32_t pressedAtMs_ = 0;
    bool shortPressEvent_ = false;
    bool longPressEvent_ = false;
    bool longPressFired_ = false;
};
