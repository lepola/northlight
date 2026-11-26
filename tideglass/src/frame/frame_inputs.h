#pragma once
// What the renderer is given for one frame: the values every host supplies (WaterFrameValues) and the further facts
// captured from the client on the game thread. It travels with the frame, so drawing never reads live game state.
#include "frame_values.h"
#include "host_scenery_pose.h"
#include "water_environment.h"
#include "frame/reflection_object.h"
#include "client/native_liquid.h"

class GroundMap;
using Mover = WaterMover;
using WakePoint = WaterWakePoint;
using Boat = WaterBoat;
using Splash = WaterSplash;
constexpr int kMaxMovers = kWaterMaxMovers;
constexpr int kBoatTrailPoints = kWaterBoatTrailPoints;
constexpr int kMaxBoats = kWaterMaxBoats;
constexpr float kSplashRingSeconds = kWaterSplashRingSeconds;
constexpr float kSplashReach = kWaterSplashReach;
constexpr int kMaxSplashes = kWaterMaxSplashes;

struct FrameInputs : WaterFrameValues
{
    NativeLiquidFrame nativeLiquids;
    float reflectedFogWeight = 0;
    WaterSceneryPoseRequests* sceneryPoses=nullptr; // render-owner only, after immutable frame capture
    WaterLampSamples lamps{};
    // Native material lighting is distinct from the visible celestial direction.
    // Captured on the game thread; the public WaterFrameValues ABI stays unchanged.
    bool skyPaletteKnown=false;
    uint32_t skyPalette[6]={}; // current native zenith-to-horizon bands; no solar disc
    bool sceneLightingKnown = false;
    float sceneToLight[3] = {};
    uint32_t sceneAmbientColor = 0, sceneDirectColor = 0;
    uint32_t modelMilliseconds=0; // captured once; queued rendering never reads a live animation clock
    uint32_t zoneId = 0; // client AreaTable zone, including building interiors; zero when unavailable
    uint32_t subzoneId = 0;
    int32_t mapId = -1; // captured with the world view; unknown never admits owned scene data
    // The camera is under a liquid surface (water or slime) and its height is known (cameraWaterHeight): the reflection
    // is then drawn from below, mirrored at that surface.
    bool CameraUnderSurface() const { return (cameraInWater || cameraInSlime) && cameraWaterHeightKnown; }
    unsigned reflectionObjectCount=0;
    ReflectionObject reflectionObjects[kMaxReflectionObjects]={};
    const GroundMap* ground = nullptr;
    // Coarser and reaching further; only while lava has been drawn lately.
    const GroundMap* farGround = nullptr;
};
