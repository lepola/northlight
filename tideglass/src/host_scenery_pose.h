#pragma once
#include <cstdint>

// Borrowed by the host device on its render thread. The core owns the lifetime;
// a session or device reset retires every request. Sample one palette at a time.
constexpr unsigned kWaterPoseBones = 12;
constexpr unsigned kWaterPosePlacements = 16;
struct WaterPoseSample {
    uint32_t tick;
    float scales[kWaterPoseBones][3];
};
struct WaterPosePlacement {
    uint32_t triangles, vertices, bones;
    unsigned char firstVertex[48];
    float origin[3];
    uint32_t started, samples, processed;
    WaterPoseSample observed[4];
    uint32_t phase, epoch, lastVisible;
    enum Status : uint32_t { Waiting, Matched, Unavailable } status;
};
struct WaterSceneryPoseRequests {
    uint32_t count, waiting;
    WaterPosePlacement placements[kWaterPosePlacements];
};
