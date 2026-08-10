#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "telemetry/TelemetryStore.h"

class WebDashboard
{
public:
    void begin(TelemetryStore& store);
    void update();

    void setRuntimeStatus(
        bool bleConnected,
        bool displayOn,
        uint32_t goodReads,
        uint32_t i2cErrors
    );

    uint8_t clientCount() const;
    bool running() const { return running_; }

private:
    void handleRoot();
    void handleTelemetry();
    void handleResetExtrema();
    void handleNotFound();

    static void appendNullableFloat(
        String& json,
        bool valid,
        float value,
        uint8_t decimals
    );

    static void appendMetric(
        String& json,
        const char* name,
        bool valid,
        float value,
        const MetricStats& stats,
        uint8_t decimals
    );

    WebServer server_{80};
    TelemetryStore* store_ = nullptr;

    bool running_ = false;
    bool bleConnected_ = false;
    bool displayOn_ = true;
    uint32_t goodReads_ = 0;
    uint32_t i2cErrors_ = 0;
};
