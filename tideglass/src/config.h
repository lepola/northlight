#pragma once

#include "reflection_distance.h"
#include <string>
#include <vector>
#include <memory>

enum class DebugView
{
    Off = 0,
    WorldGrid = 1,
    Depth = 2,
};

struct LiquidStyle
{
    float shallowColor[3];
    float deepColor[3];
    float gradientDepth;
    float clarity;
    float waveStrength;
    float waveSpeed;
    float reflection;
    float stockTint;
    float foamStrength;
    float foamColor[3];
    float foamDepth;
    float foamGlow;
    float stockRelief;
    float surfaceTransmission;
    float insideBrightness;
    float rippleResponse;
    float abyssColor[3];
    float abyssDepth;
    float nearReflection;
    float surfaceDeepness;
    float waveCrestShape = 0.0f;
    float waveCrestSharpness = 0.0f;
};

struct Config
{
    int profileRequest = 0, profileMode = 1, profileSeconds = 300, profileFrames = 60000;
    int profileCalls = 0; // 0 off, 1 outer calls, 2 nested spans; opt-in clocks
    bool profileBypass = false; // diagnostic control; effective only during an active bounded capture
    bool enable = true;
    bool engineHooks = true;
    bool surface = true;
    int waterAtmosphere = 0; // private comparison:0 legacy,1 water distance,2 host distance with the same material inputs
    bool waterHorizonBlend = true;
    bool waterGameFog = false; // with WaterAtmosphere=2: the game's own distance fog over the whole water colour
    // The wave moments the filtered material reads (only where the host supplies its environment): samples per mid/far
    // water pixel, 16, 32 or 64 (the measured reference).
    int waterWaveMomentSamples = 64;
    float waveCrestShape = 0.0f;
    float waveCrestSharpness = 0.0f;
    float waveStrength = 1.0f;
    float waveSpeed = 1.0f;
    float foamStrength = 1.0f;
    float clarity = 1.0f;
    float reflection = 0.7f;
    bool screenReflections = true;
    float nearReflection = 0.55f;
    float reflectionRamp = 30.0f;
    float scatter = 0.6f;
    float specular = 1.0f;
    float stockTint = 0.0f;
    float shallowColor[3] = {0.10f, 0.48f, 0.46f};
    float deepColor[3] = {0.02f, 0.13f, 0.24f};
    float gradientDepth = 6.0f;
    float abyssColor[3] = {0.01f, 0.05f, 0.12f};
    float abyssDepth = 25.0f;
    float twilightShallowColor[3] = {0.08f, 0.45f, 0.30f};
    float overcastWater = 0.75f;
    float surfaceDeepness = 1.0f;
    float surfaceBlur = 1.0f;
    float surfaceVeil = 0.15f;
    // How much foam open water gathers (the dapple and the chosen swells' crests; see OpenWaterFoam), 1 as designed.
    float openWaterCrestFoam = 1.0f;
    float openWaterFoam = 1.0f;
    bool antiAliasing = true;
    float ambientOcclusion = 1.0f;
    float ambientOcclusionRadius = 1.5f;
    float ambientOcclusionDistance = 120.0f;
    float ambientOcclusionColor[3] = {0.34f, 0.39f, 0.43f};
    float puddles = 1.0f;
    float testRain = -1.0f;
    float puddleFillSeconds = 90.0f;
    float puddleDrySeconds = 240.0f;
    float rainRings = 0.5f;
    float rainRingStrength = 1.0f;
    bool ripples = true;
    float rippleStrength = 1.6f;
    float shoreWaves = 1.0f;
    bool slime = true;
    // Draw the client's magma as the lava material (shaders/lava.hlsli), not the stock surface.
    bool lava = true;
    LiquidStyle slimeStyle = {{0.42f, 0.70f, 0.12f}, {0.10f, 0.28f, 0.03f}, 2.5f, 0.1f, 0.45f, 0.1f, 0.9f, 0.55f,
                              1.0f, {0.55f, 1.0f, 0.2f}, 0.35f, 0.6f, 0.0f, 0.6f, 1.0f, 0.8f,
                              {0.10f, 0.28f, 0.03f}, 1000.0f, 0.9f, 1.0f};
    float slimeRippleSpeed = 0.6f; // yards a second a ripple spreads; water's is 2.8
    float slimeVisibility = 6.0f;
    float slimeInsideColor[3] = {0.22f, 0.44f, 0.12f};
    float slimeInsideDeepColor[3] = {0.11f, 0.23f, 0.06f};
    float slimeWindow = 1.0f;
    float slimeGlancingMirror = 0.2f;
    bool underwater = true;
    bool underwaterScenery = false;
    float underwaterVisibility = 35.0f;
    float underwaterWindow = 0.7f;
    int underwaterDebug = 0;
    int reflectionDebug = 0;
    int refractionDebug = 0;
    bool underwaterDaylight = true;
    bool stockLiquidEffects = true;
    float underwaterGlancingMirror = 1.0f;
    DebugView debugView = DebugView::Off;
    float gridSpacing = 4.0f;
    float overlayOpacity = 0.6f;
    int captureFrame = 0;
    // How many frames in a row a capture saves (frame-<number>-<i>.bmp when more than one).
    int captureFrames = 1;
    // Development: how often (seconds) the GPU and CPU timings are logged.
    int timingSeconds = 60;
    int reflectionSceneParts = 31; // diagnostic: owned-scene parts drawn (1 terrain, 2 models, 4 horizon, 8 live, 16 animation)
    // Trials default off until the above/below-water and dry-land transitions are accepted in game.
    bool sharedUnderwaterFoam = false;
    bool skipEmptyWater = false;
    // Diagnostics, also exact references for tests: the reflection looked up for a flat surface, and the water showing
    // its reflection alone.
    bool reflectionSurfaceFlat = false;
    int reflectionView = 0; // 0 the water; 1 its reflection alone, 2 the mirrored image's colour, 3 its coverage, 4 the directional horizon
    int reflectionOwnedLighting = 1; // 0 legacy celestial direction, 1 native material, 2 unlit, 3 ambient, 4 direct
    int reflectionProducer = 0; // 0 a view the host supplies (standalone has none and uses 3), 1 owned fixture, 2 sky, 3 owned world
    std::shared_ptr<const std::wstring> reflectionOwnedArchives; // immutable UTF-8 source-list path from settings
    // Private cache controls; source import/full quality remain the defaults.
    int reflectionOwnedTextureCache=0,reflectionOwnedTextureCacheMiB=256,reflectionOwnedTextureQuality=0;
    std::shared_ptr<const std::wstring> reflectionOwnedTextureCacheDirectory;
    int reflectionOwnedDelayMs = 0;
    bool reflectionOwnedFail = false;
    bool reflectionOwnedFailDraw = false;
    int reflectionOwnedUploadBytes = 1048576;
    float waterSceneExposure = 1.f;
    float reflectionBendReach = 3.0f; // yards of reflected ray over which the waves' bend of the lookup is measured
    float reflectionWarp = 0.0125f; // how far the waves may shift the reflection lookup, a fraction of the view's width
    // Opt-in exact pass inputs/output packets, bounded to eight consecutive frames.
    int reflectionCapture = 0;
    int reflectionCaptureFrames = 1;
    bool reflectionCaptureBurst = false; // exactly two baseline frames; deferred native readback
    bool waterWorkAudit = false; // optional sky/underwater GPU intervals
    bool groundPreparationWorker = false; // owned same-frame shore/nearest-water preparation
    bool reflectionSceneryShoreEdge = false; // exclude dry-land taps from extra-view boundary coverage
    bool reflectionSceneryAlign = false; // align extra geometry to D3D9 integer raster centres
    unsigned reflectionResolution = 100;
    water::ReflectionDistance reflectionDistance = water::ReflectionDistance::Standard;
    bool reflectionScenery = false; // continuous extra geometry, composed through ordinary water shading
    float timeOfDay = -1.0f;
    float dayCycleSeconds = 0.0f;
    int traceLiquids = 0;
    int seabedDebug = 0;
    bool testShip = false;
    // The offline harness holds the liquids' clock here (seconds) so frames can be compared; below 0, real time.
    float testSeconds = -1.0f;
    int logLevel = 1;
};

class ConfigFile
{
public:
    void Load(const std::string& path);
    // Cooperative hosts keep their hooks installed and may return water ownership live.
    // The standalone proxy still treats Enable as a startup setting.
    void ReloadIfChanged(bool reloadEnable = false);
    void Override(const Config& config) { m_config = config; }
    // With the stock view on (a key in the game, for comparing), everything the renderer adds reads as off.
    const Config& Get() const { return m_stockView ? StockView() : m_config; }
    void SetStockView(bool stock) { m_stockView = stock; }
    bool StockViewOn() const { return m_stockView; }
    static LiquidStyle WaterStyle(const Config& config);
    std::string Directory() const;

private:
    bool Parse();

    std::string m_path;
    unsigned long long m_writeTime = 0;
    const Config& StockView() const;

    Config m_config;
    mutable Config m_stockConfig;
    std::vector<std::string> m_invalidKeys;
    bool m_stockView = false;
};

ConfigFile& GlobalConfig();
