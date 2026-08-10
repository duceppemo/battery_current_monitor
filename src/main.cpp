#include <Arduino.h>

#include "app/BatteryMonitorApp.h"

BatteryMonitorApp app;

void setup()
{
    app.begin();
}

void loop()
{
    app.update();
}
