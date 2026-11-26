#pragma once
// The running client (3.3.5a, build 12340): the code addresses Tideglass hooks or reads, and the functions that sample
// game state on the game thread. Every address belongs to that one executable; IsSupportedClient() guards their use.

#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include "frame/frame_inputs.h"

namespace engine
{
constexpr uint32_t kClientTimestamp = 0x4C2452FE;

constexpr uintptr_t kCameraLiquidReadySite = 0x0079A888;
constexpr uintptr_t kCameraLiquidUpdateTarget = 0x00790920;
constexpr uintptr_t kWorldRenderSite = 0x004FB03D;
constexpr uintptr_t kWorldRenderTarget = 0x004F8EA0;
constexpr uintptr_t kOpaqueM2PassSite = 0x004F911D;
constexpr uintptr_t kOpaqueM2PassTarget = 0x00823CB0;
constexpr uintptr_t kLiquidSurfaceSite = 0x004F9170;
// With the camera under water the client draws transparent models first and the liquid surface last, from here.
constexpr uintptr_t kLiquidSurfaceUnderwaterSite = 0x004F91B0;
constexpr uintptr_t kLiquidSurfaceTarget = 0x0077F020;
constexpr uintptr_t kScreenEffectsSite = 0x004F9281;
constexpr uintptr_t kDayFractionSite = 0x007E2752;
constexpr uintptr_t kFrameDayFractionSite = 0x004F859C;
constexpr uintptr_t kDayFractionTarget = 0x0076CFF0;
constexpr uintptr_t kScreenEffectsTarget = 0x008C1010;
// The day-night light update asks which liquid the camera is in and switches to the liquid's underwater light
// (sky, sun, ambient and direct colours) when there is one.
constexpr uintptr_t kDayNightLiquidSite = 0x007F3245;
// The day-night fog update asks the same, and in a liquid uses the liquid's fog (which also paints the sky).
constexpr uintptr_t kDayNightFogLiquidSite = 0x007F17CE;
// The sky pass reads the camera liquid directly ("cmp [0xCD8794], ebx"): in a liquid it skips the sky model and
// clouds and clears to a plain colour instead.
constexpr uintptr_t kSkyClearLiquidRead = 0x0079AB74;
constexpr uintptr_t kSkyModelLiquidRead = 0x0079ACAF;
constexpr uintptr_t kCameraLiquidGlobal = 0x00CD8794;
constexpr uintptr_t kNativeRipplesSwitch = 0x00ADF7F0;
constexpr uintptr_t kCameraLiquidTarget = 0x00780620;

bool IsSupportedClient();
// Cooperative visual testing uses the same native display clock as the host.
void ConfigureVisualTime(float hour,float cycleSeconds);
void UpdateVisualTime(); // game capture thread only
bool RestoreVisualTime(); // after capture stops, before module unload

void* GameD3DDevice();
bool CameraInWater();
bool CameraInSlime();
// Game thread only. Metadata is copied with queued frames; replay never reads
// native game state. A mapper translates exposed texture identities to backend
// identities without invoking or retaining the captured numeric addresses.
using LiquidTextureMap=uintptr_t(__cdecl*)(void*,uintptr_t);
void CaptureLiquidIdentity(LiquidTextureMap map=nullptr,void* context=nullptr);
const NativeLiquidFrame& LiquidIdentity();
uint32_t CameraLiquidType();
bool CameraWaterHeight(float& height);
#if !defined(WATER_COOPERATIVE_BUILD)
// Standalone only; the cooperative core never writes to the client.
void PushStockFogOutOfRange();
void RestoreStockFog();
// The game's own wake switch (its console's waterRipples), giving what it held.
int SwapNativeRipples(int value);
#endif

void CaptureOpaqueState(IDirect3DDevice9* device);
bool HasOpaqueState();
void ClearOpaqueState();
bool ReadMapId(int32_t& map);
bool BuildFrameInputs(FrameInputs& out);
// Read current world camera globals without device calls or replacing opaque state.
bool BuildFrameInputsFromGlobals(IDirect3DDevice9* device, FrameInputs& out);
// Read this draw stage's camera without replacing the final opaque-pass camera.
bool BuildFrameInputsFromRenderState(IDirect3DDevice9* device, FrameInputs& out);
int ReadMovers(const float* camera, Mover* out, int max, Boat* boats, int maxBoats, int& boatCount, ReflectionObject* reflections=nullptr,unsigned* reflectionCount=nullptr);
// The raindrop splashes the game is drawing, nearest the camera first (see FrameInputs::splashes); how many.
int ReadRainSplashes(const float* camera, Splash* out, int max, float& reach);
// Looks up more of the ground height around the camera (see GroundMap), within the time budget.
void UpdateGround(double budgetSeconds);
void UpdateGround(double budgetSeconds, bool worker);
// Lava is being drawn: the far ground map is kept up while it has been lately, and costs nothing otherwise.
void NoteLavaDrawn();
bool BuildFrameInputsFromCamera(const float* view, const float* projection, const D3DVIEWPORT9& viewport,
                               FrameInputs& out);
}
