#pragma once

// Placeholder for an optional DS18B20 attached to the physical shunt.
// Not wired into the application yet, so no OneWire dependency is required.
class Ds18b20Sensor
{
public:
    bool begin() { return false; }
};
