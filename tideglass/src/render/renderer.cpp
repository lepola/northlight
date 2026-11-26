// Shaders, shared targets and the frame itself: the snapshot before liquid, the water composite and the release paths.
// The other parts of Renderer are in renderer_*.cpp and lava_resources.cpp.
#include "render/renderer_internal.h"
#include "ps_ao.h"
#include "ps_ao_blur.h"
#include "ps_fxaa.h"
#include "ps_level.h"
#include "ps_overlay.h"
#include "ps_probe.h"
#include "ps_puddle.h"
#include "ps_reflection_scenery.h"
#include "ps_slime_reflection.h"
#include "ps_noise_lattice.h"
#include "ps_surface_detail.h"
#include "ps_wake_map.h"
#include "ps_fill_pull.h"
#include "ps_fill_push.h"
#include "ps_ring.h"
#include "ps_ripple.h"
#include "ps_sky_memory.h"
#include "ps_sky_observed_air.h"
#include "ps_sky_memory_extend.h"
#include "ps_slime_capture.h"
#include "ps_slime_deferred.h"
#include "ps_slime_reflection_view.h"
#include "ps_slime_deferred_fine.h"
#include "ps_slime_undo.h"
#include "ps_slime_lift.h"
#include "ps_slime_shore.h"
#include "ps_lava.h"
#include "ps_lava_stir.h"
#include "ps_lava_mask.h"
#include "ps_lava_halo.h"
#include "ps_lava_glow.h"
#include "ps_lava_bake_cells.h"
#include "ps_lava_bake_slope.h"
#include "ps_lava_bake_noise.h"
#include "ps_lava_bake_foam.h"
#include "ps_lava_bake_flow.h"
#include "ps_snapshot.h"
#include "ps_underwater.h"
#include "ps_underwater_foam.h"
#include "ps_underwater_shared.h"
#include "ps_underwater_scenery.h"
#include "ps_slime_underwater_scenery.h"
#include "ps_underwater_scene.h"
#include "ps_contact.h"
#include "ps_contact_spread.h"
#include "ps_water.h"
#include "ps_water_fine.h"
#include "ps_water_lamps.h"
#include "ps_water_moments.h"
#include "ps_water_moments_fine.h"
#include "ps_surface_detail_moments.h"
#include "vs_fullscreen.h"

using namespace render_detail;

bool Renderer::Skip(const char* reason)
{
    m_skip = reason;
    return false;
}

bool Renderer::EnsurePixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader)
{
    if (shader) return true;
    // Each resource belongs to this renderer/device. Only the selected pass is
    // created; a failed optional program stays unavailable until Reset.
    const struct Entry { IDirect3DPixelShader9** slot; const ShaderProgram* program; } entries[] = {
        {&m_snapshotPs, &p_ps_snapshot},
        {&m_overlayPs, &p_ps_overlay},
        {&m_probePs, &p_ps_probe},
        {&m_waterPs, &p_ps_water},
        {&m_waterLampPs, &p_ps_water_lamps},
        {&m_waterFinePs, &p_ps_water_fine},
        {&m_fxaaPs, &p_ps_fxaa},
        {&m_levelPs, &p_ps_level},
        {&m_skyMemoryPs, &p_ps_sky_memory},
        {&m_skyObservedAirPs, &p_ps_sky_observed_air},
        {&m_skyExtendPs, &p_ps_sky_memory_extend},
        {&m_underwaterPs, &p_ps_underwater},
        {&m_underwaterFoamPs, &p_ps_underwater_foam},
        {&m_underwaterSharedPs, &p_ps_underwater_shared},
        {&m_underwaterSceneryPs, &p_ps_underwater_scenery},
        {&m_slimeUnderwaterSceneryPs, &p_ps_slime_underwater_scenery},
        {&m_ripplePs, &p_ps_ripple},
        {&m_slimeCapturePs, &p_ps_slime_capture},
        {&m_slimeUndoPs, &p_ps_slime_undo},
        {&m_slimeLiftPs, &p_ps_slime_lift},
        {&m_slimeShorePs, &p_ps_slime_shore},
        {&m_slimeDeferredPs, &p_ps_slime_deferred},
        {&m_slimeReflectionViewPs, &p_ps_slime_reflection_view},
        {&m_slimeDeferredFinePs, &p_ps_slime_deferred_fine},
        {&m_lava.surfacePs, &p_ps_lava},
        {&m_lava.stirPs, &p_ps_lava_stir},
        {&m_lava.maskPs, &p_ps_lava_mask},
        {&m_lava.haloPs, &p_ps_lava_halo},
        {&m_lava.glowPs, &p_ps_lava_glow},
        {&m_lava.bakeCellsPs, &p_ps_lava_bake_cells},
        {&m_lava.bakeSlopePs, &p_ps_lava_bake_slope},
        {&m_lava.bakeNoisePs, &p_ps_lava_bake_noise},
        {&m_lava.bakeFoamPs, &p_ps_lava_bake_foam},
        {&m_lava.bakeFlowPs, &p_ps_lava_bake_flow},
        {&m_aoPs, &p_ps_ao},
        {&m_aoBlurPs, &p_ps_ao_blur},
        {&m_puddlePs, &p_ps_puddle},
        {&m_ringPs, &p_ps_ring},
        {&m_surfaceDetailPs, &p_ps_surface_detail},
        {&m_noiseLatticePs, &p_ps_noise_lattice},
        {&m_wakeMapPs, &p_ps_wake_map},
        {&m_fillPullPs, &p_ps_fill_pull},
        {&m_fillPushPs, &p_ps_fill_push},
        {&m_underwaterScenePs, &p_ps_underwater_scene},
        {&m_contactPs, &p_ps_contact},
        {&m_contactSpreadPs, &p_ps_contact_spread},
        {&m_waterMomentsPs, &p_ps_water_moments},
        {&m_waterMomentsFinePs, &p_ps_water_moments_fine},
        {&m_reflectionSceneryPs, &p_ps_reflection_scenery},
        {&m_slimeReflectionPs, &p_ps_slime_reflection},
        {&m_surfaceMomentsPs, &p_ps_surface_detail_moments},
    };
    for (const auto& entry : entries) {
        if (entry.slot != &shader) continue;
        if (std::find(m_failedShaders.begin(), m_failedShaders.end(), entry.program) != m_failedShaders.end()) return false;
        const HRESULT result = entry.program->Create(dev, &shader);
        if (SUCCEEDED(result)) return true;
        D3DCAPS9 caps = {}; dev->GetDeviceCaps(&caps);
        const auto& required = entry.program->requirements;
        WATER_LOG_ERROR("shader %s unavailable (0x%08lx): needs %u temporaries, %u sampler registers, %u slots; device pixel slots %lu; retry after Reset",
                        entry.program->name, static_cast<unsigned long>(result), required.temporaries,
                        required.samplerRegisters, required.instructionSlots, static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
        m_failedShaders.push_back(entry.program);
        return false;
    }
    WATER_LOG_ERROR("unregistered renderer shader resource");
    return false;
}

HRESULT Renderer::BindPixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader)
{
    return EnsurePixelShader(dev, shader) ? dev->SetPixelShader(shader) : D3DERR_NOTAVAILABLE;
}

HRESULT Renderer::BindHighlightShader(IDirect3DDevice9* dev, const Config& cfg, IDirect3DPixelShader9*& standard, IDirect3DPixelShader9*& fine)
{
    if (cfg.fineHighlights && cfg.specular > 0 && SUCCEEDED(BindPixelShader(dev, fine))) return D3D_OK;
    return BindPixelShader(dev, standard);
}

bool Renderer::EnsureShaders(IDirect3DDevice9* dev)
{
    if (m_vs && m_decl && m_snapshotPs) return true;
    const bool ok = (m_vs || SUCCEEDED(p_vs_fullscreen.Create(dev, &m_vs))) &&
                    (m_decl || SUCCEEDED(dev->CreateVertexDeclaration(kFullscreenVertex, &m_decl))) &&
                    EnsurePixelShader(dev, m_snapshotPs);
    return ok || Skip("shared fullscreen/depth shader unavailable");
}

bool Renderer::EnsureStateBlock(IDirect3DDevice9* dev)
{
    return EnsureScreenPassStateBlock(dev, m_state) || Skip("state block creation failed");
}

bool Renderer::EnsureSnapshotTarget(IDirect3DDevice9* dev, UINT width, UINT height)
{
    if (m_snapshot && m_snapshotWidth == width && m_snapshotHeight == height)
        return true;
    SafeRelease(m_snapshot);
    if (FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &m_snapshot,
                                  nullptr)))
        return Skip("depth snapshot target creation failed");
    m_snapshotWidth = width;
    m_snapshotHeight = height;
    return true;
}

bool Renderer::EnsureSceneTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (m_sceneBefore && m_sceneDesc.Width == desc.Width && m_sceneDesc.Height == desc.Height &&
        m_sceneDesc.Format == desc.Format) {
        m_sceneDesc = desc;
        return true;
    }
    SafeRelease(m_sceneBefore);
    SafeRelease(m_sceneWithWater);
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                  &m_sceneBefore, nullptr)) ||
        FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                  &m_sceneWithWater, nullptr)))
    {
        SafeRelease(m_sceneBefore);
        SafeRelease(m_sceneWithWater);
        m_sceneDesc = {};
        return Skip("scene copy targets could not be created");
    }
    m_sceneDesc = desc;
    return true;
}

bool Renderer::BuildFrameConstants(const FrameInputs& in, const Config& cfg, const D3DSURFACE_DESC& depthDesc,
                                   float (&constants)[9][4])
{
    const D3DVIEWPORT9& vp = in.viewport;
    if (vp.Width < 16 || vp.Height < 16 || vp.X + vp.Width > depthDesc.Width || vp.Y + vp.Height > depthDesc.Height)
        return Skip("world viewport outside the depth buffer");
    float viewToWorld[16];
    if (!ViewToWorld(in, viewToWorld))
        return Skip("view matrix not invertible");
    const float* proj = in.glProjection;
    const DepthMapping depth = MapDepth(proj, vp);
    const float frame[9][4] = {
        {static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width),
         static_cast<float>(vp.Height)},
        {1.0f / depthDesc.Width, 1.0f / depthDesc.Height, cfg.gridSpacing, cfg.overlayOpacity},
        {proj[0], proj[5], proj[8], proj[9]},
        {depth.atInfinity, depth.perInverseViewZ, depth.deepestWorldDepth, m_snapshotValid ? 1.0f : 0.0f},
        {},
        {},
        {},
        {},
        // w is always 1: the shaders' Unfolded() multiplies by it (see common.hlsli).
        {static_cast<float>(cfg.debugView), vp.X + vp.Width * 0.5f, vp.Y + vp.Height * 0.5f, 1.0f},
    };
    std::memcpy(constants, frame, sizeof(frame));
    std::memcpy(constants[4], viewToWorld, sizeof(viewToWorld));
    return true;
}

// In the liquid pass, water may already have been drawn; the depth just before each deferred liquid's draw tells its
// own pixels apart from it.
bool LiquidScreenArea(const FrameInputs& in, const float* bounds, RECT& area, int fadeRamp)
{
    if (!bounds || !in.viewport.Width || !in.viewport.Height) return false;
    for (unsigned c = 0; c < 3; ++c)
        if (!std::isfinite(bounds[c]) || !std::isfinite(bounds[c+3]) || bounds[c] > bounds[c+3]) return false;
    if (fadeRamp >= 0 && fadeRamp < 2 && in.nativeLiquids.baseKnown) {
        // Fade shading only contributes where the authored style has some slime. Split ambiguous boxes conservatively;
        // a tiny green fringe must not make a large, otherwise-water chunk shade the whole viewport. At most 32 leaves.
        area = {LONG(in.viewport.X), LONG(in.viewport.Y), LONG(in.viewport.X), LONG(in.viewport.Y)};
        bool bounded = true, any = false;
        auto include = [&](auto&& self, const float* box, unsigned depth) -> void {
            if (!bounded) return;
            const auto style = in.nativeLiquids.StyleAt(box, unsigned(fadeRamp));
            if (style == LiquidKind::Water) return;
            unsigned axis = 0;
            for (unsigned c = 1; c < 3; ++c) if (box[c+3]-box[c] > box[axis+3]-box[axis]) axis = c;
            if (style == LiquidKind::Fade && depth < 5 && box[axis+3]-box[axis] > 1.f) {
                float first[6], second[6];std::copy(box,box+6,first);std::copy(box,box+6,second);
                first[axis+3] = second[axis] = .5f*(box[axis]+box[axis+3]);
                self(self, first, depth+1);self(self, second, depth+1);
            } else {
                RECT part = {};
                if (!LiquidScreenArea(in,box,part)) { bounded = false;return; }
                if (part.left >= part.right || part.top >= part.bottom) return;
                if (!any) area = part;
                else {
                    area.left=std::min(area.left,part.left);area.top=std::min(area.top,part.top);
                    area.right=std::max(area.right,part.right);area.bottom=std::max(area.bottom,part.bottom);
                }
                any = true;
            }
        };
        include(include,bounds,0);
        return bounded;
    }
    double low[2] = {1e30, 1e30}, high[2] = {-1e30, -1e30};
    for (unsigned corner = 0; corner < 8; ++corner) {
        double p[4] = {}, view[4] = {}, clip[4] = {};
        for (unsigned c = 0; c < 3; ++c) p[c] = double(bounds[c + ((corner & (1u << c)) ? 3 : 0)]) - in.cameraPosition[c];
        // Like ViewToWorld and OwnedWorldGpu, use the captured rotation with the separately captured camera origin.
        // The client's view can retain a translation; applying that after subtracting cameraPosition moves the box twice.
        view[3] = 1;
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned r = 0; r < 3; ++r) view[c] += p[r] * in.cameraRelativeView[r*4+c];
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r) clip[c] += view[r] * in.glProjection[r*4+c];
        for (double value : clip) if (!std::isfinite(value)) return false;
        if (clip[3] <= 1e-6 || clip[2] < -clip[3]) return false;
        const double screen[2] = {in.viewport.X + (.5 + .5 * clip[0]/clip[3]) * in.viewport.Width,
                                  in.viewport.Y + (.5 - .5 * clip[1]/clip[3]) * in.viewport.Height};
        for (unsigned c = 0; c < 2; ++c) { low[c] = std::min(low[c],screen[c]); high[c] = std::max(high[c],screen[c]); }
    }
    const double left = in.viewport.X, top = in.viewport.Y;
    const double right = left + in.viewport.Width, bottom = top + in.viewport.Height;
    area = {LONG(std::clamp(std::floor(low[0])-1,left,right)), LONG(std::clamp(std::floor(low[1])-1,top,bottom)),
            LONG(std::clamp(std::ceil(high[0])+1,left,right)), LONG(std::clamp(std::ceil(high[1])+1,top,bottom))};
    return true;
}

// Everything the water composite reads, also used by its reflection pass.
bool Renderer::BindWaterTextures(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg, bool slime)
{
    bool ok=BindRingMap(dev);
    if (slime) {
        ok=BindPointTexture(dev,WATER_SLOT_sDepth,depthTexture)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sOpaqueDepth,m_snapshot)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sSceneBefore,m_sceneBefore)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sSceneWithWater,m_slime.stock)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sWaterLevel,m_slime.level[0]?m_slime.level[m_slime.levelIndex]:nullptr)&&ok;
        ok=BindSky(dev,WATER_SLOT_sRememberedSky,cfg)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sRipples,m_slimeSurface.Ripples())&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sSeabed,nullptr)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sShore,m_slimeShoreMeasured?m_slimeShore[0]:nullptr)&&ok;
        return BindPointTexture(dev,kBoatPathStage,nullptr)&&ok;
    }
    ok=BindPointTexture(dev,WATER_SLOT_sDepth,depthTexture)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sOpaqueDepth,m_snapshot)&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sSceneBefore,m_sceneBefore)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sSceneWithWater,m_sceneWithWater)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sWaterLevel,m_level[0]?m_level[m_levelIndex]:nullptr)&&ok;
    ok=BindSky(dev,WATER_SLOT_sRememberedSky,cfg,in.reflectedFogWeight)&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sRipples,m_waterSurface.Ripples())&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sSeabed,GroundTexture(dev,in.ground))&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sShore,in.ground?m_shore:nullptr)&&ok;
    return BindPointTexture(dev,kBoatPathStage,BoatPathTexture(dev,in))&&ok;
}

// Before a pass that works out the ships' wakes (the surface detail pass, the underwater view): the optimisations being
// tried, and each ship's box, drawing the wake map (and binding it) when it is on. Leaves the render targets, viewport,
// pixel shader, stages 10, 12 and 13 and the first constants changed.
bool Renderer::BindLiquidOptions(IDirect3DDevice9* dev, const Config& cfg, bool mapped)
{
    const float options[4] = {mapped ? 1.0f : 0.0f, cfg.reflectionSurfaceFlat ? 1.0f : 0.0f,
                              static_cast<float>(cfg.reflectionView), 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(kLiquidOptionsRegister, options, 1)))return false;
    const float reflectionOptions[2][4] = {
        {0.0f, 0.0f, cfg.reflectionScenery ? 1.0f : 0.0f, 0.0f},
        {0.0f, cfg.reflectionWarp, cfg.reflectionBendReach, 0.0f}};
    if(FAILED(dev->SetPixelShaderConstantF(kReflectionOptionsRegister, &reflectionOptions[0][0], 2)))return false;
    return true;
}

// The liquids' clock (seconds, wrapping every hour so it keeps its precision), or the harness's fixed time.
float Renderer::LiquidSeconds(const Config& cfg)
{
    if (cfg.testSeconds >= 0.0f)
        return cfg.testSeconds;
    if (m_startTicks == 0)
        m_startTicks = Ticks();
    return static_cast<float>(std::fmod(SecondsSince(m_startTicks), kTimeWrapSeconds));
}

bool Renderer::BeginPass(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, SavedTargets& saved)
{
    if (m_passRestoreFailed) return Skip("host state restoration failed earlier in this frame");
    if (dev->TestCooperativeLevel() != D3D_OK)
        return Skip("device not ready");
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev))
        return false;
    if (!saved.Capture(dev, m_state))
        return Skip("device state capture failed");
    if (saved.depth != boundDepth)
        return Skip("readable depth not bound");
    bool ready=true;
    for(DWORD i=1;i<4;++i)if(saved.color[i])ready=SUCCEEDED(dev->SetRenderTarget(i,nullptr))&&ready;
    ready=SetFullscreenState(dev)&&ready;
    if(!ready){saved.Restore();return Skip("fullscreen state setup failed");}
    return true;
}

bool Renderer::SetFullscreenState(IDirect3DDevice9* dev)
{
    return SetScreenPassState(dev,m_vs,m_decl);
}

bool Renderer::SnapshotBeforeLiquid(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                    IDirect3DSurface9* boundDepth, bool captureScene)
{
    m_snapshotValid = false;
    m_sceneValid = false;
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)) || !EnsureSnapshotTarget(dev, desc.Width, desc.Height))
        return false;
    IDirect3DSurface9* colour = nullptr;
    D3DSURFACE_DESC colourDesc = {};
    if (captureScene && SUCCEEDED(dev->GetRenderTarget(0,&colour)) &&
        SUCCEEDED(colour->GetDesc(&colourDesc)) && colourDesc.Width == desc.Width && colourDesc.Height == desc.Height)
        captureScene = EnsureSceneTargets(dev,colourDesc);
    else captureScene = false;
    SafeRelease(colour);
    m_snapshotValid = CopySceneDepth(dev,depthTexture,boundDepth,m_snapshot,captureScene ? m_sceneBefore : nullptr,m_sceneValid);
    if (m_snapshotValid) ++m_snapshots;
    return m_snapshotValid;
}

Renderer::CompositeScene::~CompositeScene()
{
    if (m_active) {
        std::swap(m_renderer.m_snapshot, m_renderer.m_compositeDepth);
        std::swap(m_renderer.m_sceneBefore, m_renderer.m_compositeScene);
    }
}

bool Renderer::CompositeScene::Capture(IDirect3DDevice9* dev, IDirect3DTexture9* depth,
                                       IDirect3DSurface9* boundDepth)
{
    auto& r = m_renderer;
    // A captured liquid always has a valid pre-liquid pair. Refuse a target transition rather than pairing
    // the old camera's opaque scene with a different-sized composite. The host starts a new capture there.
    D3DSURFACE_DESC z = {}, colour = {};
    IDirect3DSurface9* target = nullptr;
    const bool described = SUCCEEDED(dev->GetRenderTarget(0, &target)) && target &&
        SUCCEEDED(target->GetDesc(&colour)) && SUCCEEDED(boundDepth->GetDesc(&z));
    SafeRelease(target);
    if (m_active || !r.m_snapshotValid || !r.m_sceneValid || !described ||
        z.Width != r.m_snapshotWidth || z.Height != r.m_snapshotHeight ||
        colour.Width != z.Width || colour.Height != z.Height ||
        colour.Width != r.m_sceneDesc.Width || colour.Height != r.m_sceneDesc.Height ||
        colour.Format != r.m_sceneDesc.Format || colour.MultiSampleType != r.m_sceneDesc.MultiSampleType ||
        colour.MultiSampleQuality != r.m_sceneDesc.MultiSampleQuality)
        return r.Skip("deferred composite has no matching pre-liquid scene");
    if (r.m_compositeDesc.Width != colour.Width || r.m_compositeDesc.Height != colour.Height ||
        r.m_compositeDesc.Format != colour.Format) {
        SafeRelease(r.m_compositeDepth);
        SafeRelease(r.m_compositeScene);
        r.m_compositeDesc = {};
    }
    if (!r.m_compositeDepth && FAILED(dev->CreateTexture(z.Width, z.Height, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F, D3DPOOL_DEFAULT, &r.m_compositeDepth, nullptr)))
        return r.Skip("deferred composite depth creation failed");
    if (!r.m_compositeScene && FAILED(dev->CreateTexture(colour.Width, colour.Height, 1, D3DUSAGE_RENDERTARGET,
        colour.Format, D3DPOOL_DEFAULT, &r.m_compositeScene, nullptr)))
        return r.Skip("deferred composite colour creation failed");
    r.m_compositeDesc = colour;
    bool colourValid = false;
    if (!r.CopySceneDepth(dev, depth, boundDepth, r.m_compositeDepth, r.m_compositeScene, colourValid) || !colourValid)
        return false;
    // No extra full-screen copy: the existing composite snapshot is written to its own reusable targets.
    std::swap(r.m_snapshot, r.m_compositeDepth);
    std::swap(r.m_sceneBefore, r.m_compositeScene);
    ++r.m_snapshots;
    m_active = true;
    return true;
}

bool Renderer::CopySceneDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                             IDirect3DSurface9* boundDepth, IDirect3DTexture9* depthCopy,
                             IDirect3DTexture9* colourCopy, bool& colourValid)
{
    colourValid = false;
    D3DSURFACE_DESC desc = {};
    if (!depthCopy || FAILED(depthCopy->GetLevelDesc(0,&desc))) return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if (colourCopy) colourValid = CopyToTexture(dev,saved.color[0],colourCopy);
    if(!SetTarget(dev, depthCopy))return false;
    D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float texel[4] = {1.0f / desc.Width, 1.0f / desc.Height, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(1, texel, 1);
    if (FAILED(BindPixelShader(dev, m_snapshotPs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    const bool ok = SUCCEEDED(DrawScreenTriangle(dev));
    return saved.Restore() && ok;
}

// The scene already contains the visible native letters. Share the same read-only test
// between final surface shading and the world-space anti-aliasing pass.
static bool BindForegroundStencil(IDirect3DDevice9* dev, IDirect3DSurface9* depth, uint32_t bit)
{
    if(FAILED(dev->SetDepthStencilSurface(depth)))return false;
    const struct { D3DRENDERSTATETYPE state; DWORD value; } states[]={
        {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_STENCILENABLE,TRUE},
        {D3DRS_TWOSIDEDSTENCILMODE,FALSE},{D3DRS_STENCILFUNC,D3DCMP_NOTEQUAL},
        {D3DRS_STENCILREF,bit},{D3DRS_STENCILMASK,bit},{D3DRS_STENCILWRITEMASK,0},
        {D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP},{D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP},
        {D3DRS_STENCILPASS,D3DSTENCILOP_KEEP}};
    for(const auto& state:states)
        if(FAILED(dev->SetRenderState(state.state,state.value)))return false;
    return true;
}

bool Renderer::CompositeWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                              const FrameInputs& in, const Config& cfg, bool shadeSurface, IDirect3DTexture9* nativeWater,
                              IDirect3DTexture9* refractionScene, IDirect3DTexture9* surfaceVisibility, uint32_t stencilBit)
{
    m_distanceAtmosphereShaded=false;
    if(m_surfaceResult==SurfaceResult::AbortFrame)return Skip("water frame already abandoned after unrecoverable failure");
    m_surfaceResult=SurfaceResult::Unchanged;
    shadeSurface=shadeSurface&&cfg.surface;
    PrepareRainRings(dev,boundDepth,in,cfg);
    m_usingEnvironment=cfg.waterAtmosphere&&m_environment&&m_environment->Available();
    m_usingWaveMoments = false;
    struct SurfaceEnvironmentScope {bool& active;bool& moments;~SurfaceEnvironmentScope(){active=false;moments=false;}} environmentScope{m_usingEnvironment,m_usingWaveMoments};
    if (!m_snapshotValid || !m_sceneValid)
        return Skip("no scene snapshot before the liquid pass");
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    // A host's material lighting belongs in the refraction source, not on the
    // completed water. Borrow its matching copy for this call; preserve the
    // original scene for underwater, later liquids, failures and the next frame.
    struct RefractionColourScope {
        IDirect3DTexture9*& current; IDirect3DTexture9* original;
        ~RefractionColourScope() { current = original; }
    } colourScope{m_sceneBefore, m_sceneBefore};
    if (refractionScene) {
        D3DSURFACE_DESC supplied = {};
        const bool matching = SUCCEEDED(refractionScene->GetLevelDesc(0, &supplied)) &&
            supplied.Width == m_sceneDesc.Width && supplied.Height == m_sceneDesc.Height &&
            supplied.Format == m_sceneDesc.Format;
        if (!matching || refractionScene == m_sceneWithWater) return Skip("invalid host refraction scene");
        m_sceneBefore = refractionScene;
    }
    if (surfaceVisibility) {
        D3DSURFACE_DESC supplied = {};
        if (FAILED(surfaceVisibility->GetLevelDesc(0, &supplied)) ||
            supplied.Width != desc.Width || supplied.Height != desc.Height || supplied.Format != D3DFMT_G16R16F)
            return Skip("invalid host surface visibility");
    }
    m_environmentRayStep=2.f*(std::fabs(frame[4][2]/(frame[0][2]*frame[2][0]))+
                             std::fabs(frame[5][2]/(frame[0][3]*frame[2][1])));
    if(m_usingEnvironment&&!std::isfinite(m_environmentRayStep))return Skip("invalid water projection footprint");
    float constants[kWaterConstants][4];
    if (!BuildWaterConstants(in, cfg, Weathered(ConfigFile::WaterStyle(cfg), in, cfg), m_waterSurface, frame, constants))
        return false;

    // From below, the underwater view draws the surface itself from the scene captured before the liquid pass;
    // shading the client's surface as seen from above would only put a wrong layer over it.
    const bool seenFromBelow = cfg.underwater && in.cameraInWater;
    if (seenFromBelow) {
        // Northlight supplies its water-only mask; standalone reaches this boundary before later models.
        // Keep the actual liquid geometry for the underside, not the camera's single horizontal plane.
        m_nativeWater = nativeWater;
        if (!nativeWater) {
            D3DSURFACE_DESC have={};
            if (m_waterDepth && (FAILED(m_waterDepth->GetLevelDesc(0,&have)) || have.Width!=desc.Width || have.Height!=desc.Height))
                SafeRelease(m_waterDepth);
            if (!m_waterDepth && FAILED(dev->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,
                                                          D3DPOOL_DEFAULT,&m_waterDepth,nullptr)))
                return Skip("underwater liquid depth creation failed");
            bool unused=false;
            m_waterDepthValid=CopySceneDepth(dev,depthTexture,boundDepth,m_waterDepth,nullptr,unused);
            if (!m_waterDepthValid) return false;
        }
    }
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev,boundDepth,saved)) {
        if(m_passRestoreFailed)m_surfaceResult=SurfaceResult::AbortFrame;
        return false;
    }
    bool attempted=false,shaded=false;
    IDirect3DSurface9* rollback=nullptr;
    // The original native colour is retained until the surface transaction ends.
    // A failed draw may have written part of its target despite returning failure.
    auto finish=[&](bool success){
        IDirect3DSurface9* original=saved.color[0];if(original)original->AddRef();
        const bool restored=saved.Restore();
        bool recovered=true;
        if(attempted&&(!shaded||!restored)){
            IDirect3DSurface9* baseline=nullptr;
            recovered=original && (rollback || SUCCEEDED(m_sceneWithWater->GetSurfaceLevel(0,&baseline)));
            if(recovered)recovered=SUCCEEDED(dev->StretchRect(rollback?rollback:baseline,nullptr,original,nullptr,D3DTEXF_NONE));
            SafeRelease(baseline);
        }
        SafeRelease(original);
        if(!restored||!recovered)m_surfaceResult=SurfaceResult::AbortFrame;
        else if(shaded)m_surfaceResult=SurfaceResult::Shaded;
        else if(attempted)m_surfaceResult=SurfaceResult::Recovered;
        const bool surfaceOutput=cfg.reflectionView==0&&cfg.refractionDebug==0&&cfg.seabedDebug==0&&cfg.reflectionDebug==0;
        m_distanceAtmosphereShaded=success&&restored&&recovered&&shaded&&m_usingEnvironment&&surfaceOutput&&cfg.waterAtmosphere==1;
        return success&&restored&&recovered;
    };
    // Level tracking and simulation use depth. Only the above-water draw reads
    // native colour or needs it to recover from a partial write to the target.
    const bool drawSurface = shadeSurface && !seenFromBelow;
    bool ok = !drawSurface || CopyToTexture(dev,saved.color[0],m_sceneWithWater);
    if (ok)
    {
        TimeBegin(GpuTimer::WaterSurface);
        ok=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&
            MeasureLevel(dev,depthTexture,m_level,m_levelIndex)&&
            AdvanceSurface(dev,m_waterSurface,m_level[0]?m_level[m_levelIndex]:nullptr,WaterPhysics(cfg),in,cfg,frame,constants);
        TimeEnd(GpuTimer::WaterSurface);
    }
    if (ok && drawSurface)
    {
        BeginReflectionCapture(cfg, in);
        m_usingWaveMoments = FilteredWaterMaterial(cfg,m_usingEnvironment) && PrepareWaveMoments(dev,cfg);
        const bool controls=BindWaterControls(dev,cfg,m_usingEnvironment,m_usingWaveMoments);
        TimeBegin(GpuTimer::WaterDetail);
        m_surfaceDetailDrawn=controls&&PrepareShipWakes(dev,in,cfg,frame,constants)&&DrawSurfaceDetail(dev,depthTexture,in,cfg,frame,constants);
        TimeEnd(GpuTimer::WaterDetail);
        if(!m_surfaceDetailDrawn)return finish(false);
        bool hasIndoorWater=false;
        const bool reflections = cfg.screenReflections && DrawReflections(dev, depthTexture, in, cfg, frame, constants, false, &hasIndoorWater);
        if (cfg.reflectionScenery && cfg.screenReflections && !reflections)
        {
            m_candidateFailure = "reflection candidate unavailable; water surface comparison skipped";
            finish(false);
            return Skip(m_candidateFailure);
        }
        TimeBegin(GpuTimer::WaterUnderwaterScene);
        m_underwaterSceneDrawn = DrawUnderwaterScene(dev, depthTexture, in, cfg, frame, constants);
        TimeEnd(GpuTimer::WaterUnderwaterScene);
        const D3DVIEWPORT9 vp=FullDepth(in.viewport);
        // Optional refraction preparation can fail with a smaller MRT still bound.
        // Detach it before binding the native target for raw-refraction fallback.
        bool bound=SUCCEEDED(dev->SetRenderTarget(1,nullptr));
        bound=SUCCEEDED(dev->SetRenderTarget(0,saved.color[0]))&&bound;
        bound=SUCCEEDED(dev->SetViewport(&vp))&&bound;
        bound=SUCCEEDED(dev->SetRenderState(D3DRS_COLORWRITEENABLE,kColorOnly))&&bound;
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&bound;
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(kFrameConstants,&constants[0][0],kWaterConstants))&&bound;
        IDirect3DPixelShader9*& composite=m_usingWaveMoments ? m_waterMomentsPs : m_waterPs;
        IDirect3DPixelShader9*& fine=m_usingWaveMoments ? m_waterMomentsFinePs : m_waterFinePs;
        bound=SUCCEEDED(BindHighlightShader(dev,cfg,composite,fine))&&bound;
        bound=BindWaterControls(dev,cfg,m_usingEnvironment,m_usingWaveMoments,surfaceVisibility!=nullptr,reflections && hasIndoorWater)&&bound;
        bound=BindWaterTextures(dev,depthTexture,in,cfg)&&bound;
        bound=BindSurfaceDetail(dev)&&bound;
        // Composite normals/foam already come from surface detail: s9 no longer
        // reads rain rings here. The map is borrowed only for this draw.
        bound=BindPointTexture(dev,WATER_SLOT_sSurfaceVisibility,surfaceVisibility)&&bound;
        D3DSURFACE_DESC reflectionDesc = {};
        IDirect3DTexture9* const* result = m_reflections;
        const bool drawn = reflections && SUCCEEDED(result[0]->GetLevelDesc(0, &reflectionDesc));
        const float grid[4] = {static_cast<float>(reflectionDesc.Width), static_cast<float>(reflectionDesc.Height),
                               (drawn ? 1.0f : 0.0f) + (m_underwaterSceneDrawn ? 2.0f : 0.0f),
                               static_cast<float>(cfg.reflectionDebug)};
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(kReflectionRegister,grid,1))&&bound;
        bound=BindPointTexture(dev,kReflectionStage,drawn?result[0]:nullptr)&&bound;
        bound=BindPointTexture(dev,kReflectionHitStage,drawn?result[1]:nullptr)&&bound;
        bound=BindLinearTexture(dev,kUnderwaterSceneStage,m_underwaterSceneDrawn?m_underwaterFill.push[0][0]:nullptr)&&bound;
        bound=BindLinearTexture(dev,kUnderwaterThicknessStage,m_underwaterSceneDrawn?m_underwaterFill.push[0][1]:nullptr)&&bound;
        const std::string capture = m_reflectionCaptureDirectory.empty() ? "" : m_reflectionCaptureDirectory + "/composite";
        const bool captured = !capture.empty() && reflection::CapturePassInputs(dev, capture, m_snapshotPs,ActiveCaptureBurst());
        TimeBegin(GpuTimer::WaterComposite);
        if(bound && m_sceneDesc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            bound=screen_pass::SaveMultisampleColour(dev,saved.color[0],m_sceneDesc,m_multisampleRollback);
            rollback=m_multisampleRollback;
        }
        if(bound && stencilBit)bound=BindForegroundStencil(dev,boundDepth,stencilBit);
        if(bound){attempted=true;shaded=SUCCEEDED(DrawScreenTriangle(dev));}
        if(shaded && cfg.fineHighlights && cfg.specular>0 && in.lamps.positions[0][3]>0 &&
           !cfg.reflectionView && !cfg.refractionDebug && !cfg.seabedDebug && !cfg.reflectionDebug)
            shaded=DrawLampGlints(dev,in);
        TimeEnd(GpuTimer::WaterComposite);
        if(captured)reflection::CapturePassOutputs(dev,capture,"output",ActiveCaptureBurst());
        ok=bound&&shaded;
        if(shaded)++m_composites;
        if(!ok)Skip(bound?"water surface draw failed":"water surface bindings failed");
    }
    return finish(ok);
}

bool Renderer::DrawLampGlints(IDirect3DDevice9* dev,const FrameInputs& in)
{
    RECT area={};
    const RECT viewport={LONG(in.viewport.X),LONG(in.viewport.Y),LONG(in.viewport.X+in.viewport.Width),LONG(in.viewport.Y+in.viewport.Height)};
    for(const auto& p:in.lamps.positions){
        if(p[3]<=0)continue;
        const float bounds[]={p[0]-p[3],p[1]-p[3],p[2]-p[3],p[0]+p[3],p[1]+p[3],p[2]};
        RECT part={};
        if(!LiquidScreenArea(in,bounds,part))part=viewport;
        UnionRect(&area,&area,&part);
    }
    if(IsRectEmpty(&area) || FAILED(BindPixelShader(dev,m_waterLampPs)))return true;
    // The main surface has already shaded. Native depth and the resolved wave normal are still bound.
    const bool bound=SUCCEEDED(dev->SetPixelShaderConstantF(WATER_SLOT_cLampPositions,in.lamps.positions[0],4)) &&
           SUCCEEDED(dev->SetPixelShaderConstantF(WATER_SLOT_cLampColours,in.lamps.colours[0],4)) &&
           SUCCEEDED(dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE)) &&
           SUCCEEDED(dev->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD)) &&
           SUCCEEDED(dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE)) &&
           SUCCEEDED(dev->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
    // A refused optional binding leaves the completed base surface intact.
    return !bound || SUCCEEDED(DrawScreenArea(dev,&area));
}

bool Renderer::BuildWaterConstants(const FrameInputs& in, const Config& cfg, const LiquidStyle& style,
                                   const SurfaceSim& sim, const float (&frame)[9][4],
                                   float (&constants)[kWaterConstants][4])
{
    const float seconds = LiquidSeconds(cfg);
    const float lightScale = in.lightIsMoon ? kMoonLightScale : 1.0f;
    float viewToWorld[16];
    std::memset(constants,0,sizeof(constants));
    const auto row = [&](UINT slot) { return constants[slot-kFrameConstants]; };
    const auto set = [&](UINT slot,const std::array<float,4>& values) {
        std::copy(values.begin(),values.end(),row(slot));
    };
    set(WATER_SLOT_cWaterTimeWavesFoamClarity,{seconds, style.waveStrength, style.foamStrength, style.clarity});
    set(WATER_SLOT_cWaterReflectSpecTint,{style.reflection, cfg.specular, style.stockTint, cfg.screenReflections ? 1.0f : 0.0f});
    set(WATER_SLOT_cToLight,{in.toLight[0], in.toLight[1], in.toLight[2],
         lightScale * std::clamp((in.toLight[2] + kHorizonFadeBelow) / kHorizonFadeWidth, 0.0f, 1.0f)});
    set(WATER_SLOT_cFogRange,{in.fogStart, in.fogEnd, 1.0f - in.stormWeight, style.waveSpeed});
    set(WATER_SLOT_cShallowColor,{style.shallowColor[0], style.shallowColor[1], style.shallowColor[2], FarClipDistance(in.glProjection)});
    set(WATER_SLOT_cDeepColorDepth,{style.deepColor[0], style.deepColor[1], style.deepColor[2], style.gradientDepth});
    set(WATER_SLOT_cStyleExtras,{cfg.underwaterVisibility, style.foamDepth, style.stockRelief, in.daylight});
    set(WATER_SLOT_cFoamColor,{style.foamColor[0], style.foamColor[1], style.foamColor[2], style.foamGlow});
    set(WATER_SLOT_cInsideAndReflection,{style.surfaceTransmission, style.insideBrightness, style.nearReflection, cfg.reflectionRamp});
    set(WATER_SLOT_cRippleArea,{sim.origin[0], sim.origin[1], 1.0f / (kRippleCells * kRippleCellSize),
         cfg.ripples && sim.valid ? cfg.rippleStrength * style.rippleResponse : 0.0f});
    set(WATER_SLOT_cRippleGrid,{1.0f / kRippleCells, kRippleCellSize, cfg.scatter * style.rippleResponse, style.waveCrestShape});
    set(WATER_SLOT_cAbyss,{style.abyssColor[0], style.abyssColor[1], style.abyssColor[2], style.abyssDepth});
    set(kUnderwaterWindowRegister,{cfg.underwaterWindow, static_cast<float>(cfg.underwaterDebug), cfg.underwaterGlancingMirror, 0.0f});
    set(kTwilightRegister,{cfg.twilightShallowColor[0], cfg.twilightShallowColor[1], cfg.twilightShallowColor[2], in.goldenHour});
    set(WATER_SLOT_cGroundInfo,{static_cast<float>(cfg.seabedDebug), in.stockFogPushed ? 1.0f : 0.0f,
         in.ground && in.ground->NearbyWaterKnown() ? 1.0f : 0.0f, OvercastAmount(in, cfg)});
    set(WATER_SLOT_cSurface,{style.surfaceDeepness, in.lightIsMoon ? 1.0f : 0.0f, RainNow(in, cfg), cfg.rainRingStrength});
    set(WATER_SLOT_cLookingDown,{cfg.surfaceBlur, cfg.surfaceVeil, style.waveCrestSharpness, cfg.openWaterFoam});
    std::memcpy(viewToWorld, frame[WATER_SLOT_cViewToWorld0], sizeof(viewToWorld));
    if (!Invert4x4(viewToWorld, row(WATER_SLOT_cWorldToView0)))
        return Skip("world-to-view matrix not invertible");
    ColorToFloat(in.directColor, 1.0f, row(WATER_SLOT_cDirectColor));
    ColorToFloat(in.ambientColor, 1.0f, row(WATER_SLOT_cAmbientColor));
    ColorToFloat(in.fogColor, 1.0f, row(WATER_SLOT_cFogColor));
    if (in.lightIsMoon)
        std::memcpy(row(WATER_SLOT_cSunColor), kMoonTint, sizeof(kMoonTint));
    else
        ColorToFloat(in.sunColor, 1.0f, row(WATER_SLOT_cSunColor));
    if(m_usingEnvironment&&m_environment){
        const auto& source=m_environment->Values().light;
        for(unsigned i=0;i<3;++i){row(WATER_SLOT_cToLight)[i]=source.direction[i];row(WATER_SLOT_cSunColor)[i]=source.colour[i];}
        row(WATER_SLOT_cToLight)[3]=source.visibility;row(WATER_SLOT_cFogRange)[2]=1; // visibility already includes weather once
        row(WATER_SLOT_cSurface)[1]=source.moon?1.f:0.f;
    }
    // Ships for their wakes (rows c34..c41: position and speed, then heading and hull size; their paths are in
    // BoatPathTexture).
    for (int i = 0; i < in.boatCount && i < kMaxBoats; ++i)
    {
        const Boat& boat = in.boats[i];
        float* where = row(WATER_SLOT_cBoat+i);
        float* heading = row(WATER_SLOT_cBoatShape+i);
        where[0] = boat.position[0];
        where[1] = boat.position[1];
        where[2] = boat.position[2];
        where[3] = boat.speed;
        heading[0] = boat.direction[0];
        heading[1] = boat.direction[1];
        heading[2] = kHullHalfLength;
        heading[3] = kHullHalfBeam;
    }
    return true;
}

bool Renderer::Render(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                      const FrameInputs& in, const Config& cfg, uint32_t stencilBit)
{
    if (!cfg.underwater || in.mapId != m_airMap || in.weatherType != m_airWeather)
        m_airMap = -1;
    if (cfg.reflectionCapture <= 0 || cfg.reflectionCapture != m_reflectionCaptureRequest) EndReflectionCapture(true);
    const char* candidateFailure = m_candidateFailure;
    m_candidateFailure = nullptr;
    m_skip = "";
    ++m_frame;
    bool captureNow = m_captureLeft > 0;
    if (!captureNow && cfg.captureFrame > 0 && cfg.captureFrame != m_capturedRequest)
    {
        captureNow = m_pendingCapture == cfg.captureFrame;
        m_pendingCapture = cfg.captureFrame;
    }
    const D3DVIEWPORT9& vp = in.viewport;
    const float* proj = in.glProjection;
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)))
        return Skip("depth description failed");
    if (m_lastSummary == 0 || SecondsSince(m_lastSummary) > kSummarySeconds)
    {
        m_lastSummary = Ticks();
        WATER_LOG_INFO("frame %u: surface %s, %s view; viewport %lu,%lu %lux%lu of %ux%u depth %.4f..%.4f; near %.3f "
                       "far %.1f; camera (%.2f %.2f %.2f)%s",
                       m_frame, cfg.surface ? "on" : "off", DebugViewName(cfg.debugView), vp.X, vp.Y, vp.Width,
                       vp.Height, desc.Width, desc.Height, vp.MinZ, vp.MaxZ, -proj[14] / (1.0f + proj[10]),
                       proj[14] / (1.0f - proj[10]), in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2],
                       in.cameraInWater ? " under water" : "");
        WATER_LOG_INFO("passes: %u depth snapshots, %u water composites, %u slime composites, %u underwater views, %u "
                       "ambient occlusion, %u puddles, %u end-of-world; rain: weather %u at %.2f, rain %.2f, puddles "
                       "%.2f full, %u rings",
                       m_snapshots, m_composites, m_slimeComposites, m_underwaterFrames, m_groundEffects.occlusion,
                       m_groundEffects.puddles, m_rendered, in.weatherType, in.weatherIntensity, in.rain, m_wetness,
                       m_ringCount);
        WATER_LOG_INFO("lava: %u captures, %u composites, %u stir steps, %u contact steps%s", m_lava.work.captures,
                       m_lava.work.composites, m_lava.work.stirSteps, m_lava.work.contactSteps, m_lava.hidden ? ", out of sight" : "");
        WATER_LOG_INFO("light (%.2f %.2f %.2f) %s: direct %08X sun %08X ambient %08X; fog %08X %.0f..%.0f, storm %.2f",
                       in.toLight[0], in.toLight[1], in.toLight[2], in.lightIsMoon ? "moon" : "sun", in.directColor,
                       in.sunColor, in.ambientColor, in.fogColor, in.fogStart, in.fogEnd, in.stormWeight);
    }
    const bool overlay = cfg.debugView != DebugView::Off;
    const bool underwater = (cfg.underwater && in.cameraInWater) || (cfg.slime && in.cameraInSlime);
    // (With water this frame, the sky is remembered for its reflections whatever else is drawn: going no further without
    // anti-aliasing, the sky memory never changed.)
    if (!overlay && !cfg.antiAliasing && !underwater && !m_snapshotValid)
    {
        if (captureNow)
            Capture(dev, cfg.captureFrame, cfg.captureFrames);
        return Skip(candidateFailure ? candidateFailure : "nothing to draw at the end of the world pass");
    }
    float constants[9][4];
    if (!BuildFrameConstants(in, cfg, desc, constants))
        return false;

    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    IDirect3DSurface9* target = saved.color[0];
    D3DSURFACE_DESC targetDesc = {};
    if (m_snapshotValid && SUCCEEDED(target->GetDesc(&targetDesc)) && EnsureSceneTargets(dev, targetDesc) &&
        CopyToTexture(dev, target, m_sceneWithWater))
    {
        dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
        const float toLight[4] = {in.toLight[0], in.toLight[1], in.toLight[2], 0.0f};
        dev->SetPixelShaderConstantF(kToLightRegister, toLight, 1);
        float fog[4];
        ColorToFloat(in.fogColor, 1.0f, fog);
        dev->SetPixelShaderConstantF(kFogColorRegister, fog, 1);
        if(cfg.waterWorkAudit)TimeBegin(GpuTimer::SkyMemory);
        // Looking down to dive must not erase the sky just seen above the bank.
        const float rightZ = constants[4][2] / proj[0], upZ = constants[5][2] / proj[5];
        const float highestRay = constants[6][2] - rightZ * proj[8] - upZ * proj[9] + std::abs(rightZ) + std::abs(upZ);
        RememberSky(dev, depthTexture, m_sceneWithWater, in, cfg, highestRay > 0);
        if(cfg.waterWorkAudit)TimeEnd(GpuTimer::SkyMemory);
        dev->SetRenderTarget(0, target);
    }
    if (underwater)
        DrawUnderwater(dev, depthTexture, target, in, cfg, constants);
    if (cfg.antiAliasing)
        Antialias(dev, target, boundDepth, stencilBit, in, constants);
    if (overlay)
    {
        DrawOverlay(dev, depthTexture, target, in, cfg, constants);
        if (m_lastProbe == 0 || SecondsSince(m_lastProbe) > kProbeSeconds)
            Probe(dev, depthTexture, in, constants);
    }
    if (!saved.Restore()) return Skip("world pass state restoration failed");
    if (captureNow)
        Capture(dev, cfg.captureFrame, cfg.captureFrames);
    // (Only the sky remembered: nothing drawn on the frame.)
    if (!overlay && !cfg.antiAliasing && !underwater)
        return Skip(candidateFailure ? candidateFailure : "nothing to draw at the end of the world pass");
    ++m_rendered;
    return candidateFailure ? Skip(candidateFailure) : true;
}

void Renderer::Antialias(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* boundDepth, uint32_t stencilBit, const FrameInputs& in,
                         const float (&constants)[9][4])
{
    D3DSURFACE_DESC desc = {};
    if (FAILED(target->GetDesc(&desc)) || !EnsureSceneTargets(dev, desc) ||
        !CopyToTexture(dev, target, m_sceneWithWater))
        return;
    dev->SetRenderTarget(0, target);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_fxaaPs))) return;
    if (!stencilBit || BindForegroundStencil(dev,boundDepth,stencilBit)){
        BindLinearTexture(dev, 2, m_sceneWithWater);
        DrawScreenTriangle(dev);
    }
    if(stencilBit){
        dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        dev->SetDepthStencilSurface(nullptr);
    }
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
}

void Renderer::ReleaseDefaultPool()
{
    m_distanceAtmosphereShaded=false;
    m_surfaceResult=SurfaceResult::Unchanged;
    SetScenery(nullptr,nullptr,0,nullptr);
    m_candidateFailure = nullptr;
    EndReflectionCapture(true);
    m_reflectionCaptureRemaining = 0;
    SafeRelease(m_snapshot);
    SafeRelease(m_sceneBefore);
    SafeRelease(m_sceneWithWater);
    SafeRelease(m_multisampleRollback);
    SafeRelease(m_compositeDepth);
    SafeRelease(m_compositeScene);
    m_compositeDesc = {};
    m_sceneDesc = {};
    m_sceneValid = false;
    SafeRelease(m_probeTarget);
    SafeRelease(m_level[0]);
    SafeRelease(m_level[1]);
    SafeRelease(m_sky[0]);
    SafeRelease(m_sky[1]);
    SafeRelease(m_observedAir);
    m_airMap = -1;
    ReleaseDeferred(m_slime);
    SafeRelease(m_slimeGround);
    for (auto*& map : m_slimeShore) SafeRelease(map);
    m_slimeShoreMeasured = false;
    m_lava.ReleaseDefaultPool();
    SafeRelease(m_surfaceBaseDepth);
    SafeRelease(m_waterDepth);m_nativeWater=nullptr;m_waterDepthValid=m_slimeCaptured=false;
    SafeRelease(m_occlusion[0]);
    SafeRelease(m_occlusion[1]);
    m_occlusionWidth = 0;
    m_occlusionHeight = 0;
    SafeRelease(m_ringMap);
    m_ringMapReach = 0.0f;
    SafeRelease(m_contactMarks);
    SafeRelease(m_contactSpread);
    SafeRelease(m_surfaceDetail[0]);
    SafeRelease(m_surfaceDetail[1]);
    SafeRelease(m_noiseLattice[0]);
    SafeRelease(m_noiseLattice[1]);
    m_surfaceDetailDrawn = false;
    m_usingWaveMoments = false;
    m_waveMomentsFailed = false;
    m_ringShaderFailed = false;
    m_failedShaders.clear();
    SafeRelease(m_wakeMap[0]);
    SafeRelease(m_wakeMap[1]);
    SafeRelease(m_reflections[0]);
    SafeRelease(m_reflections[1]);
    ReleaseFillChain(m_underwaterFill);
    m_underwaterSceneDrawn = false;
    ReleaseSurface(m_waterSurface);
    ReleaseSurface(m_slimeSurface);
    m_slime.thisFrame = false;
    SafeRelease(m_state);
    m_snapshotWidth = 0;
    m_snapshotHeight = 0;
    m_snapshotValid = false;
}

void Renderer::ReleaseAll()
{
    ReleaseDefaultPool();
    SafeRelease(m_probeReadback);
    SafeRelease(m_vs);
    SafeRelease(m_snapshotPs);
    SafeRelease(m_overlayPs);
    SafeRelease(m_probePs);
    SafeRelease(m_waterPs);
    SafeRelease(m_waterLampPs);
    SafeRelease(m_waterFinePs);
    SafeRelease(m_waterMomentsPs);
    SafeRelease(m_waterMomentsFinePs);
    SafeRelease(m_surfaceMomentsPs);
    SafeRelease(m_fxaaPs);
    SafeRelease(m_levelPs);
    SafeRelease(m_skyMemoryPs); SafeRelease(m_skyObservedAirPs); SafeRelease(m_skyExtendPs);
    SafeRelease(m_underwaterPs);
    SafeRelease(m_underwaterFoamPs);
    SafeRelease(m_underwaterSharedPs);
    SafeRelease(m_underwaterSceneryPs);
    SafeRelease(m_slimeUnderwaterSceneryPs);
    SafeRelease(m_ripplePs);
    SafeRelease(m_ground);
    SafeRelease(m_shore);
    SafeRelease(m_farGround);
    m_farGroundSource = 0;
    m_farGroundRevision = 0;
    SafeRelease(m_boatPaths);
    m_groundSource = 0;
    SafeRelease(m_slimeCapturePs);
    SafeRelease(m_slimeLiftPs);
    SafeRelease(m_slimeShorePs);
    SafeRelease(m_slimeUndoPs);
    SafeRelease(m_slimeDeferredPs);
    SafeRelease(m_slimeReflectionViewPs);
    SafeRelease(m_slimeDeferredFinePs);
    m_lava.ReleaseManaged();
    SafeRelease(m_aoPs);
    SafeRelease(m_aoBlurPs);
    SafeRelease(m_puddlePs);
    SafeRelease(m_ringPs);
    SafeRelease(m_reflectionSceneryPs);
    SafeRelease(m_slimeReflectionPs);
    SafeRelease(m_surfaceDetailPs);
    SafeRelease(m_noiseLatticePs);
    SafeRelease(m_wakeMapPs);
    SafeRelease(m_fillPullPs);
    SafeRelease(m_fillPushPs);
    SafeRelease(m_underwaterScenePs);
    SafeRelease(m_contactPs);
    SafeRelease(m_contactSpreadPs);
    SafeRelease(m_ringVs);
    SafeRelease(m_ringDecl);
    SafeRelease(m_decl);
}
