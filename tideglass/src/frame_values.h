#pragma once

#include <d3d9.h>
#include <cstdint>

// Plain values shared across the local host ABI; no STL or allocator ownership crosses it.
struct WaterMover
{
    float position[3];
    float radius;
    float strength;
    uint32_t continuous;
};

constexpr int kWaterMaxMovers = 16;

constexpr int kWaterBoatTrailPoints = 24;

// A point of a ship's path: where it was, how long ago (seconds) and how fast it was going then.
struct WaterWakePoint
{
    float x;
    float y;
    float age;
    float speed;
};

// A ship moving on the water: where it is, which way it heads and how fast, and the path it came along (its position
// first, then a point every few yards back; trailCount of them), for its wake.
struct WaterBoat
{
    float position[3];
    float direction[2];
    float speed;
    WaterWakePoint trail[kWaterBoatTrailPoints];
    int trailCount;
    // How many frames its position was read, and in how many it had changed (for the log).
    int positionReads;
    int positionChanges;
};

constexpr int kWaterMaxBoats = 4;

// A raindrop splash the game has just drawn: where it landed and how long ago (seconds).
struct WaterSplash
{
    float x;
    float y;
    float z;
    float age;
};

// WaterSplashes are passed on for as long as their rings last (rain_rings.hlsli), this near the camera, this many at most.
constexpr float kWaterSplashRingSeconds = 1.6f;
constexpr float kWaterSplashReach = 25.0f;
constexpr int kWaterMaxSplashes = 8192;

struct WaterFrameValues
{
    float cameraRelativeView[16];
    float glProjection[16];
    float cameraPosition[3];
    D3DVIEWPORT9 viewport;
    uint32_t cameraInWater;
    uint32_t cameraInSlime;
    // Whether the host suppressed native distance fog for this frame's water/underwater treatment.
    uint32_t stockFogPushed;
    float toLight[3];
    uint32_t lightIsMoon;
    float daylight;
    float goldenHour;
    uint32_t directColor;
    uint32_t sunColor;
    float stormWeight;
    // The game's weather (0 none, 1 rain, 2 snow, 3 sandstorm) and its intensity now (0..1), and how hard it rains.
    uint32_t weatherType;
    float weatherIntensity;
    float rain;
    uint32_t ambientColor;
    uint32_t fogColor;
    float fogStart;
    float fogEnd;
    float reflectionActor[4] = {}; // local player feet xyz and exclusion radius; w=0 when unavailable
    WaterMover movers[kWaterMaxMovers];
    int moverCount;
    WaterBoat boats[kWaterMaxBoats];
    int boatCount;
    // The splashes near the camera whose rings still show (nearest first), and how far from the camera the game places
    // splashes at all (0 when it places none).
    const WaterSplash* splashes;
    int splashCount;
    float splashReach;
    uint32_t cameraWaterHeightKnown = false;
    float cameraWaterHeight = 0;
};
