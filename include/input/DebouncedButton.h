#pragma once

#include <Arduino.h>

class DebouncedButton
{
public:
    DebouncedButton(uint8_t pin, uint32_t debounceMs);

    void begin();
    void update(uint32_t nowMs);
    bool consumePressed();

private:
    uint8_t pin_;
    uint32_t debounceMs_;

    int rawState_ = HIGH;
    int stableState_ = HIGH;
    uint32_t lastRawChangeMs_ = 0;
    bool pressedEvent_ = false;
};
