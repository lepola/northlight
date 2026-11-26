// The sky the water mirrors where nothing else is reflected, and the weather and light the water is shaded with.
#include "render/renderer_internal.h"

using namespace render_detail;

bool Renderer::EnsureMemoryTargets(IDirect3DDevice9* dev, IDirect3DTexture9* (&targets)[2], UINT width, UINT height,
                                   D3DFORMAT format)
{
    if(targets[0]&&targets[1])return true;
    for(auto*& target:targets){
        SafeRelease(target);
        const D3DVIEWPORT9 vp={0,0,width,height,0,1};
        if(FAILED(dev->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&target,nullptr))||
           !SetTarget(dev,target)||FAILED(dev->SetViewport(&vp))||FAILED(dev->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))){
            SafeRelease(targets[0]);SafeRelease(targets[1]);return false;
        }
    }
    return true;
}

// From the finished image (after the fog), so the sky is remembered as it is seen, fog glow and all.
void Renderer::RememberSky(IDirect3DDevice9* dev, IDirect3DTexture9* depth, IDirect3DTexture9* finished, const FrameInputs& in,
                           const Config& cfg, bool seesAboveHorizon)
{
    if (!EnsureMemoryTargets(dev, m_sky, kSkySides, kSkyRows))
        return;
    ++m_work[5];
    const int next = m_skyIndex ^ 1;
    if(!SetTarget(dev, m_sky[next]))return;
    D3DVIEWPORT9 vp = {0, 0, kSkySides, kSkyRows, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float horizon[]={m_level[0]?1.f:0.f,1.f/std::max(FarClipDistance(in.glProjection),1.f),0,0};
    dev->SetPixelShaderConstantF(46,horizon,1);
    BindPointTexture(dev,4,m_level[0]?m_level[m_levelIndex]:nullptr);
    BindPointTexture(dev,0,depth);
    BindPointTexture(dev, 1, m_snapshot);
    BindPointTexture(dev, 2, finished);
    BindPointTexture(dev, 3, m_sky[m_skyIndex]);
    const DWORD now = GetTickCount();
    bool observeAir = false;
    if (cfg.underwater && seesAboveHorizon && m_surfaceResult == SurfaceResult::Shaded &&
        !in.cameraInWater && !in.cameraInSlime && in.mapId >= 0 && (!m_airTick || now - m_airTick >= 200))
    {
        m_airTick = now;
        m_airMap = -1;
        if (!m_observedAir)
            dev->CreateTexture(kSkySides, kSkyRows, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                               D3DPOOL_DEFAULT, &m_observedAir, nullptr);
        observeAir = m_observedAir && SUCCEEDED(BindPixelShader(dev, m_skyObservedAirPs)) &&
                     SUCCEEDED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)) && SetTarget(dev, m_observedAir, 1);
    }
    if (!observeAir && FAILED(BindPixelShader(dev, m_skyMemoryPs))) return;
    const std::string capture=!m_reflectionCaptureDirectory.empty() ? m_reflectionCaptureDirectory+"/sky-memory" : "";
    const bool captured=!capture.empty() && reflection::CapturePassInputs(dev,capture,m_snapshotPs,ActiveCaptureBurst());
    const bool drawn = SUCCEEDED(DrawScreenTriangle(dev));
    if (observeAir)
    {
        dev->SetRenderTarget(1, nullptr);
        m_airMap = drawn ? in.mapId : -1;
        std::copy_n(in.cameraPosition, 3, m_airPosition);
        std::copy_n(in.toLight, 3, m_airLight);
        m_airWeather = in.weatherType;
        m_airIntensity = in.weatherIntensity;
    }
    if (captured) reflection::CapturePassOutputs(dev,capture,"output",ActiveCaptureBurst());
    m_skyIndex = next;
    // Extend missing rows from the observed sky: reuse the previous memory target after the update has finished.
    // Unbind it before targeting it; only the updated memory is read here.
    for (DWORD stage=0;stage<kStages;++stage) dev->SetTexture(stage,nullptr);
    if(!SetTarget(dev,m_sky[next^1]))return;
    BindPointTexture(dev,3,m_sky[next]);
    if (FAILED(BindPixelShader(dev, m_skyExtendPs))) return;
    if (SUCCEEDED(DrawScreenTriangle(dev))) m_skyIndex=next^1;
}

float Renderer::ObservedAirWeight(const FrameInputs& in) const
{
    if (!m_observedAir || m_airMap < 0 || in.mapId != m_airMap || in.weatherType != m_airWeather)
        return 0;
    float distance2 = 0, lightChange2 = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        const float moved = in.cameraPosition[i] - m_airPosition[i], turned = in.toLight[i] - m_airLight[i];
        distance2 += moved * moved;
        lightChange2 += turned * turned;
    }
    // The observed glow belongs to the crossing. Ease it out during long swims or a change of sky.
    const float age = (GetTickCount() - m_airTick) * 0.001f;
    return std::clamp(std::min({(120.f - age) / 60.f, (96.f - std::sqrt(distance2)) / 48.f,
        (0.15f - std::sqrt(lightChange2)) / 0.10f, (0.3f - std::abs(in.weatherIntensity - m_airIntensity)) / 0.2f}), 0.f, 1.f);
}

// Bind the accepted environment when available, otherwise the ordinary sky memory.
bool Renderer::BindSky(IDirect3DDevice9* dev, DWORD stage, const Config& cfg, float reflectedFogWeight)
{
    if(m_usingEnvironment&&m_environment){
        const auto& values=m_environment->Values();
        float controls[8][4]={{cfg.waterAtmosphere==1?1.f:0.f,m_environment->Background()?1.f:0.f,values.referenceDistance,std::log(values.transmissionAtReference)},
            {values.light.angularRadius,m_environmentRayStep,0,cfg.waterHorizonBlend?1.f:0.f}};
        const unsigned glare=values.light.moon?WaterBackgroundMoonGlare:WaterBackgroundSunGlare;
        controls[1][2]=m_environment->Background()&&(values.backgroundContent&glare)?1.f:0.f;
        for(unsigned i=0;i<6;++i)ColorToFloat(values.palette[i],1,controls[i+2]);
        // The reflected water-to-scenery leg belongs to the material with either owner of camera distance.
        const float layout[4]={2,reflectedFogWeight,0,0};
        return SUCCEEDED(dev->SetPixelShaderConstantF(WATER_SLOT_cEnvironment,&controls[0][0],8))&&
            SUCCEEDED(dev->SetPixelShaderConstantF(kSkyLayoutRegister,layout,1))&&
            BindLinearTexture(dev,stage,m_environment->Background());
    }
    const bool bound=BindPointTexture(dev,stage,m_sky[0]?m_sky[m_skyIndex]:nullptr);
    const float layout[4] = {0.0f, 0.0f, 0.0f, m_sky[0] ? 0.0f : -1.0f};
    return SUCCEEDED(dev->SetPixelShaderConstantF(kSkyLayoutRegister,layout,1))&&bound;
}

// Water under fog, cloud or rain: less light reaches the bottom and the grey sky's reflection dominates, so the
// colours turn duller and greyer, shallow and deep closer together, tinted a little by the fog. How overcast it is
// comes from the game's storm weight and how near its fog closes in.
float Renderer::OvercastAmount(const FrameInputs& in, const Config& cfg)
{
    const float clearFog = std::clamp((in.fogEnd - kFoggiestFogEnd) / (kClearFogEnd - kFoggiestFogEnd), 0.0f, 1.0f);
    const float overcast = 1.0f - (1.0f - std::clamp(in.stormWeight, 0.0f, 1.0f)) * clearFog;
    return overcast * cfg.overcastWater;
}

LiquidStyle Renderer::Weathered(const LiquidStyle& style, const FrameInputs& in, const Config& cfg)
{
    const float amount = OvercastAmount(in, cfg);
    if (amount <= 0.0f)
        return style;
    float fog[4];
    ColorToFloat(in.fogColor, 1.0f, fog);
    const float fogLuma = std::max(0.2126f * fog[0] + 0.7152f * fog[1] + 0.0722f * fog[2], 1.0e-3f);
    auto weather = [&](const float* in3, float* out3) {
        const float luma = 0.2126f * in3[0] + 0.7152f * in3[1] + 0.0722f * in3[2];
        for (int i = 0; i < 3; ++i)
        {
            const float muted = luma + (fog[i] / fogLuma * luma - luma) * kOvercastFogTint;
            out3[i] = (in3[i] + (muted - in3[i]) * amount) * (1.0f - kOvercastDimming * amount);
        }
    };
    LiquidStyle weathered = style;
    weather(style.shallowColor, weathered.shallowColor);
    weather(style.deepColor, weathered.deepColor);
    weather(style.abyssColor, weathered.abyssColor);
    return weathered;
}

WaterEnvironmentValues Renderer::NativeEnvironment(const FrameInputs& in)
{
    WaterEnvironmentValues values={};values.policy=WaterEnvironmentCoreOwnsDistance;
    values.encoding=WaterEnvironmentSurfaceCompositeRGB;values.paletteKnown=in.skyPaletteKnown?1:0;
    std::memcpy(values.palette,in.skyPalette,sizeof values.palette);
    values.environmentKey=in.mapId>=0?uint64_t(in.mapId)+1:0;
    values.light=water::NativeWaterLight(in.toLight,in.sunColor,in.lightIsMoon,1-std::clamp(in.stormWeight,0.f,1.f));
    values.referenceDistance=std::max(FarClipDistance(in.glProjection),1.f);
    values.transmissionAtReference=.35f;
    return values;
}
