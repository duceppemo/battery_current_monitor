#pragma once

#include <Arduino.h>

namespace Config
{
    // Keep the release version in one place. The preprocessor string literal is
    // also embedded in OTA images for the dashboard's selected-file check.
#define BATTERY_MONITOR_FIRMWARE_VERSION "0.5.1"
    constexpr char FIRMWARE_VERSION[] = BATTERY_MONITOR_FIRMWARE_VERSION;
    // Deliberately retained in the application image so the Web Dashboard can
    // identify a selected OTA .bin before it is written to the inactive slot.
    constexpr char FIRMWARE_IMAGE_MARKER[] = "BMFW:" BATTERY_MONITOR_FIRMWARE_VERSION;
    constexpr char HARDWARE_REVISION[] = "Rev A";

    // I2C: XIAO ESP32-C3 D4/D5
    constexpr uint8_t SDA_PIN = 6;
    constexpr uint8_t SCL_PIN = 7;
    constexpr uint32_t I2C_CLOCK_HZ = 100000;

    constexpr uint8_t INA228_ADDRESS = 0x40;
    constexpr uint8_t OLED_ADDRESS = 0x3C;

    // First-boot default for the prototype INA228 breakout's R015 = 15 mOhm
    // shunt. A validated NVS calibration profile overrides this at startup.
    // The planned 100 A / 50 mV Kelvin shunt is 0.0005f ohm.
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
    // Leave BLE time to deliver the verified OTA status before reset.
    constexpr uint32_t BLE_OTA_RESTART_GRACE_MS = 2000;
    // Diagnostics must remain harmless when no USB serial client is attached.
    constexpr uint32_t SERIAL_INTERVAL_MS = 5000;

    constexpr char BLE_DEVICE_NAME[] = "BatteryMonitor";

    constexpr char WIFI_AP_SSID[] = "BatteryMonitor";
    constexpr char WIFI_AP_PASSWORD[] = "Battery123";
}
