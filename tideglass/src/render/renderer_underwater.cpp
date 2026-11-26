// The view from under a liquid surface.
#include "render/renderer_internal.h"

using namespace render_detail;

void Renderer::PrepareUnderwaterShaders(IDirect3DDevice9* dev, const Config& cfg)
{
    if (!cfg.enable || !cfg.underwater) return;
    const bool scenery = cfg.underwaterScenery && cfg.screenReflections;
    const bool foam = cfg.sharedUnderwaterFoam || scenery;
    if (foam) EnsurePixelShader(dev, m_underwaterFoamPs);
    EnsurePixelShader(dev, scenery ? m_underwaterSceneryPs : (foam ? m_underwaterSharedPs : m_underwaterPs));
}

// What lies under the water, for the refraction to bend into (underwater_scene_ps): the scene captured before the
// liquid where water covers it, with the water's thickness there, and what stands out of the water (a character's
// body, a plant) or lies dry filled in from the water around it (FillIn), so a view bent onto it finds the bottom
// beside it rather than a hole. Leaves the render targets, viewport, pixel shader, stages 0 to 3 and the first
// constants changed.
bool Renderer::DrawUnderwaterScene(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                                   const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || !m_sceneBefore || FAILED(m_snapshot->GetLevelDesc(0, &full)))
        return false;
    // At half the resolution: a texel for every 2x2 pixels, the water reading the seabed itself at full resolution and
    // only what the fill made up from here (see water_ps's Refracted).
    const auto layout = reflection::TargetLayout(full.Width, full.Height, in.viewport, true);
    const UINT width = layout.width;
    const UINT height = layout.height;
    if (!EnsureFillChain(dev, m_underwaterFill, width, height))
        return Skip("underwater scene buffers could not be created");
    if(!SetPairTargets(dev, m_underwaterFill.pull[0]))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    const auto& vp = layout.viewport;
    // An inset draw leaves a border that the coarser fill levels can still sample.
    if (vp.X || vp.Y || vp.Width != width || vp.Height != height)
    {
        const D3DVIEWPORT9 whole = {0, 0, width, height, 0.0f, 1.0f};
        if (FAILED(dev->SetViewport(&whole)) || FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))
            return false;
    }
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_underwaterScenePs)))return false;
    if(!BindWaterTextures(dev, depthTexture, in, cfg))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(!FillIn(dev,m_underwaterFill))return false;
    return true;
}

bool Renderer::DrawUnderwaterFoam(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                  const D3DSURFACE_DESC& desc, const FrameInputs& in, const Config& cfg,
                                  const float (&frame)[9][4], const float (&water)[kWaterConstants][4])
{
    // The surface composite has consumed its detail buffer by the end-of-world underwater pass.
    m_surfaceDetailDrawn = false;
    if (!EnsureSurfaceDetailTargets(dev, desc, false) || !SetTarget(dev, m_surfaceDetail[0])) return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &water[0][0], kWaterConstants);
    BindWaterControls(dev, cfg, m_usingEnvironment, m_usingWaveMoments);
    if (FAILED(BindPixelShader(dev, m_underwaterFoamPs))) return false;
    BindPointTexture(dev, 1, m_snapshotValid && m_sceneValid ? m_snapshot : depthTexture);
    BindPointTexture(dev, 4, m_level[0] ? m_level[m_levelIndex] : nullptr);
    BindLinearTexture(dev, 6, m_waterSurface.Ripples());
    BindLinearTexture(dev, 7, GroundTexture(dev, in.ground));
    BindLinearTexture(dev, 8, in.ground ? m_shore : nullptr);
    BindPointTexture(dev, kBoatPathStage, BoatPathTexture(dev, in));
    DrawScreenTriangle(dev);
    return true;
}

void Renderer::DrawUnderwater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                              const FrameInputs& in, const Config& cfg, const float (&frame)[9][4])
{
    const bool slime = in.cameraInSlime;
    float water[kWaterConstants][4];
    D3DSURFACE_DESC desc = {};
    SurfaceSim& surface = slime ? m_slimeSurface : m_waterSurface;
    if (!BuildWaterConstants(in, cfg, slime ? ConfigFile::SlimeStyle(cfg, in.zoneId, in.subzoneId) : Weathered(ConfigFile::WaterStyle(cfg), in, cfg), surface,
                             frame, water) ||
        FAILED(target->GetDesc(&desc)) || !EnsureSceneTargets(dev, desc) ||
        !CopyToTexture(dev, target, m_sceneWithWater))
        return;
    ++m_work[slime?8:7];
    if(cfg.waterWorkAudit)TimeBegin(GpuTimer::UnderwaterView);
    if (slime)
    {
        water[kUnderwaterRegister - kFrameConstants][0] = cfg.slimeVisibility;
        water[kUnderwaterWindowRegister - kFrameConstants][0] = cfg.slimeWindow;
        water[kUnderwaterWindowRegister - kFrameConstants][2] = cfg.slimeGlancingMirror;
        water[kUnderwaterWindowRegister - kFrameConstants][3] = 1.0f;
        std::memcpy(water[kTwilightRegister - kFrameConstants], cfg.slimeInsideColor, sizeof(cfg.slimeInsideColor));
        std::memcpy(water[kAbyssRegister - kFrameConstants], cfg.slimeInsideDeepColor,
                    sizeof(cfg.slimeInsideDeepColor));
    }
    else
    {
        if(!AdvanceSurface(dev,m_waterSurface,m_level[0]?m_level[m_levelIndex]:nullptr,WaterPhysics(cfg),in,cfg,frame,water)){
            if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
            return;
        }
    }
    if(!PrepareShipWakes(dev,in,cfg,frame,water)){
        if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
        return;
    }
    // Under slime as under water: the mirrored scene below the surface shows on its underside (2026-11-09).
    const bool scenery=cfg.underwaterScenery && cfg.screenReflections && in.CameraUnderSurface();
    const bool source=scenery && m_sceneryUnderwater && m_sceneryColour && m_sceneryDepth &&
        std::fabs(m_sceneryProjection[0]-frame[2][0])<=1e-5f &&
        std::fabs(m_sceneryProjection[5]-frame[2][1])<=1e-5f &&
        std::fabs(m_sceneryProjection[8]-frame[2][2])<=1e-5f &&
        std::fabs(m_sceneryProjection[9]-frame[2][3])<=1e-5f;
    const float currentWater=in.cameraWaterHeight;
    const bool nativeWater=scenery && in.cameraWaterHeightKnown;
    IDirect3DTexture9* nativeSurface = slime ? (m_slimeCaptured ? m_slime.depth : nullptr) :
        (m_nativeWater ? m_nativeWater : (m_waterDepthValid ? m_waterDepth : nullptr));
    const float nativeMode = nativeSurface ? (!slime && m_nativeWater ? 1.f : 2.f) : 0.f;
    float nativePlane[4]={nativeWater?1.f:0.f,currentWater,nativeMode,0};
    // The foam pass has no live-depth input; s0 reads the same native surface there.
    BindPointTexture(dev,0,nativeSurface);
    dev->SetPixelShaderConstantF(67,nativePlane,1);
    const bool foam = !slime && (cfg.sharedUnderwaterFoam || scenery) && DrawUnderwaterFoam(dev, depthTexture, desc, in, cfg, frame, water);
    dev->SetRenderTarget(0, target);
    BindPointTexture(dev, kUnderwaterFoamStage, foam ? m_surfaceDetail[0] : nullptr);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &water[0][0], kWaterConstants);
    // Slime has its own absorption and no foam texture. The same shader can
    // show the ordinary underside while a new scene or device is warming up.
    const bool slimeMirror = slime && scenery && EnsurePixelShader(dev, m_slimeUnderwaterSceneryPs);
    IDirect3DPixelShader9*& underside = slimeMirror ? m_slimeUnderwaterSceneryPs :
        (!slime && scenery ? m_underwaterSceneryPs : (foam ? m_underwaterSharedPs : m_underwaterPs));
    if (FAILED(BindPixelShader(dev, underside))) return;
    D3DSURFACE_DESC mirror={};if(source)m_sceneryDepth->GetLevelDesc(0,&mirror);
    const float reflected[4]={source?1.f:0.f,m_sceneryHeight,float(mirror.Width),float(mirror.Height)};
    dev->SetPixelShaderConstantF(65,reflected,1);
    // Long sightlines near the surface still see sunlit water. The shader keeps
    // the full path for absorption and adds the real descent when looking down.
    float distanceAsDepth = 0.5f;
    float shallowDaylight = 0.f;
    if (!slime && in.cameraWaterHeightKnown)
    {
        const float gradientDepth = water[WATER_SLOT_cDeepColorDepth - kFrameConstants][3];
        const float cameraDepth = currentWater - in.cameraPosition[2];
        const float shallow = std::clamp(1.f - cameraDepth / (2.f * gradientDepth), 0.f, 1.f);
        distanceAsDepth -= 0.35f * shallow;
        shallowDaylight = 0.45f * shallow * std::clamp(in.daylight, 0.f, 1.f);
    }
    const float sourceProjection[4]={m_sceneryProjection[10],m_sceneryProjection[14],shallowDaylight,distanceAsDepth};
    dev->SetPixelShaderConstantF(66,sourceProjection,1);
    // What lies beyond the surface comes from the depth and colour captured before the client drew its own surface
    // (seen from below it would hide everything above).
    const bool beyondSurface = m_snapshotValid && m_sceneValid;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, beyondSurface ? m_snapshot : depthTexture);
    BindLinearTexture(dev, 2, beyondSurface ? m_sceneBefore : m_sceneWithWater);
    BindPointTexture(dev, 3, m_sceneWithWater);
    if (slime)
        BindPointTexture(dev, 4, m_slime.level[0] ? m_slime.level[m_slime.levelIndex] : nullptr);
    else
        BindPointTexture(dev, 4, m_level[0] ? m_level[m_levelIndex] : nullptr);
    const float airWeight = !slime && beyondSurface ? ObservedAirWeight(in) : 0.f;
    if (airWeight > 0 && BindPointTexture(dev, WATER_SLOT_sRememberedSky, m_observedAir))
    {
        nativePlane[3] = airWeight;
        dev->SetPixelShaderConstantF(67, nativePlane, 1);
    }
    BindLinearTexture(dev, 6, surface.Ripples());
    BindLinearTexture(dev, 7, slime ? nullptr : GroundTexture(dev, in.ground));
    BindLinearTexture(dev, 8, !slime && in.ground ? m_shore : nullptr);
    BindPointTexture(dev, kBoatPathStage, slime ? nullptr : BoatPathTexture(dev, in));
    if (slimeMirror || (!slime && scenery)) {
        BindPointTexture(dev, 12, source ? m_sceneryColour : nullptr);
        BindPointTexture(dev, 13, source ? m_sceneryDepth : nullptr);
    }
    // Only the water variants using shared foam have their native surface on s10.
    BindPointTexture(dev, !slime && (scenery || foam) ? 10 : 11, nativeSurface);
    DrawScreenTriangle(dev);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
    ++m_underwaterFrames;
}
