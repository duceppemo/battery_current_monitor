#pragma once

// Placeholder for future Ah / Wh accumulation.
// Planned responsibilities:
// - integrate signed current over elapsed time
// - integrate power over elapsed time
// - expose Ah and Wh session totals
// - reset session totals
class EnergyAccumulator
{
public:
    void reset() {}
};
