#pragma once

#include <algorithm>
#include <cmath>

struct MetricStats
{
    bool initialized = false;
    float minimum = NAN;
    float maximum = NAN;

    void update(float value)
    {
        if (!std::isfinite(value)) {
            return;
        }

        if (!initialized) {
            minimum = value;
            maximum = value;
            initialized = true;
            return;
        }

        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    void reset()
    {
        initialized = false;
        minimum = NAN;
        maximum = NAN;
    }
};
