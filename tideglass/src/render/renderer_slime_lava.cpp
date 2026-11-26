// Slime and lava. The client's own surface is captured and undone as it is drawn, then shaded once over the finished world.
#include "render/renderer_internal.h"

using namespace render_detail;

bool Renderer::EnsureDeferredTargets(IDirect3DDevice9* dev, DeferredSurface& surface, const D3DSURFACE_DESC& color, bool keepStock)
{
    if (surface.depth && surface.desc.Width == color.Width && surface.desc.Height == color.Height &&
        surface.desc.Format == color.Format)
        return true;
    SafeRelease(surface.depth);
    SafeRelease(surface.stock);
    SafeRelease(surface.fade);
    if (FAILED(dev->CreateTexture(color.Width, color.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                                  &surface.depth, nullptr)) ||
        (keepStock && FAILED(dev->CreateTexture(color.Width, color.Height, 1, D3DUSAGE_RENDERTARGET, color.Format, D3DPOOL_DEFAULT,
                                                &surface.stock, nullptr))))
    {
        SafeRelease(surface.depth);
        SafeRelease(surface.stock);
        surface.desc = {};
        return Skip("liquid surface targets could not be created");
    }
    surface.desc = color;
    return true;
}

void Renderer::ReleaseDeferred(DeferredSurface& surface)
{
    SafeRelease(surface.level[0]);
    SafeRelease(surface.level[1]);
    SafeRelease(surface.depth);
    SafeRelease(surface.stock);
    SafeRelease(surface.fade);
    surface.desc = {};
    surface.thisFrame = surface.outdoorThisFrame = surface.fadeThisFrame = surface.fadeKept = false;
}

void Renderer::WriteDepthAlways(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* boundDepth,
                                const D3DVIEWPORT9& viewport)
{
    dev->SetRenderTarget(0, target);
    dev->SetDepthStencilSurface(boundDepth);
    const D3DVIEWPORT9 vp = FullDepth(viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
}

bool Renderer::SnapshotSurfaceBase(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth, const RECT* area)
{
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)))
        return false;
    D3DSURFACE_DESC existing = {};
    if (m_surfaceBaseDepth && (FAILED(m_surfaceBaseDepth->GetLevelDesc(0, &existing)) || existing.Width != desc.Width ||
                             existing.Height != desc.Height))
        SafeRelease(m_surfaceBaseDepth);
    if (!m_surfaceBaseDepth && FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                       D3DPOOL_DEFAULT, &m_surfaceBaseDepth, nullptr)))
        return Skip("liquid base depth target creation failed");
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if(!SetTarget(dev, m_surfaceBaseDepth))return false;
    D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float texel[4] = {1.0f / desc.Width, 1.0f / desc.Height, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(1, texel, 1);
    if (FAILED(BindPixelShader(dev, m_snapshotPs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    const bool drawn=SUCCEEDED(DrawScreenArea(dev,area));
    return saved.Restore() && drawn;
}

bool Renderer::CaptureAndUndoSurface(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                     IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg,
                                     bool inLiquidPass, LiquidKind kind, bool undo, const RECT* area, int ramp)
{
    DeferredSurface& surface = kind == LiquidKind::Magma ? m_lava.surface : m_slime;
    // The lava is drawn from nothing but where the surface lies; slime keeps the client's colour as well.
    const bool keepStock = kind != LiquidKind::Magma;
    if (!m_snapshotValid || !m_sceneValid)
        return Skip("no scene snapshot before the liquid");
    IDirect3DTexture9* depthBefore = inLiquidPass ? m_surfaceBaseDepth : m_snapshot;
    if (!depthBefore)
        return Skip("no depth captured before the liquid");
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    D3DSURFACE_DESC colorDesc = {};
    bool ok = SUCCEEDED(saved.color[0]->GetDesc(&colorDesc)) && EnsureDeferredTargets(dev, surface, colorDesc, keepStock) &&
              (!keepStock || CopyToTexture(dev, saved.color[0], m_sceneWithWater));
    if (ok)
    {
        const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
        dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
        if (!surface.thisFrame)
        {
            if (keepStock) m_buildingSlime = false;
            if (!keepStock) m_lava.ordinarySeen = m_lava.felSeen = false;
            if(!SetTarget(dev, surface.depth))return false;
            dev->SetViewport(&full);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);
            surface.area = area ? *area : RECT{};
            surface.areaWhole = !area;
            surface.thisFrame = true;
        }
        if (!area) surface.areaWhole = true;
        else if (!surface.areaWhole) {
            surface.area.left = std::min(surface.area.left, area->left);
            surface.area.top = std::min(surface.area.top, area->top);
            surface.area.right = std::max(surface.area.right, area->right);
            surface.area.bottom = std::max(surface.area.bottom, area->bottom);
        }
        if (FAILED(BindPixelShader(dev, m_slimeCapturePs))) return false;
        BindPointTexture(dev, 0, depthTexture);
        BindPointTexture(dev, 1, depthBefore);
        BindPointTexture(dev, 2, keepStock ? m_sceneWithWater : nullptr);
        // Depth magnitude stays exact. Its sign carries the palette without another full-size target.
        const bool fel = !keepStock && ramp == NativeLiquidFrame::FelMagma;
        const float captureDepth[4] = {0.0f, fel ? -1.0f : 1.0f, 0.0f, 1.0f};
        const float captureColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        if(!SetTarget(dev, surface.depth))return false;
        dev->SetViewport(&full);
        dev->SetPixelShaderConstantF(8, captureDepth, 1);
        DrawScreenArea(dev, area);
        if (keepStock)
        {
            if(!SetTarget(dev, surface.stock))return false;
            dev->SetViewport(&full);
            dev->SetPixelShaderConstantF(8, captureColor, 1);
            DrawScreenArea(dev, area);
        }
        // A fade draw is water as well: mark where it lies (with the depth under it), so the composite lays the slime
        // over the water there.
        if (kind == LiquidKind::Fade || surface.fadeThisFrame)
        {
            if (!surface.fade && FAILED(dev->CreateTexture(colorDesc.Width, colorDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                           D3DPOOL_DEFAULT, &surface.fade, nullptr)))
                ok = Skip("slime fade target creation failed");
            if (ok)
            {
                if (!SetTarget(dev, surface.fade)) return false;
                dev->SetViewport(&full);
                if (!surface.fadeThisFrame)
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                surface.fadeThisFrame = true;
                surface.fadeKept |= kind == LiquidKind::Fade && !undo;
                const float captureFade[4] = {2.0f, kind != LiquidKind::Fade ? 0.0f : (ramp == 0 ? -1.0f : 1.0f), 0.0f, 1.0f};
                dev->SetPixelShaderConstantF(8, captureFade, 1);
                DrawScreenArea(dev, area);
            }
        }

        if (ok && undo)
        {
            WriteDepthAlways(dev, saved.color[0], boundDepth, full);
            if (FAILED(BindPixelShader(dev, m_slimeUndoPs))) return false;
            BindPointTexture(dev, 0, surface.depth);
            BindPointTexture(dev, 1, depthBefore);
            BindPointTexture(dev, 2, m_sceneBefore);
            DrawScreenArea(dev, area);
        }
    }
    if (!saved.Restore()) return false;
    surface.outdoorThisFrame |= ok && inLiquidPass;
    m_buildingSlime |= ok && keepStock && !inLiquidPass;
    m_slimeCaptured |= ok && keepStock;
    if (ok && !keepStock) {
        ++m_lava.work.captures;
        m_lava.felSeen |= ramp == NativeLiquidFrame::FelMagma;
        m_lava.ordinarySeen |= ramp != NativeLiquidFrame::FelMagma;
    }
    return ok;
}

bool Renderer::CompositeDeferredSlime(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                      IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (!m_slime.thisFrame)
        return false;
    m_slime.thisFrame = false;
    const bool fade = m_slime.fadeThisFrame && m_slime.fade;
    const bool fadeKept = fade && m_slime.fadeKept;
    m_slime.fadeThisFrame = m_slime.fadeKept = false;
    PrepareRainRings(dev, boundDepth, in, cfg);
    CompositeScene scene(*this);
    if (!scene.Capture(dev, depthTexture, boundDepth))
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    const float regionalStrength = cfg.slimeStrengthFor(in.zoneId,in.subzoneId);
    const LiquidStyle style = ConfigFile::SlimeStyle(cfg, in.zoneId, in.subzoneId);
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, style, m_slimeSurface, frame, constants))
        return false;
    // Slime's glints follow its body pigment. Clear-water light colours produced
    // blue-white patches on ripple facets and the thin, less glowing shoreline.
    for (unsigned channel = 0; channel < 3; ++channel)
        constants[kSunColorRegister - kFrameConstants][channel] *= style.shallowColor[channel];
    // Slime has no open-water crests, so its own glow rides where water carries their foam (SlimeBodyGlow).
    constants[WATER_SLOT_cLookingDown - kFrameConstants][3] = style.bodyGlow;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    TimeBegin(GpuTimer::SlimeSurface);
    bool prepared=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&
        MeasureLevel(dev,m_slime.depth,m_slime.level,m_slime.levelIndex);
    IDirect3DTexture9* slimeLevel = m_slime.level[0] ? m_slime.level[m_slime.levelIndex] : nullptr;
    // Slime has its own measured level and no water ground/shore field.
    constants[kGroundRegister-kFrameConstants][2]=0;
    prepared=prepared&&AdvanceSurface(dev,m_slimeSurface,slimeLevel,SlimePhysics(cfg, regionalStrength),in,cfg,frame,constants);
    // The waves that lap at the slime's edge (water_common's ReadShoreWaves) ride in slots slime leaves unused, where
    // water carries how peaked its swell is: how strong they are, and how many times shorter than the sea's. Inside a
    // building they are short laps against the steps and walls of a canal; under the sky, longer folds up a bank.
    m_slimeShoreMeasured=prepared&&cfg.slimeShoreWaves>0&&MeasureSlimeShore(dev);
    constants[WATER_SLOT_cLookingDown-kFrameConstants][2]=m_slimeShoreMeasured?cfg.slimeShoreWaves:0.0f;
    constants[WATER_SLOT_cRippleGrid-kFrameConstants][3]=m_buildingSlime?kSlimeLapScaleIndoors:kSlimeLapScaleOutdoors;
    TimeEnd(GpuTimer::SlimeSurface);
    if(!prepared){saved.Restore();return false;}
    if (cfg.slime && in.cameraInSlime)
    {
        // From below, the under-slime view draws the surface itself from the scene captured without it.
        saved.Restore();
        return true;
    }
    const bool controls=BindLiquidOptions(dev,cfg,false)&&BindWaterControls(dev,cfg,false);
    TimeBegin(GpuTimer::SlimeRays);
    // Building slime can use the owned scene's imported receiver mask. A host
    // mirror without that mask has no proof that it represents this pool.
    const bool reflections=controls&&(m_slime.outdoorThisFrame||m_sceneryViews)&&cfg.screenReflections&&cfg.reflectionScenery&&
        DrawReflections(dev,m_slime.depth,in,cfg,frame,constants,true);
    TimeEnd(GpuTimer::SlimeRays);
    TimeBegin(GpuTimer::SlimeShade);
    dev->SetRenderTarget(1,nullptr);
    // Fade draws the host left drawn are in the depth just captured: the ground under them instead (slime_lift_ps).
    const bool lifted=fadeKept&&LiftFadeSurfaces(dev,desc,frame);
    dev->SetRenderTarget(0, saved.color[0]);
    WriteDepthAlways(dev, saved.color[0], boundDepth, in.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    const bool inspectMirror = cfg.reflectionView == 2 || cfg.reflectionView == 3;
    bool bound=controls&&SUCCEEDED(inspectMirror ? BindPixelShader(dev,m_slimeReflectionViewPs) :
                                   BindHighlightShader(dev,cfg,m_slimeDeferredPs,m_slimeDeferredFinePs))&&
        BindWaterTextures(dev,m_slime.depth,in,cfg,true);
    if(lifted)bound=BindPointTexture(dev,1,m_slimeGround)&&bound;
    D3DSURFACE_DESC reflectionDesc={};
    const bool drawn=reflections&&SUCCEEDED(m_reflections[0]->GetLevelDesc(0,&reflectionDesc));
    const float grid[4]={float(reflectionDesc.Width),float(reflectionDesc.Height),drawn?1.f:0.f,drawn && m_buildingSlime && cfg.specular>0 ? 1.f : 0.f};
    bound=SUCCEEDED(dev->SetPixelShaderConstantF(kReflectionRegister,grid,1))&&bound;
    bound=BindPointTexture(dev,kReflectionStage,drawn?m_reflections[0]:nullptr)&&bound;
    bound=BindPointTexture(dev,kReflectionHitStage,drawn?m_reflections[1]:nullptr)&&bound;
    bound=BindSlimeFade(dev,fade?m_slime.fade:nullptr,in.nativeLiquids)&&bound;
    const bool shaded=bound&&SUCCEEDED(DrawScreenArea(dev,m_slime.areaWhole?nullptr:&m_slime.area));
    TimeEnd(GpuTimer::SlimeShade);
    const bool restored=saved.Restore();
    if(shaded&&restored)++m_slimeComposites;
    return shaded&&restored;
}

bool Renderer::MeasureSlimeShore(IDirect3DDevice9* dev)
{
    IDirect3DTexture9* grid = m_slimeSurface.Ripples();
    if (!grid)
        return false;
    for (auto*& map : m_slimeShore)
        if (!map && FAILED(dev->CreateTexture(kSlimeShoreCells, kSlimeShoreCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F,
                                              D3DPOOL_DEFAULT, &map, nullptr)))
            return Skip("slime shore map could not be created");
    const D3DVIEWPORT9 cells = {0, 0, kSlimeShoreCells, kSlimeShoreCells, 0.0f, 1.0f};
    const float cell = 1.0f / kSlimeShoreCells;
    const float edge[4] = {0.0f, 0.0f, 0.0f, cell};
    const float across[4] = {cell, 0.0f, 1.0f, cell};
    const float down[4] = {0.0f, cell, 2.0f, cell};
    const float soften[4] = {0.0f, 0.0f, 3.0f, cell};
    const auto pass = [&](const float* way, IDirect3DTexture9* source, bool filtered, IDirect3DTexture9* target) {
        return SetTarget(dev, target) && SUCCEEDED(dev->SetViewport(&cells)) &&
               SUCCEEDED(dev->SetPixelShaderConstantF(0, way, 1)) &&
               (filtered ? BindLinearTexture(dev, 0, source) : BindPointTexture(dev, 0, source)) &&
               SUCCEEDED(DrawScreenTriangle(dev));
    };
    // The grid is read between its cells, four to each of the map's, and the softening between the map's own, so
    // both have to be filtered. The two targets take the passes in turn; the first ends with the finished map.
    return SUCCEEDED(BindPixelShader(dev, m_slimeShorePs)) && pass(edge, grid, true, m_slimeShore[1]) &&
           pass(across, m_slimeShore[1], false, m_slimeShore[0]) && pass(down, m_slimeShore[0], false, m_slimeShore[1]) &&
           pass(soften, m_slimeShore[1], true, m_slimeShore[0]);
}

bool Renderer::LiftFadeSurfaces(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4])
{
    D3DSURFACE_DESC existing = {};
    if (m_slimeGround && (FAILED(m_slimeGround->GetLevelDesc(0, &existing)) || existing.Width != desc.Width ||
                          existing.Height != desc.Height))
        SafeRelease(m_slimeGround);
    if (!m_slimeGround && FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                    D3DPOOL_DEFAULT, &m_slimeGround, nullptr)))
        return Skip("slime ground target creation failed");
    if (!SetTarget(dev, m_slimeGround))
        return false;
    const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&full);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    return SUCCEEDED(BindPixelShader(dev, m_slimeLiftPs)) && BindPointTexture(dev, 0, m_snapshot) &&
           BindPointTexture(dev, 2, m_slime.depth) && BindPointTexture(dev, 3, m_slime.fade) &&
           SUCCEEDED(DrawScreenTriangle(dev));
}

// Where a fade draw lies (slime_fade.hlsli): the mark and the lights the slime share is worked out from, in the order
// NativeLiquidFrame keeps them. Without a fade draw only the switch (off) is set.
bool Renderer::BindSlimeFade(IDirect3DDevice9* dev, IDirect3DTexture9* mark, const NativeLiquidFrame& liquids)
{
    constexpr unsigned kMax = NativeLiquidFrame::MaxStyleLights;
    float constants[2 + 3 * kMax][4] = {};
    const bool active = mark && liquids.baseKnown;
    const unsigned count = active ? unsigned(std::min<size_t>(liquids.lights.size(), kMax)) : 0;
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned c = 0; c < 3; ++c)
            constants[kind][c] = float(liquids.base[kind][c]);
    constants[0][3] = float(count);
    constants[1][3] = active ? 1.0f : 0.0f;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& light = liquids.lights[i];
        for (unsigned c = 0; c < 3; ++c)
        {
            constants[2 + i][c] = float(light.centre[c]);
            constants[2 + kMax + i][c] = float(light.colour[0][c]);
            constants[2 + 2 * kMax + i][c] = float(light.colour[1][c]);
        }
        constants[2 + i][3] = float(light.outer);
        const float slope = float(1.0 / (light.outer - light.inner));
        constants[2 + kMax + i][3] = light.usable[0] ? slope : 0.0f;
        constants[2 + 2 * kMax + i][3] = light.usable[1] ? slope : 0.0f;
    }
    const UINT rows = active ? 2 + 3 * kMax : 2;
    return SUCCEEDED(dev->SetPixelShaderConstantF(kSlimeFadeRegister, &constants[0][0], rows)) &&
           BindPointTexture(dev, kSlimeFadeStage, active ? mark : nullptr);
}

// One step of how stirred the lava is (lava_stir_ps), on a grid over the same square as the ground map and the ripples.
bool Renderer::AdvanceLavaStir(IDirect3DDevice9* dev, IDirect3DTexture9* level, const FrameInputs& in, const Config& cfg,
                               const float (&frame)[9][4], float (&constants)[kWaterConstants][4])
{
    SurfaceSim& sim = m_lava.stir;
    float* rippleArea = constants[kRippleAreaRegister - kFrameConstants];
    if (sim.advancedThisFrame)
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        return true;
    }
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                  D3DPOOL_DEFAULT, target, nullptr)))
        {
            ReleaseSurface(sim);
            return Skip("lava stir targets could not be created");
        }
    struct Update {
        SurfaceSim& sim;
        bool complete = false;
        ~Update() { if (!complete) { sim.valid = false; sim.advancedThisFrame = false; } }
    } update{sim};
    const float origin[2] = {in.ground ? in.ground->Origin()[0] : GroundMap::OriginFor(in.cameraPosition[0]),
                             in.ground ? in.ground->Origin()[1] : GroundMap::OriginFor(in.cameraPosition[1])};
    float shift[2] = {(origin[0] - sim.origin[0]) / kRippleCellSize, (origin[1] - sim.origin[1]) / kRippleCellSize};
    const long long now = Ticks();
    const double seconds = cfg.testSeconds >= 0.0f
                               ? (sim.lastTestSeconds >= 0.0f ? std::max(cfg.testSeconds - sim.lastTestSeconds, 0.0f) : 0.0)
                           : sim.lastTicks ? SecondsSince(sim.lastTicks)
                                           : 0.0;
    // A step is needed when the grid has moved (the shore's maps are read through its place), and otherwise only as often
    // as cooling and trails can show it, and not at all while none of the lava is in sight: the next step makes up the time.
    if (sim.valid && shift[0] == 0.0f && shift[1] == 0.0f && (m_lava.hidden || seconds < kLavaStirStepSeconds))
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        sim.advancedThisFrame = true;
        update.complete = true;
        return true;
    }
    sim.lastTestSeconds = cfg.testSeconds;
    sim.lastTicks = now;
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if (!sim.valid || std::fabs(shift[0]) >= kRippleCells || std::fabs(shift[1]) >= kRippleCells)
    {
        for (auto* target : {sim.ripples[0], sim.ripples[1]})
        {
            if (!SetTarget(dev, target) || FAILED(dev->SetViewport(&grid)) ||
                FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))
                return false;
        }
        shift[0] = shift[1] = 0.0f;
        sim.valid = true;
    }
    sim.origin[0] = origin[0];
    sim.origin[1] = origin[1];
    rippleArea[0] = origin[0];
    rippleArea[1] = origin[1];

    // Whatever crosses the surface, from the depth buffer: the last way across the grid, into a texture of its own.
    IDirect3DTexture9* seabed = in.ground ? GroundTexture(dev, in.ground) : nullptr;
    IDirect3DTexture9* contact = nullptr;
    // What stands still needs finding less often than the lava steps; the rise makes up the time since it was last looked for.
    m_lava.contactDue += seconds;
    float contactRise = 0.0f;
    if (m_snapshotValid && seabed && !m_lava.hidden && m_lava.contactDue >= kLavaContactSeconds)
    {
        contactRise = static_cast<float>(1.0 - std::exp(-m_lava.contactDue * kLavaContactRisePerSecond));
        m_lava.contactDue = 0.0;
        ++m_lava.work.contactSteps;
        if (!SpreadContactMarks(dev, level, seabed, frame, constants))
            return false;
        const float cell = 1.0f / kRippleCells;
        const float down[4] = {0.0f, cell, 1.0f, cell};
        if (!SetTarget(dev, m_contactSpread) || FAILED(dev->SetViewport(&grid)) ||
            FAILED(dev->SetPixelShaderConstantF(0, down, 1)) || !BindPointTexture(dev, 0, m_contactMarks) ||
            !BindPointTexture(dev, 1, nullptr) || !BindPointTexture(dev, 2, nullptr) || FAILED(DrawScreenTriangle(dev)))
            return false;
        contact = m_contactSpread;
    }

    // Only what is moving: something standing is found by the contact marks, and a pulse lasts one frame.
    float movers[kMaxMovers][4] = {};
    float radii[kMaxMovers][4] = {};
    int count = 0;
    for (int i = 0; i < in.moverCount; ++i)
    {
        const auto& mover = in.movers[i];
        if (!mover.continuous)
            continue;
        std::memcpy(movers[count], mover.position, sizeof(mover.position));
        radii[count][0] = mover.radius;
        ++count;
    }
    const float gridConstants[4] = {1.0f / kRippleCells, static_cast<float>(seconds), kRippleCellSize, 0.0f};
    const float originConstants[4] = {origin[0], origin[1], shift[0], shift[1]};
    const float countConstants[4] = {static_cast<float>(count), contactRise, 0.0f, 0.0f};
    const int next = sim.rippleIndex ^ 1;
    if (FAILED(BindPixelShader(dev, m_lava.stirPs)) || !SetTarget(dev, sim.ripples[next]) ||
        FAILED(dev->SetViewport(&grid)) || FAILED(dev->SetPixelShaderConstantF(0, gridConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(1, originConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(2, countConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(kRippleMoverRegister, &movers[0][0], kMaxMovers)) ||
        FAILED(dev->SetPixelShaderConstantF(kRippleRadiusRegister, &radii[0][0], kMaxMovers)) ||
        !BindPointTexture(dev, 0, sim.ripples[sim.rippleIndex]) || !BindPointTexture(dev, 1, contact) ||
        !BindPointTexture(dev, 2, level) || FAILED(DrawScreenTriangle(dev)))
        return false;
    sim.rippleIndex = next;
    sim.advancedThisFrame = true;
    update.complete = true;
    ++m_lava.work.stirSteps;
    return true;
}

bool Renderer::CompositeDeferredLava(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                     IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (!m_lava.surface.thisFrame)
        return false;
    m_lava.surface.thisFrame = false;
    ++m_lava.work.composites;
    m_lava.PollVisibility(dev);
    // The scene as it now stands: the models drawn since the lava was captured may stand in front of it.
    CompositeScene scene(*this);
    if (!scene.Capture(dev, depthTexture, boundDepth))
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, cfg.slimeStyle, m_lava.stir, frame, constants))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    bool prepared = SUCCEEDED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) &&
                    MeasureLevel(dev, m_lava.surface.depth, m_lava.surface.level, m_lava.surface.levelIndex);
    IDirect3DTexture9* level = m_lava.surface.level[0] ? m_lava.surface.level[m_lava.surface.levelIndex] : nullptr;
    prepared = prepared && m_lava.BakePatterns(*this,dev);
    TimeBegin(GpuTimer::LavaStir);
    prepared = prepared && AdvanceLavaStir(dev, level, in, cfg, frame, constants);
    TimeEnd(GpuTimer::LavaStir);
    TimeBegin(GpuTimer::LavaHalo);
    prepared = prepared && DrawLavaHalo(dev, desc, frame);
    TimeEnd(GpuTimer::LavaHalo);
    prepared = prepared && BindLiquidOptions(dev, cfg, false) && BindWaterControls(dev, cfg, false);
    if (!prepared)
    {
        saved.Restore();
        return false;
    }
    dev->SetRenderTarget(1, nullptr);
    dev->SetRenderTarget(0, saved.color[0]);
    WriteDepthAlways(dev, saved.color[0], boundDepth, in.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    bool bound = SUCCEEDED(BindPixelShader(dev, m_lava.surfacePs));
    bound = BindPointTexture(dev, 0, m_lava.surface.depth) && bound;
    bound = BindPointTexture(dev, 1, m_snapshot) && bound;
    bound = BindSky(dev, 5, cfg) && bound;
    bound = BindLinearTexture(dev, 6, m_lava.stir.Ripples()) && bound;
    bound = BindLinearTexture(dev, 7, GroundTexture(dev, in.ground)) && bound;
    bound = BindLinearTexture(dev, 8, in.ground ? m_shore : nullptr) && bound;
    const auto bindPattern = [&](DWORD stage, IDirect3DTexture9* texture, bool mipped) {
        bool ok = BindLinearTexture(dev, stage, texture);
        for (DWORD axis : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
            ok = SUCCEEDED(dev->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(axis), D3DTADDRESS_WRAP)) && ok;
        if (mipped)
        {
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR)) && ok;
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC)) && ok;
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, 4)) && ok;
        }
        return ok;
    };
    bound = bindPattern(9, m_lava.cells, true) && bound;
    bound = bindPattern(10, m_lava.noise, true) && bound;
    bound = bindPattern(11, m_lava.foam, true) && bound;
    bound = bindPattern(13, m_lava.flow, false) && bound;
    bound = BindPointTexture(dev, 12, m_lava.rand) && bound;
    bound = BindLinearTexture(dev, 14, m_lava.halo) && bound;
    bound = SUCCEEDED(dev->SetSamplerState(14, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR)) && bound;
    for (DWORD axis : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
        bound = SUCCEEDED(dev->SetSamplerState(12, static_cast<D3DSAMPLERSTATETYPE>(axis), D3DTADDRESS_WRAP)) && bound;
    TimeBegin(GpuTimer::LavaSurface);
    const bool counting = m_lava.visibleQuery && !m_lava.visiblePending && SUCCEEDED(m_lava.visibleQuery->Issue(D3DISSUE_BEGIN));
    const bool shaded = bound && SUCCEEDED(DrawScreenTriangle(dev));
    if (counting) m_lava.visiblePending = SUCCEEDED(m_lava.visibleQuery->Issue(D3DISSUE_END));
    TimeEnd(GpuTimer::LavaSurface);
    TimeBegin(GpuTimer::LavaGlow);
    const float farArea[4] = {in.farGround ? in.farGround->Origin()[0] : 0.0f, in.farGround ? in.farGround->Origin()[1] : 0.0f,
                              in.farGround ? 1.0f / in.farGround->Area() : 0.0f, 0.0f};
    const bool farBound = BindLinearTexture(dev, 4, FarGroundTexture(dev, in.farGround)) && SUCCEEDED(dev->SetPixelShaderConstantF(kFarAreaRegister, farArea, 1));
    const bool glowed = shaded && farBound && DrawLavaGlow(dev, desc, in.viewport, saved.color[0]);
    TimeEnd(GpuTimer::LavaGlow);
    const bool restored = saved.Restore();
    m_lava.drewThisFrame = shaded && restored;
    return glowed && restored;
}

// Where the lava lies on screen, for its border and its light on what stands beside it (lava_halo_ps).
bool Renderer::DrawLavaHalo(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4])
{
    const UINT width = std::max(desc.Width / kLavaHaloScale, 1u), height = std::max(desc.Height / kLavaHaloScale, 1u);
    if (!m_lava.EnsureHalo(dev,width,height)) return false;
    const D3DVIEWPORT9 halo = {0, 0, width, height, 0.0f, 1.0f};
    const float haloConstants[4] = {1.0f / width, static_cast<float>(kLavaHaloScale), 0.0f, 0.0f};
    const bool mixed = m_lava.ordinarySeen && m_lava.felSeen;
    if (!SetTarget(dev, m_lava.halo) || (mixed && (!SetTarget(dev, m_lava.haloColours, 1) ||
        FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, kColorOnly)))) || FAILED(dev->SetViewport(&halo)) || FAILED(BindPixelShader(dev, m_lava.haloPs)) ||
        FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) ||
        FAILED(dev->SetPixelShaderConstantF(9, haloConstants, 1)) || !BindPointTexture(dev, 0, m_lava.surface.depth) ||
        !BindPointTexture(dev, 1, m_snapshot) ||
        FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)) || FAILED(DrawScreenTriangle(dev)))
        return false;
    if (mixed) {
        if (FAILED(dev->SetRenderTarget(1, nullptr))) return false;
        m_lava.haloColours->GenerateMipSubLevels();
    }
    m_lava.halo->GenerateMipSubLevels();
    return true;
}

// The lava's light on what stands beside it (lava_glow_ps).
bool Renderer::DrawLavaGlow(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const D3DVIEWPORT9& viewport,
                            IDirect3DSurface9* target)
{
    const UINT width = std::max(desc.Width / kLavaHaloScale, 1u), height = std::max(desc.Height / kLavaHaloScale, 1u);
    float cameraToClip[4] = {};
    dev->GetPixelShaderConstantF(2, cameraToClip, 1);
    const float glowConstants[4] = {1.0f / width, 1.0f / height, static_cast<float>(kLavaHaloScale),
                                    0.5f * viewport.Height * cameraToClip[1]};
    const bool mixed = m_lava.ordinarySeen && m_lava.felSeen;
    const float palette[4] = {m_lava.felSeen ? 1.0f : 0.0f, mixed ? 1.0f : 0.0f, 0, 0};
    bool ok = SUCCEEDED(dev->SetRenderTarget(0, target)) && SUCCEEDED(dev->SetViewport(&viewport)) &&
              SUCCEEDED(dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE)) &&
              SUCCEEDED(dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) &&
              SUCCEEDED(BindPixelShader(dev, m_lava.glowPs)) &&
              SUCCEEDED(dev->SetPixelShaderConstantF(9, glowConstants, 1)) && BindPointTexture(dev, 0, m_lava.surface.depth) &&
              BindPointTexture(dev, 1, m_snapshot) && BindPointTexture(dev, 2, m_sceneBefore) &&
              BindLinearTexture(dev, 3, m_lava.halo) &&
              SUCCEEDED(dev->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    if (mixed)
        ok = ok && BindLinearTexture(dev, 5, m_lava.haloColours) &&
             SUCCEEDED(dev->SetSamplerState(5, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    return ok && SUCCEEDED(dev->SetPixelShaderConstantF(91, palette, 1)) && SUCCEEDED(DrawScreenTriangle(dev));
}

bool Renderer::BuildLavaEffectsMask(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                                    const FrameInputs& in, const Config& cfg, IDirect3DTexture9* hostMask)
{
    m_lava.maskReady = false;
    // (With none of it in sight, the host's own mask is the whole answer.)
    if (!m_lava.drewThisFrame || m_lava.hidden || !depthTexture)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    if (!m_lava.EnsureMask(dev,desc.Width,desc.Height)) return Skip("lava effects mask could not be created");
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    const bool drawn = SetTarget(dev, m_lava.mask) && SUCCEEDED(dev->SetViewport(&full)) &&
                       SUCCEEDED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) &&
                       SUCCEEDED(BindPixelShader(dev, m_lava.maskPs)) && BindPointTexture(dev, 0, m_lava.surface.depth) &&
                       BindPointTexture(dev, 1, depthTexture) && BindPointTexture(dev, 2, hostMask) && SUCCEEDED(DrawScreenTriangle(dev));
    const bool restored = saved.Restore();
    m_lava.maskReady = drawn && restored;
    return m_lava.maskReady;
}
