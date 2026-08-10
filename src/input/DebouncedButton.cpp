#include "input/DebouncedButton.h"

DebouncedButton::DebouncedButton(uint8_t pin, uint32_t debounceMs)
    : pin_(pin), debounceMs_(debounceMs)
{
}

void DebouncedButton::begin()
{
    pinMode(pin_, INPUT_PULLUP);
    rawState_ = digitalRead(pin_);
    stableState_ = rawState_;
    lastRawChangeMs_ = millis();
    pressedEvent_ = false;
}

void DebouncedButton::update(uint32_t nowMs)
{
    const int state = digitalRead(pin_);

    if (state != rawState_) {
        rawState_ = state;
        lastRawChangeMs_ = nowMs;
    }

    if (rawState_ != stableState_ &&
        (nowMs - lastRawChangeMs_) >= debounceMs_) {
        stableState_ = rawState_;

        if (stableState_ == LOW) {
            pressedEvent_ = true;
        }
    }
}

bool DebouncedButton::consumePressed()
{
    const bool event = pressedEvent_;
    pressedEvent_ = false;
    return event;
}
