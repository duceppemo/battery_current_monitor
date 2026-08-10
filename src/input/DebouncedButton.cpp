#include "input/DebouncedButton.h"

DebouncedButton::DebouncedButton(uint8_t pin, uint32_t debounceMs, uint32_t longPressMs)
    : pin_(pin), debounceMs_(debounceMs), longPressMs_(longPressMs)
{
}

void DebouncedButton::begin()
{
    pinMode(pin_, INPUT_PULLUP);
    rawState_ = digitalRead(pin_);
    stableState_ = rawState_;
    lastRawChangeMs_ = millis();
    pressedEvent_ = false;
    shortPressEvent_ = false;
    longPressEvent_ = false;
    longPressFired_ = false;
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
            pressedAtMs_ = nowMs;
            longPressFired_ = false;
        } else if (!longPressFired_) {
            shortPressEvent_ = true;
        }
    }

    if (stableState_ == LOW && !longPressFired_ && longPressMs_ != 0 &&
        (nowMs - pressedAtMs_) >= longPressMs_) {
        longPressEvent_ = true;
        longPressFired_ = true;
    }
}

bool DebouncedButton::consumeShortPress()
{
    const bool event = shortPressEvent_;
    shortPressEvent_ = false;
    return event;
}

bool DebouncedButton::consumeLongPress()
{
    const bool event = longPressEvent_;
    longPressEvent_ = false;
    return event;
}

bool DebouncedButton::consumePressed()
{
    const bool event = pressedEvent_;
    pressedEvent_ = false;
    return event;
}
