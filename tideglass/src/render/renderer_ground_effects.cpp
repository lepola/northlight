// What is drawn on the ground rather than on a liquid: ambient occlusion, rain rings and puddles.
#include "render/renderer_internal.h"
#include "vs_ring.h"

using namespace render_detail;

bool Renderer::EnsureOcclusionTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& depth)
{
    if (m_occlusion[0] && m_occlusionWidth == depth.Width && m_occlusionHeight == depth.Height)
        return true;
    SafeRelease(m_occlusion[0]);
    SafeRelease(m_occlusion[1]);
    for (auto*& target : m_occlusion)
        if (FAILED(dev->CreateTexture(depth.Width, depth.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F,
                                      D3DPOOL_DEFAULT, &target, nullptr)))
        {
            SafeRelease(m_occlusion[0]);
            SafeRelease(m_occlusion[1]);
            m_occlusionWidth = 0;
            m_occlusionHeight = 0;
            return Skip("ambient occlusion targets could not be created");
        }
    m_occlusionWidth = depth.Width;
    m_occlusionHeight = depth.Height;
    return true;
}

bool Renderer::AmbientOcclusion(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                                const FrameInputs& in, const Config& cfg)
{
    if (cfg.ambientOcclusion <= 0.0f)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !EnsureOcclusionTargets(dev, desc))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    const float pixelsPerYard = 0.5f * vp.Height * in.glProjection[5];
    const float occlusion[2][4] = {
        {cfg.ambientOcclusionRadius, cfg.ambientOcclusionDistance * kOcclusionFadeStart, cfg.ambientOcclusionDistance,
         pixelsPerYard},
        {in.fogStart, in.fogEnd, cfg.ambientOcclusion, 0.0f},
    };
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kOcclusionRegister, &occlusion[0][0], 2);
    BindPointTexture(dev, 0, depthTexture);
    if(!SetTarget(dev, m_occlusion[0]))return false;
    dev->SetViewport(&vp);
    if (FAILED(BindPixelShader(dev, m_aoPs))) return false;
    DrawScreenTriangle(dev);

    const float across[4] = {1.0f, 0.0f, kOcclusionBlurDepthTolerance, 0.0f};
    const float down[4] = {0.0f, 1.0f, kOcclusionBlurDepthTolerance, 1.0f};
    if (FAILED(BindPixelShader(dev, m_aoBlurPs))) return false;
    if(!SetTarget(dev, m_occlusion[1]))return false;
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(kOcclusionRegister, across, 1);
    BindPointTexture(dev, 2, m_occlusion[0]);
    DrawScreenTriangle(dev);

    // The last run shades a copy of the scene (the scratch copy, taken afresh wherever else it is used) back onto it.
    D3DSURFACE_DESC colorDesc = {};
    if (FAILED(saved.color[0]->GetDesc(&colorDesc)) || !EnsureSceneTargets(dev, colorDesc) ||
        !CopyToTexture(dev, saved.color[0], m_sceneWithWater))
    {
        saved.Restore();
        return Skip("scene copy for the ambient occlusion failed");
    }
    const float shade[4] = {cfg.ambientOcclusionColor[0], cfg.ambientOcclusionColor[1], cfg.ambientOcclusionColor[2],
                            0.0f};
    dev->SetRenderTarget(0, saved.color[0]);
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(kOcclusionRegister, down, 1);
    dev->SetPixelShaderConstantF(kOcclusionRegister + 1, shade, 1);
    BindPointTexture(dev, 2, m_occlusion[1]);
    BindPointTexture(dev, 3, m_sceneWithWater);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    const bool drawn=SUCCEEDED(DrawScreenTriangle(dev));
    if (!saved.Restore() || !drawn) return false;
    ++m_groundEffects.occlusion;
    return true;
}

bool Renderer::EnsureRingMap(IDirect3DDevice9* dev)
{
    if (m_ringShaderFailed) return false;
    if ((!m_ringVs && FAILED(p_vs_ring.Create(dev, &m_ringVs))) ||
        (!m_ringDecl && FAILED(dev->CreateVertexDeclaration(kRingVertex, &m_ringDecl)))) {
        m_ringShaderFailed = true;
        return false;
    }

    if (m_ringMap)
        return true;
    if (FAILED(dev->CreateTexture(kRingMapCells, kRingMapCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F,
                                  D3DPOOL_DEFAULT, &m_ringMap, nullptr)))
        return Skip("ring map could not be created");
    return true;
}

namespace
{
// Whether a splash lands on water or ground there (by the ground map), rather than on something standing above it.
bool LandsOnSurface(const GroundMap* ground, const Splash& splash)
{
    if (!ground)
        return true;
    const int x = static_cast<int>((splash.x - ground->Origin()[0]) / GroundMap::kCellSize);
    const int y = static_cast<int>((splash.y - ground->Origin()[1]) / GroundMap::kCellSize);
    if (x < 0 || y < 0 || x >= GroundMap::kCells || y >= GroundMap::kCells)
        return true;
    const float land = ground->Heights()[y * GroundMap::kCells + x];
    const float water = ground->LiquidHeights()[y * GroundMap::kCells + x];
    if (land <= GroundMap::kUnknown && water <= GroundMap::kUnknown)
        return true;
    return std::fabs(splash.z - land) < kRingSurfaceHeight || std::fabs(splash.z - water) < kRingSurfaceHeight;
}

// A splash's lasting pick for a ring (the same splash is always picked or left), 0..1.
float SplashPick(const Splash& splash)
{
    const float h = std::sin(splash.x * 12.9898f + splash.y * 78.233f) * 43758.5453f;
    return h - std::floor(h);
}
}

bool Renderer::DrawRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in,
                             const Config& cfg)
{
    m_ringFrame = m_worldFrame;
    m_ringMapReach = 0.0f;
    if (in.splashCount <= 0 || !in.splashes || cfg.rainRings <= 0.0f || !EnsureRingMap(dev))
        return false;
    const float cell = kRingMapArea / kRingMapCells;
    m_ringMapOrigin[0] = std::floor((in.cameraPosition[0] - kRingMapArea * 0.5f) / cell) * cell;
    m_ringMapOrigin[1] = std::floor((in.cameraPosition[1] - kRingMapArea * 0.5f) / cell) * cell;
    struct RingVertex
    {
        float position[4];
        float ring[4];
    };
    static std::vector<RingVertex> vertices;
    vertices.clear();
    const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
    for (int i = 0; i < in.splashCount; ++i)
    {
        const Splash& splash = in.splashes[i];
        if (SplashPick(splash) >= cfg.rainRings || !LandsOnSurface(in.ground, splash))
            continue;
        const float phase = std::clamp(splash.age / kSplashRingSeconds, 0.0f, 1.0f);
        for (const auto& corner : corners)
        {
            const float x = splash.x + corner[0] * kRingExtent;
            const float y = splash.y + corner[1] * kRingExtent;
            vertices.push_back({{(x - m_ringMapOrigin[0]) / kRingMapArea * 2.0f - 1.0f,
                                 1.0f - (y - m_ringMapOrigin[1]) / kRingMapArea * 2.0f, 0.0f, 1.0f},
                                {corner[0] * kRingExtent, corner[1] * kRingExtent, phase, 0.0f}});
        }
    }
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if(!SetTarget(dev, m_ringMap))return false;
    D3DVIEWPORT9 vp = {0, 0, kRingMapCells, kRingMapCells, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    if (!vertices.empty())
    {
        dev->SetVertexShader(m_ringVs);
        dev->SetVertexDeclaration(m_ringDecl);
        if (FAILED(BindPixelShader(dev, m_ringPs))) return false;
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        if (FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size() / 3), vertices.data(),
                                        sizeof(RingVertex)))) return false;
    }
    if (!saved.Restore()) return false;
    m_ringMapReach = in.splashReach;
    m_ringCount = static_cast<unsigned>(vertices.size() / 6);
    return true;
}

// The ring map for the rain rings on the puddles and the water (see water_common.hlsli); its reach is 0 when it has
// no rings this frame.
void Renderer::PrepareRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (m_ringFrame == m_worldFrame)
        return;
    TimeBegin(GpuTimer::RainRings);
    DrawRainRings(dev, boundDepth, in, cfg);
    TimeEnd(GpuTimer::RainRings);
}

bool Renderer::BindRingMap(IDirect3DDevice9* dev)
{
    const float ringMap[4]={m_ringMapOrigin[0],m_ringMapOrigin[1],1.f/kRingMapArea,m_ringMapReach};
    const bool constants=SUCCEEDED(dev->SetPixelShaderConstantF(kRingMapRegister,ringMap,1));
    return BindLinearTexture(dev,kRingMapStage,m_ringMapReach>0?m_ringMap:nullptr)&&constants;
}

// How hard it rains now (0..1): the game's weather, or the test rain when one is set.
float Renderer::RainNow(const FrameInputs& in, const Config& cfg)
{
    return cfg.testRain >= 0.0f ? cfg.testRain : std::clamp(in.rain, 0.0f, 1.0f);
}

bool Renderer::Puddles(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                       const FrameInputs& in, const Config& cfg)
{
    PrepareRainRings(dev, boundDepth, in, cfg);
    const float rain = RainNow(in, cfg);
    const double elapsed = m_wetnessTicks ? std::min(SecondsSince(m_wetnessTicks), kLongestWetnessStep) : 0.0;
    m_wetnessTicks = Ticks();
    const float change = rain > 0.0f ? static_cast<float>(elapsed) * rain / cfg.puddleFillSeconds
                                     : -static_cast<float>(elapsed) / cfg.puddleDrySeconds;
    m_wetness = std::clamp(m_wetness + change, 0.0f, 1.0f);
    if (cfg.puddles <= 0.0f || (m_wetness <= 0.0f && rain <= 0.0f) || !in.ground || in.cameraInWater)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, ConfigFile::WaterStyle(cfg), m_waterSurface, frame, constants))
        return false;
    IDirect3DTexture9* ground = GroundTexture(dev, in.ground);
    if (!ground)
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    D3DSURFACE_DESC colorDesc = {};
    if (FAILED(saved.color[0]->GetDesc(&colorDesc)) || !EnsureSceneTargets(dev, colorDesc) ||
        !CopyToTexture(dev, saved.color[0], m_sceneWithWater))
    {
        saved.Restore();
        return Skip("scene copy for the puddles failed");
    }
    const float puddles[kPuddleConstants][4] = {
        {m_wetness, rain, cfg.puddles, cfg.rainRingStrength},
        {in.ground->Origin()[0], in.ground->Origin()[1], 1.0f / GroundMap::kArea, GroundMap::kCellSize},
    };
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetRenderTarget(0, saved.color[0]);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    dev->SetPixelShaderConstantF(kPuddleRegister, &puddles[0][0], kPuddleConstants);
    BindRingMap(dev);
    if (FAILED(BindPixelShader(dev, m_puddlePs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, depthTexture);
    BindLinearTexture(dev, 2, m_sceneWithWater);
    BindSky(dev, 5, cfg);
    BindLinearTexture(dev, 7, ground);
    const bool drawn=SUCCEEDED(DrawScreenTriangle(dev));
    if (!saved.Restore() || !drawn) return false;
    ++m_groundEffects.puddles;
    return true;
}
