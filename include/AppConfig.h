#pragma once

#include <Arduino.h>

namespace Config
{
    // I2C: XIAO ESP32-C3 D4/D5
    constexpr uint8_t SDA_PIN = 6;
    constexpr uint8_t SCL_PIN = 7;
    constexpr uint32_t I2C_CLOCK_HZ = 100000;

    constexpr uint8_t INA228_ADDRESS = 0x40;
    constexpr uint8_t OLED_ADDRESS = 0x3C;

    // Prototype INA228 breakout uses R015 = 15 mOhm.
    // Change to 0.001f after moving to the original Watt's Up 1 mOhm shunt.
    constexpr float SHUNT_RESISTANCE_OHMS = 0.015f;

    // Pushbuttons are active-low and use the ESP32 internal pull-ups.
    // Wiring: GPIO -> momentary pushbutton -> GND.
    // XIAO D1 = GPIO3, D2 = GPIO4.
    constexpr uint8_t RESET_SESSION_BUTTON_PIN = 3;
    constexpr uint8_t DISPLAY_TOGGLE_BUTTON_PIN = 4;
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
    constexpr uint32_t RESET_RESTART_LONG_PRESS_MS = 1000;
    constexpr uint32_t DISPLAY_LONG_PRESS_MS = 1000;
    constexpr uint32_t SPLASH_SCREEN_DURATION_MS = 3000;

    constexpr uint32_t MEASUREMENT_INTERVAL_MS = 500;
    // A delayed sample is a measurement gap, not energy that may be inferred.
    constexpr uint32_t MAX_ENERGY_INTEGRATION_GAP_MS = MEASUREMENT_INTERVAL_MS * 3;
    constexpr uint32_t DISPLAY_INTERVAL_MS = 500;
    constexpr uint32_t BLE_INTERVAL_MS = 1000;
    // Diagnostics must remain harmless when no USB serial client is attached.
    constexpr uint32_t SERIAL_INTERVAL_MS = 5000;

    constexpr char BLE_DEVICE_NAME[] = "BatteryMonitor";

    constexpr char WIFI_AP_SSID[] = "BatteryMonitor";
    constexpr char WIFI_AP_PASSWORD[] = "Battery123";
}
