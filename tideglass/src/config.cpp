#include "config.h"
#include "config_reader.h"

#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>

namespace
{
constexpr float kMinGridSpacing = 0.5f;
constexpr float kMaxGridSpacing = 100.0f;

unsigned long long LastWriteTime(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           data.ftLastWriteTime.dwLowDateTime;
}

}

void ConfigFile::Load(const std::string& path)
{
    m_path = path;
    m_writeTime = LastWriteTime(path);
    Parse();
}

void ConfigFile::ReloadIfChanged(bool reloadEnable)
{
    unsigned long long writeTime = LastWriteTime(m_path);
    if (writeTime == m_writeTime)
        return;
    m_writeTime = writeTime;
    Config before = m_config;
    if (!Parse()) return;
    if (!reloadEnable) m_config.enable = before.enable;
    m_config.engineHooks = before.engineHooks;
    WATER_LOG_INFO("settings reloaded: Surface %d, WaveStrength %.2f, FoamStrength %.2f, Clarity %.2f, "
                   "Reflection %.2f, Specular %.2f, StockTint %.2f, DebugView %d",
                   m_config.surface, m_config.waveStrength, m_config.foamStrength, m_config.clarity,
                   m_config.reflection, m_config.specular, m_config.stockTint, static_cast<int>(m_config.debugView));
}

const Config& ConfigFile::StockView() const
{
    Config& stock = m_stockConfig;
    stock = m_config;
    stock.surface = false;
    stock.ripples = false;
    stock.underwater = false;
    stock.slime = false;
    stock.lava = false;
    stock.antiAliasing = false;
    stock.ambientOcclusion = 0.0f;
    stock.puddles = 0.0f;
    stock.underwaterDaylight = false;
    stock.stockLiquidEffects = true;
    stock.debugView = DebugView::Off;
    stock.seabedDebug = 0;
    stock.testShip = false;
    stock.underwaterDebug = 0;
    stock.reflectionDebug = 0;
    stock.refractionDebug = 0;
    return stock;
}

bool ConfigFile::Parse()
{
    const Config defaults;
    Config next = m_config;
    ConfigReader reader(m_path);
    next.enable = reader.Int("Enable", defaults.enable) != 0;
    next.engineHooks = reader.Int("EngineHooks", defaults.engineHooks) != 0;
    next.surface = reader.Int("Surface", defaults.surface) != 0;
    next.waterAtmosphere=std::clamp(reader.Int("WaterAtmosphere",defaults.waterAtmosphere),0,2);
    next.waterHorizonBlend=reader.Int("WaterHorizonBlend",defaults.waterHorizonBlend)!=0;
    next.waterGameFog=reader.Int("WaterGameFog",defaults.waterGameFog)!=0;
    {const int samples=reader.Int("WaterWaveMomentSamples",defaults.waterWaveMomentSamples);next.waterWaveMomentSamples=samples<24?16:samples<48?32:64;}
    next.waveCrestShape = std::clamp(reader.Float("WaveCrestShape", defaults.waveCrestShape), 0.f, 1.f);
    next.waveCrestSharpness = std::clamp(reader.Float("WaveCrestSharpness", defaults.waveCrestSharpness), 0.f, 1.f);
    next.waveStrength = std::clamp(reader.Float("WaveStrength", defaults.waveStrength), 0.0f, 4.0f);
    next.waveSpeed = std::clamp(reader.Float("WaveSpeed", defaults.waveSpeed), 0.0f, 10.0f);
    reader.Color("ShallowColor", defaults.shallowColor, next.shallowColor);
    reader.Color("DeepColor", defaults.deepColor, next.deepColor);
    next.gradientDepth = std::clamp(reader.Float("GradientDepth", defaults.gradientDepth), 0.5f, 100.0f);
    reader.Color("AbyssColor", defaults.abyssColor, next.abyssColor);
    reader.Color("TwilightShallowColor", defaults.twilightShallowColor, next.twilightShallowColor);
    next.abyssDepth = std::clamp(reader.Float("AbyssDepth", defaults.abyssDepth), 1.0f, 1000.0f);
    next.foamStrength = std::clamp(reader.Float("FoamStrength", defaults.foamStrength), 0.0f, 4.0f);
    next.clarity = std::clamp(reader.Float("Clarity", defaults.clarity), 0.05f, 10.0f);
    next.reflection = std::clamp(reader.Float("Reflection", defaults.reflection), 0.0f, 4.0f);
    next.screenReflections = reader.Int("ScreenReflections", defaults.screenReflections) != 0;
    next.nearReflection = std::clamp(reader.Float("NearReflection", defaults.nearReflection), 0.0f, 1.0f);
    next.reflectionRamp = std::clamp(reader.Float("ReflectionRamp", defaults.reflectionRamp), 1.0f, 500.0f);
    next.scatter = std::clamp(reader.Float("Scatter", defaults.scatter), 0.0f, 4.0f);
    next.reflectionSceneryDetails = reader.Int("ReflectionSceneryDetails",defaults.reflectionSceneryDetails) != 0;
    next.fineHighlights = reader.Int("FineHighlights",defaults.fineHighlights) != 0;
    next.specular = std::clamp(reader.Float("Specular", defaults.specular), 0.0f, 4.0f);
    next.stockTint = std::clamp(reader.Float("StockTint", defaults.stockTint), 0.0f, 1.0f);
    next.antiAliasing = reader.Int("AntiAliasing", defaults.antiAliasing) != 0;
    next.ambientOcclusion =
        std::clamp(reader.Float("AmbientOcclusion", defaults.ambientOcclusion), 0.0f, 4.0f);
    next.ambientOcclusionRadius =
        std::clamp(reader.Float("AmbientOcclusionRadius", defaults.ambientOcclusionRadius), 0.25f, 8.0f);
    next.ambientOcclusionDistance =
        std::clamp(reader.Float("AmbientOcclusionDistance", defaults.ambientOcclusionDistance), 10.0f, 1000.0f);
    reader.Color("AmbientOcclusionColor", defaults.ambientOcclusionColor, next.ambientOcclusionColor);
    next.puddles = std::clamp(reader.Float("Puddles", defaults.puddles), 0.0f, 2.0f);
    next.testRain = std::clamp(reader.Float("TestRain", defaults.testRain), -1.0f, 1.0f);
    next.puddleFillSeconds =
        std::clamp(reader.Float("PuddleFillSeconds", defaults.puddleFillSeconds), 1.0f, 3600.0f);
    next.puddleDrySeconds =
        std::clamp(reader.Float("PuddleDrySeconds", defaults.puddleDrySeconds), 1.0f, 3600.0f);
    next.rainRings = std::clamp(reader.Float("RainRings", defaults.rainRings), 0.0f, 1.0f);
    next.rainRingStrength =
        std::clamp(reader.Float("RainRingStrength", defaults.rainRingStrength), 0.0f, 10.0f);
    next.ripples = reader.Int("Ripples", defaults.ripples) != 0;
    next.shoreWaves = std::clamp(reader.Float("ShoreWaves", defaults.shoreWaves), 0.0f, 5.0f);
    next.rippleStrength = std::clamp(reader.Float("RippleStrength", defaults.rippleStrength), 0.0f, 5.0f);
    next.slime = reader.Int("Slime", defaults.slime) != 0;
    next.lava = reader.Int("Lava", defaults.lava) != 0;
    LiquidStyle& slime = next.slimeStyle;
    const LiquidStyle& slimeDefaults = defaults.slimeStyle;
    reader.Color("SlimeShallowColor", slimeDefaults.shallowColor, slime.shallowColor);
    reader.Color("SlimeDeepColor", slimeDefaults.deepColor, slime.deepColor);
    reader.Color("SlimeFoamColor", slimeDefaults.foamColor, slime.foamColor);
    reader.Color("SlimeInsideColor", defaults.slimeInsideColor, next.slimeInsideColor);
    reader.Color("SlimeInsideDeepColor", defaults.slimeInsideDeepColor, next.slimeInsideDeepColor);
    next.slimeWindow = std::clamp(reader.Float("SlimeWindow", defaults.slimeWindow), 0.0f, 1.0f);
    next.slimeGlancingMirror =
        std::clamp(reader.Float("SlimeGlancingMirror", defaults.slimeGlancingMirror), 0.0f, 1.0f);
    slime.gradientDepth = std::clamp(reader.Float("SlimeGradientDepth", slimeDefaults.gradientDepth), 0.5f, 100.0f);
    slime.clarity = std::clamp(reader.Float("SlimeClarity", slimeDefaults.clarity), 0.05f, 10.0f);
    slime.waveStrength = std::clamp(reader.Float("SlimeWaveStrength", slimeDefaults.waveStrength), 0.0f, 4.0f);
    slime.waveSpeed = std::clamp(reader.Float("SlimeWaveSpeed", slimeDefaults.waveSpeed), 0.0f, 10.0f);
    slime.reflection = std::clamp(reader.Float("SlimeReflection", slimeDefaults.reflection), 0.0f, 4.0f);
    slime.stockTint = std::clamp(reader.Float("SlimeStockTint", slimeDefaults.stockTint), 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i)
        slime.abyssColor[i] = slime.deepColor[i];
    slime.foamStrength = std::clamp(reader.Float("SlimeFoamStrength", slimeDefaults.foamStrength), 0.0f, 4.0f);
    slime.foamDepth = std::clamp(reader.Float("SlimeEdgeWidth", slimeDefaults.foamDepth), 0.05f, 5.0f);
    slime.foamGlow = std::clamp(reader.Float("SlimeEdgeGlow", slimeDefaults.foamGlow), 0.0f, 4.0f);
    slime.stockRelief = std::clamp(reader.Float("SlimeFlowRelief", slimeDefaults.stockRelief), 0.0f, 4.0f);
    slime.rippleResponse = std::clamp(reader.Float("SlimeRipples", slimeDefaults.rippleResponse), 0.0f, 4.0f);
    next.slimeRegions = reader.SlimeRegions();
    next.regionalSlimeStrength = std::clamp(reader.Float("RegionalSlimeStrength", defaults.regionalSlimeStrength), 0.0f, 1.0f);
    next.slimeRippleSpeed = std::clamp(reader.Float("SlimeRippleSpeed", defaults.slimeRippleSpeed), 0.1f, 4.0f);
    slime.nearReflection =
        std::clamp(reader.Float("SlimeNearReflection", slimeDefaults.nearReflection), 0.0f, 1.0f);
    slime.insideBrightness =
        std::clamp(reader.Float("SlimeInsideBrightness", slimeDefaults.insideBrightness), 0.05f, 2.0f);
    slime.surfaceTransmission =
        std::clamp(reader.Float("SlimeSeeThrough", slimeDefaults.surfaceTransmission), 0.0f, 1.0f);
    next.slimeVisibility = std::clamp(reader.Float("SlimeVisibility", defaults.slimeVisibility), 1.0f, 100.0f);
    next.underwater = reader.Int("Underwater", defaults.underwater) != 0;
    next.underwaterScenery = reader.Int("UnderwaterScenery",0)!=0;
    next.underwaterVisibility =
        std::clamp(reader.Float("UnderwaterVisibility", defaults.underwaterVisibility), 2.0f, 500.0f);
    next.underwaterWindow =
        std::clamp(reader.Float("UnderwaterWindow", defaults.underwaterWindow), 0.0f, 1.0f);
    next.underwaterDebug = std::clamp(reader.Int("UnderwaterDebug", defaults.underwaterDebug), 0, 2);
    next.reflectionDebug = std::clamp(reader.Int("ReflectionDebug", defaults.reflectionDebug), 0, 5);
    next.refractionDebug = std::clamp(reader.Int("RefractionDebug", defaults.refractionDebug), 0, 6);
    next.underwaterDaylight = reader.Int("UnderwaterDaylight", defaults.underwaterDaylight) != 0;
    next.surfaceDeepness = std::clamp(reader.Float("SurfaceDeepness", defaults.surfaceDeepness), 0.0f, 1.0f);
    next.overcastWater = std::clamp(reader.Float("OvercastWater", defaults.overcastWater), 0.0f, 1.0f);
    next.surfaceBlur = std::clamp(reader.Float("SurfaceBlur", defaults.surfaceBlur), 0.0f, 4.0f);
    next.surfaceVeil = std::clamp(reader.Float("SurfaceVeil", defaults.surfaceVeil), 0.0f, 1.0f);
    next.testSeconds=std::clamp(reader.Float("AnimationSeconds",defaults.testSeconds),-1.f,86400.f);
    next.openWaterCrestFoam=std::clamp(reader.Float("OpenWaterCrestFoam",defaults.openWaterCrestFoam),0.f,1.f);
    next.openWaterFoam = std::clamp(reader.Float("OpenWaterFoam", defaults.openWaterFoam), 0.0f, 2.0f);
    next.stockLiquidEffects = reader.Int("StockLiquidEffects", defaults.stockLiquidEffects) != 0;
    next.underwaterGlancingMirror =
        std::clamp(reader.Float("UnderwaterGlancingMirror", defaults.underwaterGlancingMirror), 0.0f, 1.0f);
    int view = reader.Int("DebugView", static_cast<int>(defaults.debugView));
    next.debugView = view >= 0 && view <= static_cast<int>(DebugView::Depth) ? static_cast<DebugView>(view)
                                                                                : DebugView::Off;
    next.gridSpacing =
        std::clamp(reader.Float("GridSpacing", defaults.gridSpacing), kMinGridSpacing, kMaxGridSpacing);
    next.overlayOpacity = std::clamp(reader.Float("OverlayOpacity", defaults.overlayOpacity), 0.0f, 1.0f);
    next.captureFrame = reader.Int("CaptureFrame", defaults.captureFrame);
    next.captureFrames = std::clamp(reader.Int("CaptureFrames", defaults.captureFrames), 1, 180);
    next.profileRequest = std::max(0, reader.Int("ProfileRequest", 0));
    next.profileMode = std::clamp(reader.Int("ProfileMode", 1), 0, 3);
    next.profileSeconds = std::clamp(reader.Int("ProfileSeconds", 300), 1, 600);
    next.profileFrames = std::clamp(reader.Int("ProfileFrames", 60000), 16, 60000);
    next.profileCalls = std::clamp(reader.Int("ProfileCalls", 0), 0, 2);
    next.profileBypass = reader.Int("ProfileBypass", 0) != 0;
    next.timingSeconds = std::clamp(reader.Int("TimingSeconds", defaults.timingSeconds), 5, 600);
    next.reflectionSceneParts = std::clamp(reader.Int("ReflectionSceneParts", defaults.reflectionSceneParts), 0, 63);
    next.sharedUnderwaterFoam = reader.Int("SharedUnderwaterFoam", defaults.sharedUnderwaterFoam ? 1 : 0) != 0;
    next.skipEmptyWater = reader.Int("SkipEmptyWater", defaults.skipEmptyWater ? 1 : 0) != 0;
    next.reflectionSurfaceFlat = reader.Int("ReflectionSurfaceFlat", defaults.reflectionSurfaceFlat) != 0;
    next.reflectionView = std::clamp(reader.Int("ReflectionView", defaults.reflectionView), 0, 4);
    next.reflectionOwnedLighting = std::clamp(reader.Int("ReflectionOwnedLighting",defaults.reflectionOwnedLighting),0,4);
    next.reflectionProducer = std::clamp(reader.Int("ReflectionProducer", defaults.reflectionProducer), 0, 3);
    next.reflectionOwnedArchives = reader.WidePath("ReflectionOwnedArchives",next.reflectionOwnedArchives);
    next.reflectionOwnedTextureCache=reader.Int("ReflectionOwnedTextureCache",defaults.reflectionOwnedTextureCache);
    next.reflectionOwnedTextureCacheMiB=reader.Int("ReflectionOwnedTextureCacheMiB",defaults.reflectionOwnedTextureCacheMiB);
    next.reflectionOwnedTextureQuality=reader.Int("ReflectionOwnedTextureQuality",defaults.reflectionOwnedTextureQuality);
    next.reflectionOwnedTextureCacheDirectory=reader.WidePath("ReflectionOwnedTextureCacheDirectory",next.reflectionOwnedTextureCacheDirectory);
    if(next.reflectionOwnedTextureCache<0 || next.reflectionOwnedTextureCache>2)reader.Reject("ReflectionOwnedTextureCache");
    if(next.reflectionOwnedTextureCacheMiB<16 || next.reflectionOwnedTextureCacheMiB>1024)reader.Reject("ReflectionOwnedTextureCacheMiB");
    if(next.reflectionOwnedTextureQuality<0 || next.reflectionOwnedTextureQuality>1)reader.Reject("ReflectionOwnedTextureQuality");
    next.reflectionOwnedDelayMs = std::clamp(reader.Int("ReflectionOwnedDelayMs", defaults.reflectionOwnedDelayMs), 0, 10000);
    next.reflectionOwnedFail = reader.Int("ReflectionOwnedFail", defaults.reflectionOwnedFail) != 0;
    next.reflectionOwnedFailDraw = reader.Int("ReflectionOwnedFailDraw", defaults.reflectionOwnedFailDraw) != 0;
    next.reflectionOwnedUploadBytes = std::clamp(reader.Int("ReflectionOwnedUploadBytes", defaults.reflectionOwnedUploadBytes), 0, 1048576);
    next.waterSceneExposure = std::clamp(reader.Float("WaterSceneExposure",defaults.waterSceneExposure),.25f,2.f);
    next.reflectionBendReach = std::clamp(reader.Float("ReflectionBendReach",defaults.reflectionBendReach),0.0f,20.0f);
    next.reflectionWarp = std::clamp(reader.Float("ReflectionWarp", defaults.reflectionWarp * 100.0f), 0.0f, 5.0f) / 100.0f;
    next.reflectionCapture = std::max(reader.Int("ReflectionCapture", defaults.reflectionCapture), 0);
    next.reflectionCaptureFrames = std::clamp(reader.Int("ReflectionCaptureFrames", defaults.reflectionCaptureFrames), 1, 8);
    next.reflectionCaptureBurst = reader.Int("ReflectionCaptureBurst",defaults.reflectionCaptureBurst) != 0;
    next.waterWorkAudit = reader.Int("WaterWorkAudit",0) != 0;
    next.groundPreparationWorker = reader.Int("GroundPreparationWorker",0) != 0;
    next.reflectionSceneryShoreEdge = reader.Int("ReflectionSceneryShoreEdge",defaults.reflectionSceneryShoreEdge) != 0;
    next.reflectionSceneryAlign = reader.Int("ReflectionSceneryAlign",defaults.reflectionSceneryAlign) != 0;
    next.reflectionScenery = reader.Int("ReflectionScenery",next.reflectionProducer==3 || defaults.reflectionScenery) != 0;
    const int resolution=reader.Int("ReflectionResolution",defaults.reflectionResolution);
    if(resolution!=50 && resolution!=75 && resolution!=100)reader.Reject("ReflectionResolution");
    else next.reflectionResolution=static_cast<unsigned>(resolution);
    // The old switch treated every nonzero integer as Short, including 2.
    const int legacyDistance=reader.Int("ShortReflectionDistance",0)!=0 ? 1 : 0;
    const int distance=reader.Int("ReflectionDistance",legacyDistance);
    if(distance<0 || distance>2)reader.Reject("ReflectionDistance");
    else next.reflectionDistance=static_cast<water::ReflectionDistance>(distance);
    next.timeOfDay = reader.Float("TimeOfDay", defaults.timeOfDay);
    if (next.timeOfDay >= 24.0f)
        next.timeOfDay = -1.0f;
    next.dayCycleSeconds = std::clamp(reader.Float("DayCycleSeconds", defaults.dayCycleSeconds), 0.0f, 86400.0f);
    next.seabedDebug = std::clamp(reader.Int("SeabedDebug", defaults.seabedDebug), 0, 3);
    next.testShip = reader.Int("TestShip", defaults.testShip ? 1 : 0) != 0;
    next.traceLiquids = std::clamp(reader.Int("TraceLiquids", defaults.traceLiquids), 0, 2);
    next.logLevel = std::clamp(reader.Int("LogLevel", defaults.logLevel), 0, 2);
    if (!m_writeTime || LastWriteTime(m_path) != m_writeTime) reader.Reject("<file changed or unavailable>");
    if (!reader.Errors().empty()) {
        for (const auto& key : reader.Errors())
            if (std::find(m_invalidKeys.begin(), m_invalidKeys.end(), key) == m_invalidKeys.end())
                WATER_LOG_ERROR("settings rejected: invalid [Water] key %s; keeping last valid configuration", key.c_str());
        m_invalidKeys = reader.Errors();
        return false;
    }
    m_invalidKeys.clear();
    m_config = std::move(next);
    LogSetLevel(m_config.logLevel);
    return true;
}

std::string ConfigFile::Directory() const
{
    size_t slash = m_path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : m_path.substr(0, slash + 1);
}

LiquidStyle ConfigFile::SlimeStyle(const Config& config, float regionalStrength)
{
    LiquidStyle style = config.slimeStyle;
    if (regionalStrength > 0.0f)
    {
        // Apply the measured canal lift proportionally to the player's chosen pigment.
        constexpr float shallowGain[] = {0.45f / 0.42f, 0.75f / 0.70f, 0.13f / 0.12f};
        constexpr float deepGain[] = {0.13f / 0.10f, 0.39f / 0.28f, 0.043f / 0.03f};
        for (int channel = 0; channel < 3; ++channel)
        {
            style.shallowColor[channel] = std::min(1.0f, style.shallowColor[channel] * (1 + regionalStrength * (shallowGain[channel] - 1)));
            style.deepColor[channel] = std::min(1.0f, style.deepColor[channel] * (1 + regionalStrength * (deepGain[channel] - 1)));
            style.abyssColor[channel] = style.deepColor[channel];
        }
    }
    return style;
}

LiquidStyle ConfigFile::WaterStyle(const Config& config)
{
    LiquidStyle style = {};
    for (int i = 0; i < 3; ++i)
    {
        style.shallowColor[i] = config.shallowColor[i];
        style.deepColor[i] = config.deepColor[i];
        style.foamColor[i] = 0.95f;
        style.abyssColor[i] = config.abyssColor[i];
    }
    style.abyssDepth = config.abyssDepth;
    style.foamDepth = 1.4f;
    style.foamGlow = 0.0f;
    style.stockRelief = 0.0f;
    style.surfaceTransmission = 1.0f;
    style.insideBrightness = 1.0f;
    style.rippleResponse = 1.0f;
    style.nearReflection = config.nearReflection;
    style.surfaceDeepness = config.surfaceDeepness;
    style.gradientDepth = config.gradientDepth;
    style.clarity = config.clarity;
    style.waveStrength = config.waveStrength;
    style.waveCrestShape = config.waveCrestShape;
    style.waveCrestSharpness = config.waveCrestSharpness;
    style.waveSpeed = config.waveSpeed;
    style.reflection = config.reflection;
    style.stockTint = config.stockTint;
    style.foamStrength = config.foamStrength;
    return style;
}

ConfigFile& GlobalConfig()
{
    static ConfigFile config;
    return config;
}
