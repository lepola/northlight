#include "engine.h"
#include "liquid.h"
#include "native_fog.h"
#include "client_camera.h"
#include "client_image.h"
#include "client_unit_size.h"
#include "visual_time.h"
#include <atomic>
#include "config.h"

#include "ground.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace engine
{
namespace
{
constexpr uintptr_t kClientImageBase = 0x00400000;

constexpr uintptr_t kGxDevice = water::client12340::devicePointer;
constexpr uintptr_t kGxD3DDevice = 0x397C;
constexpr uintptr_t kViewGlobal = water::client12340::viewGlobal;
constexpr uintptr_t kProjectionGlobal = water::client12340::projectionGlobal;
constexpr uintptr_t kCameraPosition = 0x00CD8F5C;
// The world's line test: start, end, hit point, hit fraction (in/out), what to hit, unused. 0x100 hits the terrain
// only (not models, buildings or liquid).
constexpr uintptr_t kWorldIntersect = 0x0077F310;
constexpr uint32_t kIntersectTerrainOnly = 0x100;
constexpr uint32_t kIntersectLiquidOnly = 0x30000;
// Answers for water alone: what is liquid (above) and not this is magma or slime. Seen in game over lava and the sea.
constexpr uint32_t kIntersectWaterOnly = 0x10000;
constexpr DWORD kGroundLogIntervalMs = 30000;
constexpr uintptr_t kCurrentMap = 0x00AB63BC;


constexpr uintptr_t kClientConnection = 0x00C79CE0;
constexpr uintptr_t kObjectManager = 0x2ED0;
constexpr uintptr_t kFirstObject = 0xAC;
constexpr uintptr_t kNextObject = 0x3C;
constexpr uintptr_t kLocalGuid = 0xC0;
constexpr uintptr_t kObjectType = 0x14;
constexpr uintptr_t kObjectGuid = 0x30;
constexpr uintptr_t kUnitPosition = 0x798;
constexpr uintptr_t kGameObjectPosition = 0xE8;
constexpr uintptr_t kTransportPosition = 0x1D8;
// A transport's rotation: the first axis of its model, which points from the bow back to the stern.
constexpr uintptr_t kTransportAxis = 0x1A8;
constexpr uint64_t kTransportGuidHigh = 0x1FC0;
constexpr uint32_t kUnitType = 3;
constexpr uint32_t kPlayerType = 4;
constexpr uint32_t kGameObjectType = 5;
constexpr int kMaxObjectsWalked = 4096;
constexpr float kMoverRange = 45.0f;
constexpr float kBoatRange = 600.0f;
// A ship that stops or is no longer reported leaves its wake to settle where it was: the foam at its hull eases out
// over this long (sooner for a ship gone from view, whose hull is no longer there), and the wake behind it spreads and
// fades as it ages, gone by the time it is dropped.
constexpr float kShipStoppingSeconds = 4.0f;
constexpr float kShipVanishingSeconds = 1.0f;
constexpr DWORD kShipWakeSettleMs = 45000;
// A ship reported this much further on than any ship sails (yd/s, plus yards) was placed afresh (as when it appears
// after a loading screen and is then set right): its path so far is dropped rather than taken for sailing.
constexpr float kShipFastest = 40.0f;
constexpr float kShipJumpMargin = 5.0f;
constexpr float kMovingSpeed = 0.4f;
constexpr float kBoatSpeed = 0.6f;
constexpr float kBoatTrailStep = 8.0f;
constexpr int kShipHistory = 16;
constexpr DWORD kShipHistoryStepMs = 100;
constexpr DWORD kShipStoppedMs = 3000;
// Until a ship has been watched this long its speed is not trusted (the first few frames made it look fast).
constexpr float kShipMeasureMinSeconds = 0.5f;
constexpr float kShipSmoothingSeconds = 0.6f;
// A ship's model facing is matched to the way it sails once it goes this fast (yd/s) and the two agree this well.
constexpr float kShipSignSpeed = 3.0f;
constexpr float kShipSignAgreement = 0.8f;
constexpr float kUnitRadius = 0.3f;
constexpr float kUnitWakeStrength = 0.022f;
constexpr float kIdleRingStrength = 0.06f;
constexpr float kSplashStrength = 0.25f;
constexpr DWORD kIdlePulseMs = 1100;
constexpr DWORD kSplashPulseMs = 350;
constexpr float kSplashVerticalSpeed = 3.0f;
constexpr float kSplashFullSpeed = 12.0f;
constexpr float kMaxWakeSpeed = 8.0f;
constexpr DWORD kTrackForgetMs = 5000;
constexpr DWORD kObjectLogIntervalMs = 15000;
constexpr DWORD kCameraComparisonIntervalMs = 60000;
constexpr uintptr_t kCameraLiquidType = 0x00CD8794;

constexpr uintptr_t kDayFraction = 0x00D38B04;
constexpr float kTwilight = 0.06f;
constexpr float kGoldenHourInsideDay = 0.042f;
constexpr float kGoldenHourWidth = 0.065f;
constexpr uintptr_t kSunDayStart = 0x00A41CA4;
constexpr uintptr_t kSunDayEnd = 0x00A41CA0;
constexpr uintptr_t kSkyCenter = 0x00D38B18;
constexpr uintptr_t kSunPosition = 0x00D38E28;
constexpr uintptr_t kMoonPosition = 0x00D38E48;
// 3.3.5a outdoor material-light block: direction, then packed direct/ambient.
// The native world uses this light, while the celestial body position above is
// used by water glints. Keep those meanings separate.
constexpr uintptr_t kSceneLight = 0x00D38C9C;
constexpr uintptr_t kDirectColor = 0x00D38BD8;
constexpr uintptr_t kAmbientColor = 0x00D38BD4;
constexpr uintptr_t kSunColor = 0x00D38BF8;
constexpr uintptr_t kStormWeight = 0x00D38B88;
constexpr uintptr_t kFogColor = 0x00D38BA0;
// The weather (set by the server's weather message, handled at 0x526A69 and 0x7846A0): a pointer to the object that
// holds the intensity now (0..1, at 0x08), eased each frame (at 0x784850) from the intensity before (0x04) to the one
// asked for (0x00), and the effect drawing it: rain, snow or a sandstorm, whichever of these is set (as the game
// itself tells, at 0x783B60).
constexpr uintptr_t kWeather = 0x00CD7544;
constexpr uintptr_t kWeatherIntensity = 0x08;
constexpr uintptr_t kWeatherEffects[3] = {0x13C, 0x140, 0x144};
constexpr uint32_t kWeatherRain = 1;
// The rain (from the weather object) draws its drops in packets, a list of them (at 0xAC: the first, a set low bit
// ending it; each links to the next at 0x04). A packet holds its splashes from 0x0C, each a position, something unused
// here, and when it shows and ends (in 1/1024ths of a second from the packet's start, counted on the game clock).
// Read from the rain's splash drawing at 0x7891B0 (and its shader version at 0x78A030).
constexpr uintptr_t kWeatherRainObject = kWeatherEffects[0];
constexpr uintptr_t kRainPackets = 0xAC;
constexpr uintptr_t kPacketNext = 0x04;
constexpr uintptr_t kPacketSplashes = 0x0C;
constexpr uintptr_t kPacketSplashCount = 0x2400C;
constexpr uintptr_t kPacketStart = 0x24010;
constexpr uint32_t kPacketMaxSplashes = (kPacketSplashCount - kPacketSplashes) / 0x18;
constexpr uintptr_t kGameClock = 0x00CD7694;
constexpr float kGameClockSecondsPerTick = 1.0f / 1024.0f;
constexpr int kMaxRainPackets = 64;
constexpr DWORD kSplashLogIntervalMs = 10000;

struct RainSplash
{
    float position[3];
    float unused;
    float shows;
    float ends;
};
static_assert(sizeof(RainSplash) == 0x18, "a rain packet's splash");
constexpr uintptr_t kFogStart = 0x00D38BA4;
constexpr uintptr_t kFogEnd = 0x00D38BA8;
constexpr uintptr_t kFogGroupStart[2] = {0x00D38B90, kFogStart};
constexpr uintptr_t kFogGroupEnd[2] = {0x00D38B94, kFogEnd};
constexpr float kOutOfRangeFogStart = 50000.0f;

water::NativeFogOverride g_stockFog;
std::atomic<uint64_t> g_visualTimeSettings{0x00000000bf800000ull}; // hour=-1, period=0
// The time-of-day clock is for test pictures. It redirects two of the client's calls, so a cooperative
// core has it only when built for development (WATER_VISUAL_CLOCK); player builds never change the client.
#if defined(WATER_COOPERATIVE_BUILD) && defined(WATER_VISUAL_CLOCK)
#define WATER_COOPERATIVE_CLOCK 1
// Release packaging refuses a core carrying this text.
extern "C" const char kVisualClockMarker[]="TIDEGLASS VISUAL CLOCK BUILD";
water::VisualDayCycle g_visualDayCycle;
water::client12340::DayFractionCalls g_visualClockCalls;
std::atomic<bool> g_visualClockEnabled{false};
DWORD g_visualClockThread=0;
bool g_visualClockPinned=false;
bool WriteVisualCall(uintptr_t address,const void* data,size_t size)
{
    if(size!=5||static_cast<const unsigned char*>(data)[0]!=0xe8)return false;
    auto* target=reinterpret_cast<unsigned char*>(address);
    DWORD previous=0;
    if(!VirtualProtect(target+1,4,PAGE_EXECUTE_READWRITE,&previous))return false;
    std::memcpy(target+1,static_cast<const unsigned char*>(data)+1,4);
    DWORD ignored=0;
    const bool protectedAgain=VirtualProtect(target+1,4,previous,&ignored)!=0;
    return FlushInstructionCache(GetCurrentProcess(),target,5)&&protectedAgain;
}
#endif


struct OpaqueState : water::client12340::Camera { int32_t mapId = -1; };

OpaqueState g_opaque = {};

bool ReadMemory(uintptr_t address, void* out, size_t size)
{
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) &&
           read == size;
}

template <typename T>
bool Read(uintptr_t address, T& out)
{
    return ReadMemory(address, &out, sizeof(T));
}

uintptr_t GxDevice()
{
    uintptr_t gx = 0;
    return Read(kGxDevice, gx) ? gx : 0;
}

bool ReadLight(FrameInputs& out)
{
    float day = 0.0f, dayStart = 0.0f, dayEnd = 0.0f;
    float center[3], body[3];
    if (!Read(kDayFraction, day) || !Read(kSunDayStart, dayStart) || !Read(kSunDayEnd, dayEnd))
        return false;
    out.lightIsMoon = !(day >= dayStart && day <= dayEnd);
    const auto ramp = [](float edge, float value) {
        const float t = std::clamp((value - edge + kTwilight) / (2.0f * kTwilight), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    out.daylight = ramp(dayStart, day) * (1.0f - ramp(dayEnd, day));
    const auto bump = [](float centre, float value) {
        const float t = std::clamp(1.0f - std::fabs(value - centre) / kGoldenHourWidth, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    out.goldenHour = std::max(bump(dayStart + kGoldenHourInsideDay, day), bump(dayEnd - kGoldenHourInsideDay, day));
    if (!ReadMemory(kSkyCenter, center, sizeof(center)) ||
        !ReadMemory(out.lightIsMoon ? kMoonPosition : kSunPosition, body, sizeof(body)))
        return false;
    float length = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        out.toLight[i] = body[i] - center[i];
        length += out.toLight[i] * out.toLight[i];
    }
    length = std::sqrt(length);
    if (!(length > 1e-4f))
        return false;
    for (float& v : out.toLight)
        v /= length;
    if (!Read(kStormWeight, out.stormWeight) || !std::isfinite(out.stormWeight))
        out.stormWeight = 0.0f;
    out.stormWeight = out.stormWeight < 0.0f ? 0.0f : out.stormWeight > 1.0f ? 1.0f : out.stormWeight;
    uint32_t weather = 0;
    out.weatherType = 0;
    out.weatherIntensity = 0.0f;
    if (Read(kWeather, weather) && weather && Read(weather + kWeatherIntensity, out.weatherIntensity) &&
        std::isfinite(out.weatherIntensity))
    {
        out.weatherIntensity = std::clamp(out.weatherIntensity, 0.0f, 1.0f);
        for (uint32_t type = 1; type <= 3 && !out.weatherType; ++type)
        {
            uint32_t effect = 0;
            if (Read(weather + kWeatherEffects[type - 1], effect) && effect)
                out.weatherType = type;
        }
    }
    out.rain = out.weatherType == kWeatherRain ? out.weatherIntensity : 0.0f;
    bool fogRead = true;
    if (g_stockFog.Active())
    {
        out.fogStart = g_stockFog.Saved().start[1];
        out.fogEnd = g_stockFog.Saved().end[1];
    }
    else
        fogRead = Read(kFogStart, out.fogStart) && Read(kFogEnd, out.fogEnd);
    const bool lightRead = Read(kDirectColor, out.directColor) && Read(kSunColor, out.sunColor) && Read(kAmbientColor, out.ambientColor) &&
           Read(kFogColor, out.fogColor) && fogRead &&
           std::isfinite(out.fogStart) && std::isfinite(out.fogEnd);
    struct MaterialLight { float direction[3];uint32_t direct,ambient; } first={},second={};
    static_assert(sizeof(MaterialLight)==20,"native material-light layout");
    out.sceneLightingKnown=false;
    if(lightRead && ReadMemory(kSceneLight,&first,sizeof first) && ReadMemory(kSceneLight,&second,sizeof second) &&
       !std::memcmp(&first,&second,sizeof first)) {
        float magnitude=0,cameraError=0;
        for(unsigned i=0;i<3;++i){
            magnitude+=first.direction[i]*first.direction[i];
            const float delta=center[i]-out.cameraPosition[i];cameraError+=delta*delta;
        }
        if(std::isfinite(magnitude)&&magnitude>=.25f&&magnitude<=2.25f&&std::isfinite(cameraError)&&cameraError<=1600.f){
            const float inverse=1/std::sqrt(magnitude);
            for(unsigned i=0;i<3;++i)out.sceneToLight[i]=-first.direction[i]*inverse;
            out.sceneDirectColor=first.direct;out.sceneAmbientColor=first.ambient;out.sceneLightingKnown=true;
        }
    }
    // Six packed native sky colours (3.3.5a sky gradient consumer at 0x7F0530).
    // Capture on this existing input owner, never from command replay.
    uint32_t palette[6]={},again[6]={};
    out.skyPaletteKnown=lightRead&&ReadMemory(kAmbientColor+12,palette,sizeof palette)&&
        ReadMemory(kAmbientColor+12,again,sizeof again)&&!std::memcmp(palette,again,sizeof palette);
    if(out.skyPaletteKnown)std::memcpy(out.skyPalette,palette,sizeof palette);
    return lightRead;
}

struct Track
{
    float position[3];
    DWORD seen;
    float speed;
    float verticalSpeed;
    DWORD lastPulse;
    DWORD lastSplash;
    // Ships: the path behind them, newest first (see RecordTrail).
    struct TrailPoint
    {
        float x;
        float y;
        DWORD time;
        float speed;
    } trail[kBoatTrailPoints - 1];
    int trailCount;
    // Ships: where they were over the last second or so, for a steady speed and heading (see ShipMotion), and since
    // when they have been too slow to count as sailing.
    float history[kShipHistory][2];
    DWORD historyTime[kShipHistory];
    int historyCount;
    DWORD slowSince;
    // Ships: whether their model's facing points the way they sail (+1) or the other way (-1), once seen sailing
    // straight (0 until then).
    float facingSign;
    // Ships: their speed and heading, smoothed (see MeasureShip), and when they were last measured.
    float shipSpeed;
    float shipHeading[2];
    DWORD shipMeasured;
    // Ships: how many frames their position was read, and how many of those it had changed (logged).
    float lastRead[2];
    int reads;
    int changes;
    // Which way it has been moving across the water (smoothed, a unit vector once it has moved).
    float heading[2];
};

struct Motion
{
    float speed;
    float verticalSpeed;
    float heading[2];
    Track* track;
};

std::unordered_map<uint64_t, Track> g_tracks;

bool ValidPointer(uintptr_t value)
{
    return value >= 0x10000 && value < 0x7FFF0000 && (value & 3) == 0;
}

bool FinitePosition(const float* p)
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) && std::fabs(p[0]) < 50000.0f &&
           std::fabs(p[1]) < 50000.0f && std::fabs(p[2]) < 50000.0f;
}

struct SeenObject
{
    uint64_t guid;
    uint32_t type;
    uintptr_t object=0;
    float position[3];
    // Transports: which way the model faces (a unit vector), if it could be read.
    bool facingKnown;
    float facing[2];
};

int WalkObjects(std::vector<SeenObject>& out, uint64_t& localGuid)
{
    uintptr_t connection = 0, manager = 0, object = 0;
    if (!Read(kClientConnection, connection) || !ValidPointer(connection) ||
        !Read(connection + kObjectManager, manager) || !ValidPointer(manager) ||
        !Read(manager + kFirstObject, object) || !Read(manager + kLocalGuid, localGuid))
        return 0;
    int walked = 0;
    for (; ValidPointer(object) && walked < kMaxObjectsWalked; ++walked)
    {
        SeenObject seen = {};seen.object=object;
        // The client keeps these three list fields in the same object header.
        // A partial tail can still describe the last readable object.
        unsigned char header[kNextObject + sizeof(uint32_t) - kObjectType];
        uint32_t next = 0;
        if (ReadMemory(object + kObjectType, header, sizeof(header)))
        {
            std::memcpy(&seen.type, header, sizeof(seen.type));
            std::memcpy(&seen.guid, header + (kObjectGuid - kObjectType), sizeof(seen.guid));
            std::memcpy(&next, header + (kNextObject - kObjectType), sizeof(next));
        }
        else
        {
            if (!Read(object + kObjectType, seen.type) || !Read(object + kObjectGuid, seen.guid))
                break;
            if (!Read(object + kNextObject, next))
                next = static_cast<uint32_t>(object);
        }
        // Transports (ships, zeppelins) keep their moving position apart from the one they were placed at.
        const bool transport = seen.type == kGameObjectType && (seen.guid >> 48) == kTransportGuidHigh;
        const uintptr_t position = transport ? kTransportPosition
                                             : seen.type == kGameObjectType ? kGameObjectPosition : kUnitPosition;
        if ((seen.type == kUnitType || seen.type == kPlayerType || seen.type == kGameObjectType) &&
            ReadMemory(object + position, seen.position, sizeof(seen.position)) && FinitePosition(seen.position))
        {
            float axis[2] = {};
            const float length = transport && ReadMemory(object + kTransportAxis, axis, sizeof(axis))
                                     ? std::sqrt(axis[0] * axis[0] + axis[1] * axis[1])
                                     : 0.0f;
            seen.facingKnown = std::isfinite(length) && length > 0.5f && length < 1.5f;
            if (seen.facingKnown)
            {
                seen.facing[0] = -axis[0] / length;
                seen.facing[1] = -axis[1] / length;
            }
            out.push_back(seen);
        }
        if (next == object)
            break;
        object = next;
    }
    return walked;
}

Motion TrackMotion(const SeenObject& object, DWORD now)
{
    auto found = g_tracks.find(object.guid);
    if (found == g_tracks.end())
    {
        Track& added = g_tracks
                           .emplace(object.guid, Track{{object.position[0], object.position[1], object.position[2]},
                                                       now, 0.0f, 0.0f, 0, 0, {}, 0, {}, {}, 0, 0, 0.0f, 0.0f, {}, 0, {}, 0, 0, {}})
                           .first->second;
        return {0.0f, 0.0f, {0.0f, 0.0f}, &added};
    }
    Track& track = found->second;
    const DWORD elapsed = now - track.seen;
    if (elapsed >= 30)
    {
        const float seconds = elapsed / 1000.0f;
        const float dx = object.position[0] - track.position[0];
        const float dy = object.position[1] - track.position[1];
        const float dz = object.position[2] - track.position[2];
        const float instant = std::sqrt(dx * dx + dy * dy) / seconds;
        track.speed = track.speed * 0.6f + std::min(instant, 40.0f) * 0.4f;
        const float moved = std::sqrt(dx * dx + dy * dy);
        if (moved > 1.0e-3f)
        {
            const float hx = track.heading[0] * 0.6f + dx / moved * 0.4f;
            const float hy = track.heading[1] * 0.6f + dy / moved * 0.4f;
            const float length = std::sqrt(hx * hx + hy * hy);
            if (length > 1.0e-3f)
            {
                track.heading[0] = hx / length;
                track.heading[1] = hy / length;
            }
        }
        track.verticalSpeed = std::clamp(dz / seconds, -40.0f, 40.0f);
        std::memcpy(track.position, object.position, sizeof(track.position));
        track.seen = now;
    }
    return {track.speed, track.verticalSpeed, {track.heading[0], track.heading[1]}, &track};
}

// Keeps a point every kBoatTrailStep yards of a ship's path (with when it was there and how fast it went), so its wake
// follows the way it turned and knows how old the water behind it is.
void RecordTrail(Track& track, const float* position, float speed, DWORD now)
{
    if (track.trailCount > 0)
    {
        const float dx = position[0] - track.trail[0].x;
        const float dy = position[1] - track.trail[0].y;
        if (dx * dx + dy * dy < kBoatTrailStep * kBoatTrailStep)
            return;
    }
    constexpr int kKept = kBoatTrailPoints - 1;
    std::memmove(&track.trail[1], &track.trail[0], sizeof(track.trail[0]) * (kKept - 1));
    track.trail[0] = {position[0], position[1], now, speed};
    track.trailCount = std::min(track.trailCount + 1, kKept);
}

struct ShipMotion
{
    float speed;
    float heading[2];
};

// A ship's speed and heading from how far it went over the last second or so. (A ship's position does not change
// every frame, so from one frame to the next it can look stopped.)
ShipMotion MeasureShip(Track& track, const float* position, DWORD now)
{
    ++track.reads;
    if (position[0] != track.lastRead[0] || position[1] != track.lastRead[1])
        ++track.changes;
    track.lastRead[0] = position[0];
    track.lastRead[1] = position[1];
    if (track.historyCount == 0 || now - track.historyTime[0] >= kShipHistoryStepMs)
    {
        std::memmove(track.history[1], track.history[0], sizeof(track.history[0]) * (kShipHistory - 1));
        std::memmove(track.historyTime + 1, track.historyTime, sizeof(track.historyTime[0]) * (kShipHistory - 1));
        track.history[0][0] = position[0];
        track.history[0][1] = position[1];
        track.historyTime[0] = now;
        track.historyCount = std::min(track.historyCount + 1, kShipHistory);
    }
    ShipMotion motion = {};
    const int oldest = track.historyCount - 1;
    const float seconds = (now - track.historyTime[oldest]) / 1000.0f;
    if (oldest < 1 || seconds < kShipMeasureMinSeconds)
        return motion;
    const float dx = position[0] - track.history[oldest][0];
    const float dy = position[1] - track.history[oldest][1];
    const float moved = std::sqrt(dx * dx + dy * dy);
    const float speed = moved / seconds;
    const float heading[2] = {moved > 1.0e-3f ? dx / moved : track.shipHeading[0],
                              moved > 1.0e-3f ? dy / moved : track.shipHeading[1]};
    // Smoothed as well, so the wake does not step each time a sample joins the window.
    if (track.shipMeasured == 0)
    {
        track.shipSpeed = speed;
        track.shipHeading[0] = heading[0];
        track.shipHeading[1] = heading[1];
    }
    else
    {
        const float elapsed = static_cast<float>(now - track.shipMeasured) / 1000.0f;
        const float blend = 1.0f - std::exp(-elapsed / kShipSmoothingSeconds);
        track.shipSpeed += (speed - track.shipSpeed) * blend;
        const float hx = track.shipHeading[0] + (heading[0] - track.shipHeading[0]) * blend;
        const float hy = track.shipHeading[1] + (heading[1] - track.shipHeading[1]) * blend;
        const float length = std::sqrt(hx * hx + hy * hy);
        if (length > 1.0e-3f)
        {
            track.shipHeading[0] = hx / length;
            track.shipHeading[1] = hy / length;
        }
    }
    track.shipMeasured = now;
    motion.speed = track.shipSpeed;
    motion.heading[0] = track.shipHeading[0];
    motion.heading[1] = track.shipHeading[1];
    return motion;
}

// Ships lately seen sailing, as they were, so their wakes can settle when they stop or go.
struct RecentShip
{
    uint64_t guid;
    Boat boat;
    DWORD seen;
};
std::vector<RecentShip> g_recentShips;

// Ships whose wakes are settling (see kShipWakeSettleMs): as they were when they stopped or went, since when, and
// how long their hull's foam takes to ease out.
struct SettlingShip
{
    uint64_t guid;
    Boat boat;
    DWORD since;
    float stopping;
};
std::vector<SettlingShip> g_settlingShips;

// A settling ship as it is now: where it stopped, slowing to nothing (so its hull's foam eases out), its path aging.
Boat Settled(const SettlingShip& settling, DWORD now)
{
    Boat boat = settling.boat;
    const float seconds = (now - settling.since) / 1000.0f;
    boat.speed *= std::max(0.0f, 1.0f - seconds / settling.stopping);
    for (int i = 0; i < boat.trailCount; ++i)
        boat.trail[i].age += seconds;
    return boat;
}

// Ships that stopped sailing (docked) or are no longer reported leave their wakes settling where they were; a ship
// sailing again, or placed afresh (resetShips), has its settling wake dropped.
void SettleShips(const std::vector<uint64_t>& seenShips, const std::vector<uint64_t>& sailingShips,
                 const std::vector<uint64_t>& resetShips, Boat* boats, int maxBoats, int& boatCount, DWORD now)
{
    auto listed = [](const std::vector<uint64_t>& guids, uint64_t guid) {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    };
    g_settlingShips.erase(std::remove_if(g_settlingShips.begin(), g_settlingShips.end(),
                                         [&](const SettlingShip& s) {
                                             return listed(sailingShips, s.guid) || listed(resetShips, s.guid) ||
                                                    now - s.since > kShipWakeSettleMs;
                                         }),
                          g_settlingShips.end());
    g_recentShips.erase(std::remove_if(g_recentShips.begin(), g_recentShips.end(),
                                       [&](const RecentShip& r) { return listed(resetShips, r.guid); }),
                        g_recentShips.end());
    for (int i = 0; i < static_cast<int>(sailingShips.size()); ++i)
    {
        auto recent = std::find_if(g_recentShips.begin(), g_recentShips.end(),
                                   [&](const RecentShip& r) { return r.guid == sailingShips[i]; });
        if (recent == g_recentShips.end())
            g_recentShips.push_back({sailingShips[i], boats[i], now});
        else
            *recent = {sailingShips[i], boats[i], now};
    }
    g_recentShips.erase(std::remove_if(g_recentShips.begin(), g_recentShips.end(),
                                       [&](const RecentShip& r) {
                                           const bool sailing = listed(sailingShips, r.guid);
                                           if (!sailing)
                                           {
                                               const bool gone = !listed(seenShips, r.guid);
                                               g_settlingShips.push_back(
                                                   {r.guid, r.boat, r.seen,
                                                    gone ? kShipVanishingSeconds : kShipStoppingSeconds});
                                           }
                                           return !sailing;
                                       }),
                        g_recentShips.end());
    for (const SettlingShip& settling : g_settlingShips)
        if (boatCount < maxBoats)
            boats[boatCount++] = Settled(settling, now);
}

void ForgetStaleTracks(DWORD now)
{
    for (auto it = g_tracks.begin(); it != g_tracks.end();)
        it = now - it->second.seen > kTrackForgetMs ? g_tracks.erase(it) : std::next(it);
}

void LogObjects(int walked, const std::vector<SeenObject>& objects, uint64_t localGuid, const float* camera)
{
    static DWORD lastLogged = 0;
    const DWORD now = GetTickCount();
    if (lastLogged && now - lastLogged < kObjectLogIntervalMs)
        return;
    lastLogged = now;
    int counts[8] = {};
    const SeenObject* local = nullptr;
    for (const SeenObject& o : objects)
    {
        if (o.type < 8)
            ++counts[o.type];
        if (o.guid == localGuid)
            local = &o;
    }
    WATER_LOG_DEBUG("objects: walked %d, units %d, players %d, game objects %d; camera (%.1f %.1f %.1f); local player "
                   "%s (%.1f %.1f %.1f)",
                   walked, counts[kUnitType], counts[kPlayerType], counts[kGameObjectType], camera[0], camera[1],
                   camera[2], local ? "found" : "missing", local ? local->position[0] : 0.0f,
                   local ? local->position[1] : 0.0f, local ? local->position[2] : 0.0f);
}

bool ReadCameraState(IDirect3DDevice9* device, bool fromGlobals, OpaqueState& state)
{
    D3DVIEWPORT9 viewport = {};
    return device && SUCCEEDED(device->GetViewport(&viewport)) &&
           water::client12340::ReadCamera(ReadMemory, viewport, !fromGlobals, state);
}

void CompareCameraGlobals(const OpaqueState& gx)
{
    static DWORD lastLogged = 0;
    if (!gx.valid || (lastLogged && GetTickCount() - lastLogged < kCameraComparisonIntervalMs))
        return;
    float view[16], proj[16];
    if (!ReadMemory(kViewGlobal, view, sizeof(view)) || !ReadMemory(kProjectionGlobal, proj, sizeof(proj)))
        return;
    lastLogged = GetTickCount();
    float viewDifference = 0.0f, projDifference = 0.0f;
    for (int i = 0; i < 16; ++i)
    {
        viewDifference = std::max(viewDifference, std::fabs(view[i] - gx.cameraRelativeView[i]));
        projDifference = std::max(projDifference, std::fabs(proj[i] - gx.glProjection[i]));
    }
    WATER_LOG_DEBUG("camera globals vs render state at the opaque pass: view differs by %.5f, projection by %.5f",
                   viewDifference, projDifference);
}
}

bool IsSupportedClient()
{
    static const bool supported = water::client12340::SupportedImage(
        ReadMemory, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), kClientTimestamp, kClientImageBase);
    return supported;
}

void* GameD3DDevice()
{
    uintptr_t gx = GxDevice();
    void* device = nullptr;
    return gx && Read(gx + kGxD3DDevice, device) ? device : nullptr;
}

uint32_t CameraLiquidType()
{
    uint32_t type = 0;
    return Read(kCameraLiquidType, type) ? type : 0;
}

// The height of the liquid surface over the camera, water or slime (the client's camera liquid depth is either's).
bool CameraWaterHeight(float& height){
    float z=0,depth=0;
    if(!(CameraInWater()||CameraInSlime()) || !Read(0x00CD8F64,z) || !Read(0x00CD8790,depth) ||
       !std::isfinite(z) || !std::isfinite(depth) || depth<=0 || depth>1024)return false;
    height=z+depth;return std::isfinite(height);
}

NativeLiquidFrame g_liquidIdentity;
const NativeLiquidFrame& LiquidIdentity(){return g_liquidIdentity;}
void CaptureLiquidIdentity(LiquidTextureMap map,void* context){
    static const bool supported=IsSupportedClient()&&water::client12340::LiquidSourceSupported(ReadMemory);
    g_liquidIdentity={};
    // A real native capture that cannot be decoded stays unknown. The colour
    // fallback is reserved for callers supplying independent copied frames.
    g_liquidIdentity.known=true;
    if(!supported)return;
    g_liquidIdentity=water::client12340::LiquidSource<decltype(&ReadMemory)>(&ReadMemory).Capture([&](uintptr_t p){
        return map?map(context,p):p;
    });
    if(GlobalConfig().Get().traceLiquids){
        static unsigned previous=~0u;
        const unsigned state=unsigned(g_liquidIdentity.camera)|(unsigned(g_liquidIdentity.water[0])<<2)|(unsigned(g_liquidIdentity.water[1])<<4);
        if(previous!=state){
            WATER_LOG_INFO("native liquid identity camera=%s ocean=%s river=%s textures=%u",LiquidKindName(g_liquidIdentity.camera),LiquidKindName(g_liquidIdentity.water[0]),LiquidKindName(g_liquidIdentity.water[1]),unsigned(g_liquidIdentity.textures.size()));
            previous=state;
        }
    }
}

bool CameraInSlime()
{
    return CameraLiquidType()!=0 && g_liquidIdentity.camera==LiquidKind::Slime;
}

bool CameraInWater()
{
    return CameraLiquidType()!=0 && g_liquidIdentity.camera==LiquidKind::Water;
}

void WriteNativeFog(const water::NativeFogRanges& ranges)
{
    for (unsigned group = 0; group != 2; ++group)
    {
        *reinterpret_cast<float*>(kFogGroupStart[group]) = ranges.start[group];
        *reinterpret_cast<float*>(kFogGroupEnd[group]) = ranges.end[group];
    }
}

void PushStockFogOutOfRange()
{
    g_stockFog.Suppress([](water::NativeFogRanges& ranges) {
        for (unsigned group = 0; group != 2; ++group)
            if (!Read(kFogGroupStart[group], ranges.start[group]) ||
                !Read(kFogGroupEnd[group], ranges.end[group]))
                return false;
        return true;
    }, WriteNativeFog, kOutOfRangeFogStart);
}

void RestoreStockFog()
{
    g_stockFog.Restore(WriteNativeFog);
}

// What the game's console calls waterRipples: read only where a wake would be added to the water (0x79D465), which does nothing at 0.
int SwapNativeRipples(int value)
{
    auto* const shown=reinterpret_cast<int*>(kNativeRipplesSwitch);
    const int was=*shown;*shown=value;return was;
}

void ConfigureVisualTime(float hour,float cycleSeconds)
{
    const float values[]={hour,cycleSeconds};uint64_t bits;std::memcpy(&bits,values,sizeof bits);
    g_visualTimeSettings.store(bits,std::memory_order_release);
}
#if defined(WATER_COOPERATIVE_CLOCK)
// Same native getter/float-return ABI as standalone's DayFractionThunk. The
// native result is filtered at its read point, before either lighting update.
extern "C" {
uintptr_t g_visualDayFractionTarget=kDayFractionTarget;
__attribute__((force_align_arg_pointer)) float __cdecl water_visual_day_fraction(float real)
{
    if(!g_visualClockEnabled.load(std::memory_order_acquire)||!std::isfinite(real)||real<0||real>=1)return real;
    const auto bits=g_visualTimeSettings.load(std::memory_order_acquire);float values[2];std::memcpy(values,&bits,sizeof bits);
    return g_visualDayCycle.Evaluate(real,GetTickCount(),values[0],values[1]);
}
__attribute__((naked)) void VisualDayFractionThunk()
{
    asm("calll *_g_visualDayFractionTarget\n\t"
        "pushl %eax\n\tpushl %ecx\n\tpushl %edx\n\t"
        "subl $4, %esp\n\tfstps (%esp)\n\t"
        "calll _water_visual_day_fraction\n\t"
        "addl $4, %esp\n\tpopl %edx\n\tpopl %ecx\n\tpopl %eax\n\tretl\n\t");
}
}
bool RestoreVisualTime()
{
    g_visualClockEnabled.store(false,std::memory_order_release);
    if(!g_visualClockCalls.AnyOwned())return true;
    // A caller retiring on another thread can disable filtering immediately.
    // Never rewrite executable calls there. The pinned image keeps remaining
    // callbacks safe through process exit, even if a host ignores Close failure.
    return GetCurrentThreadId()==g_visualClockThread&&g_visualClockCalls.Restore(ReadMemory,WriteVisualCall);
}
void UpdateVisualTime()
{
    const auto bits=g_visualTimeSettings.load(std::memory_order_acquire);float values[2];std::memcpy(values,&bits,sizeof bits);
    const bool requested=(std::isfinite(values[0])&&values[0]>=0&&values[0]<24)||
                         (std::isfinite(values[1])&&values[1]>0);
    if(!requested){RestoreVisualTime();g_visualDayCycle={};return;}
    if(!IsSupportedClient())return;
    const DWORD thread=GetCurrentThreadId();
    if(g_visualClockThread&&g_visualClockThread!=thread)return;
    g_visualClockThread=thread;
    // The optional visual-testing clock is process-lifetime, like standalone's
    // hooks. It is never a render hook and does not alter server/simulation time.
    if(!g_visualClockPinned){
        HMODULE pinned=nullptr;
        g_visualClockPinned=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(reinterpret_cast<uintptr_t>(&VisualDayFractionThunk)),&pinned)!=0;
        if(!g_visualClockPinned)return;
    }
    const bool ready=g_visualClockCalls.Install(ReadMemory,WriteVisualCall,reinterpret_cast<uintptr_t>(&VisualDayFractionThunk));
    if(ready&&!g_visualClockEnabled.load(std::memory_order_acquire))WATER_LOG_INFO("%s: time of day held for pictures",kVisualClockMarker);
    g_visualClockEnabled.store(ready,std::memory_order_release);
}

#else
bool RestoreVisualTime(){return true;}
// Standalone runs its own clock from its client hooks; a player cooperative core has none.
void UpdateVisualTime(){}
#endif

namespace {
// A transport's route part (GameObject+0x1a0, vtable 0xa348c0, owner at +4) stores its route time at +0x30 only
// when the route resolves to the current map (0x713576); on another map the client leaves it as it was. So a
// transport whose route time moved within the last second is on this map, read without changing the client.
constexpr uintptr_t kGameObjectTypePart=0x1a0,kTransportRouteTable=0xa348c0,kTransportRouteTime=0x30;
struct ObservedRoute {uint64_t identity=0;uint32_t time=0;DWORD moved=0;bool seen=false;};
std::array<ObservedRoute,64> g_routes={};
bool TransportOnCurrentMap(uint64_t identity,uintptr_t object,DWORD now)
{
    uint32_t route=0,table=0,owner=0,time=0;
    if(!Read(object+kGameObjectTypePart,route) || !ValidPointer(route) || !Read(route,table) || table!=kTransportRouteTable ||
       !Read(route+4,owner) || owner!=object || !Read(route+kTransportRouteTime,time))return false;
    auto slot=std::find_if(g_routes.begin(),g_routes.end(),[&](const auto& r){return r.seen&&r.identity==identity;});
    if(slot==g_routes.end()){
        slot=std::min_element(g_routes.begin(),g_routes.end(),[](const auto& a,const auto& b){return !a.seen||(b.seen&&a.moved<b.moved);});
        *slot={identity,time,0,true};
        return false;   // one sighting cannot tell a moving route from a held one
    }
    if(slot->time!=time){slot->time=time;slot->moved=now;}
    return slot->moved && now-slot->moved<1000;
}
}
int ReadMovers(const float* camera, Mover* out, int max, Boat* boats, int maxBoats, int& boatCount, ReflectionObject* reflections,unsigned* reflectionCount)
{
    if(reflectionCount)*reflectionCount=0;
    boatCount = 0;
    std::vector<SeenObject> objects;
    objects.reserve(512);
    uint64_t localGuid = 0;
    const int walked = WalkObjects(objects, localGuid);
    const DWORD now = GetTickCount();
    LogObjects(walked, objects, localGuid, camera);

    struct Candidate
    {
        float distance;
        Mover mover;
    };
    std::vector<Candidate> candidates;
    std::vector<uint64_t> seenShips;
    std::vector<uint64_t> sailingShips;
    std::vector<uint64_t> resetShips;
    static std::unordered_map<uint32_t,water::client12340::UnitModelSize> sizes;
    // Capture factual sizing once; each render context selects its comparison.
    for (const SeenObject& o : objects)
    {
        const float dx = o.position[0] - camera[0];
        const float dy = o.position[1] - camera[1];
        const float distance = std::sqrt(dx * dx + dy * dy);
        if(reflections && reflectionCount && *reflectionCount<kMaxReflectionObjects && distance<=kReflectionObjectRange &&
           o.type==kGameObjectType && (o.guid>>48)==kTransportGuidHigh && TransportOnCurrentMap(o.guid,o.object,now)){
            uint32_t fields=0;float scale=0;ReflectionObject current;current.identity=o.guid;
            // Build12340 native transport matrix and update-field display/scale.
            // Live probe verifies all four matrix rows, including pitch/roll.
            if(Read(o.object+8,fields) && ValidPointer(fields) && Read(fields+32,current.display) &&
               Read(fields+16,scale) && std::isfinite(scale) && scale>0 && scale<=100 &&
               ReadMemory(o.object+kTransportAxis,current.transform,sizeof(current.transform))){
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)current.transform[r*4+c]*=scale;
                if(ValidReflectionObject(current))reflections[(*reflectionCount)++]=current;
            }
        }
        const bool boat = o.type == kGameObjectType && (o.guid >> 48) == kTransportGuidHigh;
        if (o.type == kGameObjectType && !boat)
            continue;
        if (distance > (boat ? kBoatRange : kMoverRange))
            continue;
        const Motion motion = TrackMotion(o, now);
        const float speed = motion.speed;
        Track& track = *motion.track;
        Mover mover = {{o.position[0], o.position[1], o.position[2]}, kUnitRadius, 0.0f, false};
        if (boat)
        {
            // Transports: ships, and zeppelins, which the renderer leaves out as they are not on the water.
            seenShips.push_back(o.guid);
            if (track.historyCount > 0)
            {
                const float jx = o.position[0] - track.history[0][0];
                const float jy = o.position[1] - track.history[0][1];
                const float seconds = std::max((now - track.historyTime[0]) / 1000.0f, 0.1f);
                if (jx * jx + jy * jy > std::pow(kShipFastest * seconds + kShipJumpMargin, 2.0f))
                {
                    track.historyCount = 0;
                    track.trailCount = 0;
                    track.shipMeasured = 0;
                    track.shipSpeed = 0.0f;
                    track.slowSince = 0;
                    resetShips.push_back(o.guid);
                }
            }
            const ShipMotion ship = MeasureShip(track, o.position, now);
            if (ship.speed >= kBoatSpeed)
                track.slowSince = 0;
            else if (track.slowSince == 0)
                track.slowSince = now;
            // A ship that stops (docks) starts a new path when it sails again.
            if (track.slowSince != 0 && now - track.slowSince >= kShipStoppedMs)
                track.trailCount = 0;
            if (ship.speed < kBoatSpeed)
                continue;
            RecordTrail(track, o.position, ship.speed, now);
            // Which way the hull points: its model's facing (the way it sails, not behind it as the measured heading
            // is when it turns), else the measured heading.
            float heading[2] = {ship.heading[0], ship.heading[1]};
            if (o.facingKnown)
            {
                const float agreement = o.facing[0] * ship.heading[0] + o.facing[1] * ship.heading[1];
                if (ship.speed > kShipSignSpeed && std::fabs(agreement) > kShipSignAgreement)
                    track.facingSign = agreement > 0.0f ? 1.0f : -1.0f;
                if (track.facingSign != 0.0f)
                {
                    heading[0] = o.facing[0] * track.facingSign;
                    heading[1] = o.facing[1] * track.facingSign;
                }
            }
            if (boatCount < maxBoats)
            {
                sailingShips.push_back(o.guid);
                Boat& boat = boats[boatCount++];
                boat = {{o.position[0], o.position[1], o.position[2]}, {heading[0], heading[1]}, ship.speed,
                        {}, 0, 0, 0};
                boat.trail[0] = {o.position[0], o.position[1], 0.0f, ship.speed};
                for (int i = 0; i < track.trailCount; ++i)
                {
                    const Track::TrailPoint& point = track.trail[i];
                    boat.trail[i + 1] = {point.x, point.y, (now - point.time) / 1000.0f, point.speed};
                }
                boat.trailCount = 1 + track.trailCount;
                boat.positionReads = track.reads;
                boat.positionChanges = track.changes;
            }
            continue;
        }
        water::client12340::UnitRippleSize size;
        {
            uint32_t fields=0,display=0;float scale=1,boundary=.389f;
            if (Read(o.object+8,fields) && ValidPointer(fields)) {
                Read(fields+4*4,scale);Read(fields+0x41*4,boundary);
                if (Read(fields+0x43*4,display)) {
                    auto found=sizes.find(display);
                    if (found==sizes.end()) {
                        water::client12340::UnitModelSize measured;
                        if (water::client12340::ReadUnitModelSize(ReadMemory,display,measured)) {
                            if(sizes.size()>=256)sizes.clear();found=sizes.emplace(display,measured).first;
                        }
                    }
                    if(found!=sizes.end())size=water::client12340::RippleSize(found->second,scale);
                    else if(std::isfinite(boundary)&&boundary>0) {
                        // Server collision radii already include object scale.
                        const float r=std::clamp(boundary*.65f,.075f,3.f);size={r,std::clamp(r/.25f,.15f,3.f),r*.5f};
                    }
                }
            }
            mover.radius=size.radius;
        }
        const float vertical = std::fabs(motion.verticalSpeed);
        const bool moving = speed > kMovingSpeed;
        if (vertical > kSplashVerticalSpeed && now - track.lastSplash >= kSplashPulseMs)
        {
            mover.radius = size.radius*2;
            mover.strength = kSplashStrength * std::min(vertical / kSplashFullSpeed, 1.0f);
            track.lastSplash = now;
            track.lastPulse = now;
        }
        else if (moving)
        {
            const float wake = std::min(speed / kMaxWakeSpeed, 1.0f);
            // Ahead of the position the game gives (about the hips) in the way it moves: a swimmer's head and shoulders
            // lead, and at swimming speed its ripples started behind the body.
            const float lead = std::min(speed * .05f, size.lead);
            mover.position[0] += motion.heading[0] * lead;
            mover.position[1] += motion.heading[1] * lead;
            mover.radius = size.radius;
            mover.strength = kUnitWakeStrength * (0.4f + 0.6f * wake);
            mover.continuous = true;
        }
        else if (now - track.lastPulse >= kIdlePulseMs)
        {
            mover.strength = kIdleRingStrength;
            track.lastPulse = now;
        }
        mover.strength*=size.strength;
        if (mover.strength != 0.0f)
            candidates.push_back({distance, mover});
    }
    SettleShips(seenShips, sailingShips, resetShips, boats, maxBoats, boatCount, now);
    ForgetStaleTracks(now);
    static DWORD boatsLogged = 0;
    if (boatCount > 0 && now - boatsLogged > kObjectLogIntervalMs)
    {
        boatsLogged = now;
        for (int i = 0; i < boatCount; ++i)
            WATER_LOG_DEBUG("ship %d at (%.1f %.1f %.1f) heading (%.2f %.2f) at %.1f yd/s, %d path points; position "
                           "changed in %d of %d frames",
                           i, boats[i].position[0], boats[i].position[1], boats[i].position[2], boats[i].direction[0],
                           boats[i].direction[1], boats[i].speed, boats[i].trailCount, boats[i].positionChanges,
                           boats[i].positionReads);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    const int count = std::min(static_cast<int>(candidates.size()), max);
    for (int i = 0; i < count; ++i)
        out[i] = candidates[i].mover;
    return count;
}

void CaptureOpaqueState(IDirect3DDevice9* device)
{
    OpaqueState state = {};
    state.valid = ReadCameraState(device, false, state);
    if (state.valid && (GlobalConfig().Get().reflectionProducer == 1 ||
                        GlobalConfig().Get().reflectionProducer == 3)) ReadMapId(state.mapId);
    CompareCameraGlobals(state);
    g_opaque = state;
}

bool HasOpaqueState()
{
    return g_opaque.valid;
}

void ClearOpaqueState()
{
    g_opaque.valid = false;
}

bool FillFrameInputs(const OpaqueState& camera, FrameInputs& out);

namespace
{
GroundMap g_ground;
GroundMap g_farGround(2.4f, true);
DWORD g_lavaDrawnAt = 0;
bool g_lavaDrawn = false;
constexpr DWORD kFarGroundKeptMs = 5000;
constexpr double kFarGroundBudgetSeconds = 0.0005;

using WorldIntersectFn = unsigned char(__cdecl*)(float* start, float* end, float* hit, float* fraction,
                                                 uint32_t flags, uint32_t unused);

bool HeightOf(uint32_t what, float x, float y, float fromZ, float toZ, float& height)
{
    float start[3] = {x, y, fromZ};
    float end[3] = {x, y, toZ};
    float hit[3] = {};
    float fraction = 1.0f;
    if (!reinterpret_cast<WorldIntersectFn>(kWorldIntersect)(start, end, hit, &fraction, what, 0))
        return false;
    height = hit[2];
    return true;
}

bool GroundHeight(float x, float y, float fromZ, float toZ, float& height)
{
    return HeightOf(kIntersectTerrainOnly, x, y, fromZ, toZ, height);
}

bool LiquidHeight(float x, float y, float fromZ, float toZ, float& height)
{
    return HeightOf(kIntersectLiquidOnly, x, y, fromZ, toZ, height);
}

// Liquid that is not water. Asked only where there is liquid.
bool HotLiquid(float x, float y, float fromZ, float toZ, float& height)
{
    return !HeightOf(kIntersectWaterOnly, x, y, fromZ, toZ, height);
}
}

int ReadRainSplashes(const float* camera, Splash* out, int max, float& reach)
{
    reach = 0.0f;
    uint32_t clock = 0;
    if (!Read(kGameClock, clock))
        return 0;
    const double now = clock * static_cast<double>(kGameClockSecondsPerTick);
    // Each splash is remembered from when it lands until its ring has spread out: the game throws a packet away with
    // all its splashes when the packet's time is up, which would end their rings early, all at once.
    struct Landed
    {
        float position[3];
        double when;
    };
    static std::unordered_map<uint64_t, Landed> landed;
    static double lastNow = 0.0;
    if (now < lastNow)
        landed.clear();
    lastNow = now;
    static std::vector<RainSplash> splashes;
    uint32_t weather = 0;
    uint32_t rain = 0;
    uint32_t packet = 0;
    if (!Read(kWeather, weather) || !weather || !Read(weather + kWeatherRainObject, rain) || !rain ||
        !Read(rain + kRainPackets, packet))
        packet = 0;
    int packets = 0;
    uint32_t total = 0;
    int seen = 0;
    for (; packet && !(packet & 1) && packets < kMaxRainPackets; ++packets)
    {
        uint32_t count = 0;
        uint32_t start = 0;
        uint32_t next = 0;
        if (!Read(packet + kPacketSplashCount, count) || !Read(packet + kPacketStart, start) ||
            !Read(packet + kPacketNext, next) || count > kPacketMaxSplashes)
            break;
        splashes.resize(count);
        if (clock > start && count &&
            ReadMemory(packet + kPacketSplashes, splashes.data(), count * sizeof(RainSplash)))
        {
            total += count;
            const float elapsed = (clock - start) * kGameClockSecondsPerTick;
            for (const RainSplash& s : splashes)
            {
                const float dx = s.position[0] - camera[0];
                const float dy = s.position[1] - camera[1];
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (!std::isfinite(distance) || distance > kSplashReach)
                    continue;
                reach = std::max(reach, distance);
                const float age = elapsed - s.shows;
                if (age < 0.0f || age >= kSplashRingSeconds)
                    continue;
                ++seen;
                uint32_t x = 0;
                uint32_t y = 0;
                std::memcpy(&x, &s.position[0], sizeof(x));
                std::memcpy(&y, &s.position[1], sizeof(y));
                landed.emplace((static_cast<uint64_t>(x) << 32) | y,
                               Landed{{s.position[0], s.position[1], s.position[2]}, now - age});
            }
        }
        packet = next;
    }

    struct Candidate
    {
        float distance;
        Splash splash;
    };
    static std::vector<Candidate> candidates;
    candidates.clear();
    for (auto it = landed.begin(); it != landed.end();)
    {
        const Landed& l = it->second;
        const float age = static_cast<float>(now - l.when);
        if (age >= kSplashRingSeconds)
        {
            it = landed.erase(it);
            continue;
        }
        const float dx = l.position[0] - camera[0];
        const float dy = l.position[1] - camera[1];
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= kSplashReach)
            candidates.push_back({distance, {l.position[0], l.position[1], l.position[2], age}});
        ++it;
    }
    if (reach <= 0.0f && !candidates.empty())
        reach = kSplashReach;
    const int kept = std::min(static_cast<int>(candidates.size()), max);
    std::partial_sort(candidates.begin(), candidates.begin() + kept, candidates.end(),
                      [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    for (int i = 0; i < kept; ++i)
        out[i] = candidates[i].splash;
    static DWORD lastLogged = 0;
    if (GetTickCount() - lastLogged > kSplashLogIntervalMs)
    {
        lastLogged = GetTickCount();
        WATER_LOG_DEBUG("rain splashes: %d packets holding %u, %d in them landed in the last %.1f s within %.0f yd (placed "
                       "out to %.1f yd), %zu remembered, %d kept; nearest (%.2f %.2f %.2f) %.2f s ago; camera (%.2f "
                       "%.2f %.2f)",
                       packets, total, seen, kSplashRingSeconds, kSplashReach, reach, landed.size(), kept,
                       kept ? out[0].x : 0.0f, kept ? out[0].y : 0.0f, kept ? out[0].z : 0.0f,
                       kept ? out[0].age : 0.0f, camera[0], camera[1], camera[2]);
    }
    return kept;
}

void NoteLavaDrawn()
{
    g_lavaDrawnAt = GetTickCount();
    g_lavaDrawn = true;
}

void UpdateGround(double budgetSeconds)
{
    UpdateGround(budgetSeconds, GlobalConfig().Get().groundPreparationWorker);
}

void UpdateGround(double budgetSeconds, bool worker)
{
    float camera[3];
    if (!ReadMemory(kCameraPosition, camera, sizeof(camera)) || !water::client12340::AllFinite(camera, 3))
        return;
    LARGE_INTEGER before, after, frequency;
    QueryPerformanceCounter(&before);
    const int filledBefore = g_ground.Filled();
    g_ground.Update(camera, &GroundHeight, &LiquidHeight, budgetSeconds, worker, &HotLiquid);
    if (g_lavaDrawn && GetTickCount() - g_lavaDrawnAt < kFarGroundKeptMs)
        g_farGround.Update(camera, &GroundHeight, &LiquidHeight, kFarGroundBudgetSeconds, worker, &HotLiquid);
    QueryPerformanceCounter(&after);
    QueryPerformanceFrequency(&frequency);
    static DWORD lastLogged = 0;
    const int looked = g_ground.Filled() - filledBefore;
    if (looked > 0 && GetTickCount() - lastLogged > kGroundLogIntervalMs)
    {
        lastLogged = GetTickCount();
        WATER_LOG_DEBUG("ground map: %d heights looked up in %.2f ms this frame, %d of %d known around (%.0f %.0f)",
                       looked, 1000.0 * (after.QuadPart - before.QuadPart) / frequency.QuadPart, g_ground.Filled(),
                       GroundMap::kCells * GroundMap::kCells, camera[0], camera[1]);
    }
}


bool BuildFrameInputsFromGlobals(IDirect3DDevice9* device, FrameInputs& out)
{
    OpaqueState state = {};
    return ReadCameraState(device, true, state) && FillFrameInputs(state, out);
}

bool BuildFrameInputsFromRenderState(IDirect3DDevice9* device, FrameInputs& out)
{
    OpaqueState state = {};
    return ReadCameraState(device, false, state) && FillFrameInputs(state, out);
}

bool BuildFrameInputs(FrameInputs& out)
{
    return g_opaque.valid && FillFrameInputs(g_opaque, out);
}

bool BuildFrameInputsFromCamera(const float* view, const float* projection, const D3DVIEWPORT9& viewport,
                               FrameInputs& out)
{
    if (!view || !projection || !water::client12340::AllFinite(view, 16) || !water::client12340::AllFinite(projection, 16) || !water::client12340::ClientPerspective(projection) ||
        !viewport.Width || !viewport.Height || !std::isfinite(viewport.MinZ) || !std::isfinite(viewport.MaxZ) ||
        viewport.MaxZ <= viewport.MinZ)
        return false;
    OpaqueState camera = {};
    std::copy(view, view + 16, camera.cameraRelativeView);
    std::copy(projection, projection + 16, camera.glProjection);
    camera.viewport = viewport;
    ReadMapId(camera.mapId); // capture_client: one world identity read on the game thread
    return FillFrameInputs(camera, out);
}

bool FillFrameInputs(const OpaqueState& camera, FrameInputs& out)
{
    out.nativeLiquids=g_liquidIdentity;
    out.mapId = camera.mapId;
    out.modelMilliseconds=GetTickCount();
    std::memcpy(out.cameraRelativeView, camera.cameraRelativeView, sizeof(out.cameraRelativeView));
    std::memcpy(out.glProjection, camera.glProjection, sizeof(out.glProjection));
    out.viewport = camera.viewport;
    out.cameraInWater = CameraInWater();
    out.cameraInSlime = CameraInSlime();
    out.cameraWaterHeightKnown = CameraWaterHeight(out.cameraWaterHeight);
    out.stockFogPushed = g_stockFog.Active();
    out.ground = g_ground.Valid() ? &g_ground : nullptr;
    out.farGround = g_lavaDrawn && GetTickCount() - g_lavaDrawnAt < kFarGroundKeptMs && g_farGround.Valid() ? &g_farGround : nullptr;
    return ReadMemory(kCameraPosition, out.cameraPosition, sizeof(out.cameraPosition)) &&
           water::client12340::AllFinite(out.cameraPosition, 3) && ReadLight(out);
}

bool ReadMapId(int32_t& map) { return Read(kCurrentMap,map); }

}
