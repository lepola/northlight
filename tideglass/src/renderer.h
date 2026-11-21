#pragma once

#include "config.h"
#include "frame_inputs.h"
#include "screen_pass.h"
#include "surface_environment.h"
#include "ground.h"
#include "native_liquid.h"
#include "reflection_capture.h"
#include "scenery_views.h"

#include <d3d9.h>

#include <string>
#include <vector>
#include <array>

constexpr UINT kWaterConstants = 33;

class GpuTimer;
struct ShaderProgram;

// The part of the world viewport a liquid draw's box (low xyz, high xyz) covers, padded by a pixel; false when it cannot
// be bounded (no box, or a corner at or behind the camera), and the whole viewport serves.
bool LiquidScreenArea(const FrameInputs& in, const float* bounds, RECT& area, int fadeRamp = -1);

class Renderer
{
public:
    ~Renderer() { ReleaseAll(); }

    // A copied scene or simulation update is not proof of shaded water. Hosts
    // must also restore their outer state before publishing atmospheric ownership.
    enum class SurfaceResult { Unchanged, Shaded, Recovered, AbortFrame };
    SurfaceResult LastSurfaceResult() const { return m_surfaceResult; }
    void SetEnvironment(const water::SurfaceEnvironment* environment){m_environment=environment;}
    static WaterEnvironmentValues NativeEnvironment(const FrameInputs& in);
    bool DistanceAtmosphereShaded()const{return m_distanceAtmosphereShaded;}

    void PrepareUnderwaterShaders(IDirect3DDevice9* device, const Config& cfg);

    // Where the water's parts are timed (none: not timed).
    void SetTimer(GpuTimer* timer) { m_timer = timer; }
    void SetOutputDirectory(const std::string& directory) { m_outputDirectory = directory; }

    struct GroundEffectCounts { unsigned occlusion=0, puddles=0; };
    const GroundEffectCounts& GroundEffectsDrawn() const { return m_groundEffects; }
    const std::array<unsigned,9>& WorkCounts() const{return m_work;}
    // The liquid snapshot belongs to the frame whose liquid took it; the end-of-world pass also runs on frames with no
    // liquid in view (the underwater view when no surface is seen), which must not reuse an earlier frame's.
    void BeginFrame() { m_snapshotValid=m_sceneValid=false;m_passRestoreFailed=false;m_distanceAtmosphereShaded=false;m_surfaceResult=SurfaceResult::Unchanged; m_nativeWater=nullptr;m_waterDepthValid=m_slimeCaptured=false; m_work={};++m_worldFrame; SetScenery(nullptr,nullptr,0,nullptr); }
    // Borrowed until EndFrame; WaterDevice owns and clears/releases the targets.
    void SetScenery(IDirect3DTexture9* colour, IDirect3DTexture9* depth, float height, const float* projection, const float* planes = nullptr, bool heights = false, bool underwater = false)
    {
        m_sceneryViews=nullptr;
        m_sceneryPlanesActive=planes!=nullptr; m_sceneryHeightsActive=heights;
        for(unsigned i=0;i<32;++i)m_sceneryPlanes[i]=planes?planes[i]:0;
        m_sceneryUnderwater=underwater;
        m_sceneryColour=colour; m_sceneryDepth=depth; m_sceneryHeight=height;
        for (int i=0;i<16;++i) m_sceneryProjection[i]=projection ? projection[i] : 0;
    }

    void SetSceneryViews(water::SceneryViews* views){m_sceneryViews=views;}

    bool SnapshotBeforeLiquid(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                              bool captureScene);
    bool CompositeWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                        const FrameInputs& in, const Config& cfg, bool shadeSurface, IDirect3DTexture9* nativeWater = nullptr,
                        IDirect3DTexture9* refractionScene = nullptr, IDirect3DTexture9* surfaceVisibility = nullptr);
    // kind: the liquid drawn, slime or lava (each keeps its own surface).
    // undo: take the client's draw back out (false: standalone keeps a fade draw, its water is found by its depth).
    // area: where the draw lies on screen (LiquidScreenArea), or null for the whole viewport.
    bool CaptureAndUndoSurface(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                               const FrameInputs& in, const Config& cfg, bool inLiquidPass, LiquidKind kind, bool undo = true,
                               const RECT* area = nullptr, int ramp = -1);
    bool SnapshotSurfaceBase(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                             const RECT* area = nullptr);
    bool HasDeferredSlime() const { return m_slime.thisFrame; }
    bool HasDeferredLava() const { return m_lava.surface.thisFrame; }
    // How much of the lava's work has run: what a frame does without lava, or with lava out of sight, is checked by it.
    struct LavaWork { unsigned captures = 0, composites = 0, stirSteps = 0, contactSteps = 0; };
    const LavaWork& LavaWorkDone() const { return m_lava.work; }
    bool CompositeDeferredLava(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                               const FrameInputs& in, const Config& cfg);
    // The host's mask with this frame's lava added, for its screen effects (lava_mask_ps); null with no lava this frame.
    bool BuildLavaEffectsMask(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                              const FrameInputs& in, const Config& cfg, IDirect3DTexture9* hostMask);
    IDirect3DTexture9* LavaEffectsMask() const { return m_lava.maskReady ? m_lava.mask : nullptr; }
    bool CompositeDeferredSlime(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg);
    bool Render(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                const FrameInputs& in, const Config& cfg);
    // Shades the solid world where it is tucked in (corners, creases, under things), once it is all drawn and before
    // the liquids, from the depth buffer.
    bool AmbientOcclusion(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                          const FrameInputs& in, const Config& cfg);
    // Wets the terrain and gathers puddles on it while it rains (and dries them after), once the solid world is drawn
    // and before the liquids.
    bool Puddles(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                 const FrameInputs& in, const Config& cfg);
    // Draws a ring on the ring map for each raindrop splash the game has just drawn, for the puddles and the water.
    bool DrawRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg);
    // The rain rings for this frame, drawn the first time a pass reads them (water, slime, puddles), so no host or
    // proxy has to call DrawRainRings itself. Northlight never did, and its water had no rain rings (2026-11-09).
    void PrepareRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg);
    IDirect3DTexture9* OpaqueDepthBeforeLiquid() const { return m_snapshotValid ? m_snapshot : nullptr; }
    IDirect3DTexture9* SceneBeforeLiquid() const { return m_sceneValid ? m_sceneBefore : nullptr; }
    bool CaptureHostPass(IDirect3DDevice9* dev,const char* name,bool after);
    void CaptureFinished(IDirect3DDevice9* dev);
    void EndFrame()
    {
        m_distanceAtmosphereShaded=false;
        m_surfaceResult=SurfaceResult::Unchanged;
        EndReflectionCapture();
        if (m_state) m_state->Release();
        m_state = nullptr;
        m_candidateFailure = nullptr;
        m_snapshotValid = false;
        m_sceneValid = false;
        m_nativeWater=nullptr;m_waterDepthValid=m_slimeCaptured=false;
       
        m_slime.thisFrame = m_slime.outdoorThisFrame = m_slime.fadeThisFrame = m_slime.fadeKept = false;
        m_lava.EndFrame();
        m_waterSurface.advancedThisFrame = false;
        m_slimeSurface.advancedThisFrame = false;
        m_ringMapReach = 0.0f;
        SetScenery(nullptr,nullptr,0,nullptr);
    }
    void ReleaseDefaultPool();
    void ReleaseAll();
    const char* LastSkipReason() const { return m_skip; }

private:
    std::string OutputDirectory() const { return m_outputDirectory.empty() ? GlobalConfig().Directory() : m_outputDirectory; }
    std::string m_outputDirectory;
    std::array<unsigned,9> m_work={};
    water::SceneryViews* m_sceneryViews=nullptr; // borrowed for this frame; no resource ownership
    bool m_sceneryUnderwater=false;
    IDirect3DTexture9* m_sceneryColour=nullptr;
    IDirect3DTexture9* m_sceneryDepth=nullptr;
    float m_sceneryHeight=0,m_sceneryProjection[16]={},m_sceneryPlanes[32]={};
    bool m_sceneryPlanesActive=false,m_sceneryHeightsActive=false;
    // A liquid's ripple simulation, on a grid that follows the camera. Water and slime each have one, following
    // their own surface level.
    struct SurfaceSim
    {
        IDirect3DTexture9* ripples[2] = {};
        int rippleIndex = 0;
        bool valid = false;
        bool advancedThisFrame = false;
        float origin[2] = {};
        double backlog = 0.0;
        long long lastTicks = 0;
        // The harness's clock at the last step (see Config::testSeconds), -1 before any.
        float lastTestSeconds = -1.0f;
        IDirect3DTexture9* Ripples() const { return valid ? ripples[rippleIndex] : nullptr; }
    };
    struct SurfacePhysics
    {
        float waveSpeed;
        float damping;
        float shoreWaves;
        bool usesGround;
        float viscosity;
        float rippleResponse;
    };

    using SavedTargets = ScreenPassState;

    bool Skip(const char* reason);
    bool EnsureShaders(IDirect3DDevice9* dev);
    bool EnsurePixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader);
    HRESULT BindPixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader);
    std::vector<const ShaderProgram*> m_failedShaders;
    bool m_ringShaderFailed = false;
    bool EnsureStateBlock(IDirect3DDevice9* dev);
    bool EnsureSnapshotTarget(IDirect3DDevice9* dev, UINT width, UINT height);
    bool EnsureSceneTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc);
    // Deferred composites need the scene after models/water, while underwater keeps the original pre-liquid scene.
    // Bind the composite pair only for the duration of that pass, including every failure/early return.
    class CompositeScene
    {
    public:
        explicit CompositeScene(Renderer& renderer) : m_renderer(renderer) {}
        ~CompositeScene();
        bool Capture(IDirect3DDevice9* dev, IDirect3DTexture9* depth, IDirect3DSurface9* boundDepth);
        CompositeScene(const CompositeScene&) = delete;
        CompositeScene& operator=(const CompositeScene&) = delete;
    private:
        Renderer& m_renderer;
        bool m_active = false;
    };
    bool CopySceneDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                       IDirect3DTexture9* depthCopy, IDirect3DTexture9* colourCopy, bool& colourValid);
    bool BuildFrameConstants(const FrameInputs& in, const Config& cfg, const D3DSURFACE_DESC& depthDesc,
                             float (&constants)[9][4]);
    bool EnsureProbeTargets(IDirect3DDevice9* dev);
    bool EnsureMemoryTargets(IDirect3DDevice9* dev, IDirect3DTexture9* (&targets)[2], UINT width, UINT height = 1,
                             D3DFORMAT format = D3DFMT_A32B32G32R32F);
    bool BindSky(IDirect3DDevice9* dev, DWORD stage, const Config& cfg);
    bool MeasureLevel(IDirect3DDevice9* dev, IDirect3DTexture9* surfaceDepth, IDirect3DTexture9* (&levels)[2],
                      int& index);
    // A liquid the client draws and the renderer redraws later (slime, lava): where its surface was drawn, what the
    // client drew there, its level, and whether it was drawn this frame.
    struct DeferredSurface
    {
        IDirect3DTexture9* depth = nullptr;
        IDirect3DTexture9* stock = nullptr;
        // Slime only: where a fade draw (water too) was captured, the depth under it; 0 elsewhere (made at the first
        // fade draw). fadeKept: a fade draw the host left drawn this frame (standalone).
        IDirect3DTexture9* fade = nullptr;
        D3DSURFACE_DESC desc = {};
        bool thisFrame = false, outdoorThisFrame = false, fadeThisFrame = false, fadeKept = false;
        // Where this frame's captures lie on screen: their areas together; whole once one could not be bounded.
        RECT area = {};
        bool areaWhole = false;
        IDirect3DTexture9* level[2] = {};
        int levelIndex = 0;
    };
    bool EnsureDeferredTargets(IDirect3DDevice9* dev, DeferredSurface& surface, const D3DSURFACE_DESC& color, bool keepStock);
    static constexpr UINT kSlimeFadeRegister = 96;
    static constexpr DWORD kSlimeFadeStage = 11;
    bool BindSlimeFade(IDirect3DDevice9* dev, IDirect3DTexture9* mark, const NativeLiquidFrame& liquids);
    bool LiftFadeSurfaces(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4]);
    static void ReleaseDeferred(DeferredSurface& surface);
    bool SpreadContactMarks(IDirect3DDevice9* dev, IDirect3DTexture9* level, IDirect3DTexture9* seabed,
                            const float (&frame)[9][4], const float (&constants)[kWaterConstants][4]);
    bool DrawLavaHalo(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4]);
    bool DrawLavaGlow(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const D3DVIEWPORT9& viewport,
                      IDirect3DSurface9* target);
    bool AdvanceLavaStir(IDirect3DDevice9* dev, IDirect3DTexture9* level, const FrameInputs& in, const Config& cfg,
                         const float (&frame)[9][4], float (&constants)[kWaterConstants][4]);
    bool EnsureOcclusionTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& depth);
    bool EnsureRingMap(IDirect3DDevice9* dev);
    bool BindRingMap(IDirect3DDevice9* dev);
    bool BindWaterTextures(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg, bool slime = false);
    bool DrawReflections(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                         const float (&frame)[9][4], const float (&constants)[kWaterConstants][4], bool slime = false);
    bool DrawSurfaceDetail(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                           const float (&frame)[9][4], const float (&constants)[kWaterConstants][4]);
    bool BindSurfaceDetail(IDirect3DDevice9* dev);
    bool DrawShipWakeMap(IDirect3DDevice9* dev, const FrameInputs& in, const float (&frame)[9][4],
                         const float (&constants)[kWaterConstants][4], float (&squares)[kMaxBoats][4]);
    bool BindLiquidOptions(IDirect3DDevice9* dev, const Config& cfg, bool mapped);
    bool PrepareShipWakes(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg, const float (&frame)[9][4],
                          const float (&constants)[kWaterConstants][4]);
    void WriteDepthAlways(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* boundDepth,
                          const D3DVIEWPORT9& viewport);
    void RememberSky(IDirect3DDevice9* dev, IDirect3DTexture9* depth, IDirect3DTexture9* finished, const FrameInputs& in, const Config& cfg);
    bool AdvanceSurface(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level,
                        const SurfacePhysics& physics, const FrameInputs& in, const Config& cfg,
                        const float (&frame)[9][4], float (&constants)[kWaterConstants][4]);
    bool MarkContacts(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level, IDirect3DTexture9* seabed,
                      float frameSeconds, const float (&frame)[9][4], const float (&constants)[kWaterConstants][4]);
    static SurfacePhysics WaterPhysics(const Config& cfg);
    static SurfacePhysics SlimePhysics(const Config& cfg);
    bool EnsureRippleTargets(IDirect3DDevice9* dev, SurfaceSim& sim);
    IDirect3DTexture9* GroundTexture(IDirect3DDevice9* dev, const GroundMap* ground);
    IDirect3DTexture9* FarGroundTexture(IDirect3DDevice9* dev, const GroundMap* ground);
    static void ReleaseSurface(SurfaceSim& sim);
    bool BeginPass(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, SavedTargets& saved);

    bool SetFullscreenState(IDirect3DDevice9* dev);
    void DrawOverlay(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                     const FrameInputs& in, const Config& cfg, const float (&constants)[9][4]);
    void Capture(IDirect3DDevice9* dev, int request, int frames);
public:
    bool SaveRenderTarget(IDirect3DDevice9* dev, const std::string& path);
private:
    bool BuildWaterConstants(const FrameInputs& in, const Config& cfg, const LiquidStyle& style, const SurfaceSim& sim,
                             const float (&frame)[9][4], float (&constants)[kWaterConstants][4]);
    IDirect3DTexture9* BoatPathTexture(IDirect3DDevice9* dev, const FrameInputs& in);
    static LiquidStyle Weathered(const LiquidStyle& style, const FrameInputs& in, const Config& cfg);
    static float OvercastAmount(const FrameInputs& in, const Config& cfg);
    static float RainNow(const FrameInputs& in, const Config& cfg);
    float LiquidSeconds(const Config& cfg);
    bool DrawNoiseLattice(IDirect3DDevice9* dev);
    bool PrepareWaveMoments(IDirect3DDevice9* dev, const Config& cfg);
    bool EnsureSurfaceDetailTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, bool needSlopes);
    bool DrawUnderwaterFoam(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const D3DSURFACE_DESC& desc,
                            const FrameInputs& in, const Config& cfg, const float (&frame)[9][4], const float (&water)[kWaterConstants][4]);
    void DrawUnderwater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                        const FrameInputs& in, const Config& cfg, const float (&frame)[9][4]);
    void Antialias(IDirect3DDevice9* dev, IDirect3DSurface9* target, const FrameInputs& in,
                   const float (&constants)[9][4]);
    void Probe(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in,
               const float (&constants)[9][4]);

    const water::SurfaceEnvironment* m_environment=nullptr;
    bool m_usingEnvironment=false, m_usingWaveMoments=false, m_waveMomentsFailed=false;
    SurfaceResult m_surfaceResult=SurfaceResult::Unchanged;
    bool m_distanceAtmosphereShaded=false;
    float m_environmentRayStep=0;
    bool m_passRestoreFailed=false;
    const char* m_skip = "";
    const char* m_candidateFailure = nullptr; // reflection failure survives the later FXAA endpoint until frame end
    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_snapshotPs = nullptr;
    IDirect3DPixelShader9* m_overlayPs = nullptr;
    IDirect3DPixelShader9* m_probePs = nullptr;
    IDirect3DPixelShader9* m_waterPs = nullptr;
    IDirect3DPixelShader9* m_waterMomentsPs = nullptr;
    IDirect3DPixelShader9* m_surfaceMomentsPs = nullptr;
    IDirect3DPixelShader9* m_fxaaPs = nullptr;
    IDirect3DPixelShader9* m_levelPs = nullptr;
    IDirect3DTexture9* m_level[2] = {};
    int m_levelIndex = 0;
    IDirect3DPixelShader9* m_skyMemoryPs = nullptr;
    IDirect3DPixelShader9* m_skyExtendPs = nullptr;
    IDirect3DPixelShader9* m_underwaterPs = nullptr;
    IDirect3DPixelShader9* m_underwaterFoamPs = nullptr;
    IDirect3DPixelShader9* m_underwaterSharedPs = nullptr;
    IDirect3DPixelShader9* m_underwaterSceneryPs = nullptr;
    IDirect3DPixelShader9* m_slimeUnderwaterSceneryPs = nullptr;
    unsigned m_underwaterFrames = 0;
    unsigned m_slimeComposites = 0;
    GroundEffectCounts m_groundEffects;
    IDirect3DPixelShader9* m_slimeCapturePs = nullptr;
    IDirect3DPixelShader9* m_slimeLiftPs = nullptr;
    IDirect3DTexture9* m_slimeGround = nullptr; // the ground under kept fade draws (LiftFadeSurfaces)
    IDirect3DPixelShader9* m_slimeUndoPs = nullptr;
    IDirect3DPixelShader9* m_slimeDeferredPs = nullptr;
    DeferredSurface m_slime;
    IDirect3DTexture9* m_surfaceBaseDepth = nullptr;
    IDirect3DTexture9* m_nativeWater = nullptr; // host distance/coverage, borrowed through this frame
    IDirect3DTexture9* m_waterDepth = nullptr; // standalone depth at the end of the liquid pass
    bool m_waterDepthValid = false, m_slimeCaptured = false;
    struct LavaResources
    {
        DeferredSurface surface;
        SurfaceSim stir;
        IDirect3DPixelShader9* surfacePs = nullptr;
        IDirect3DPixelShader9* stirPs = nullptr;
        IDirect3DPixelShader9* maskPs = nullptr;
        IDirect3DPixelShader9* haloPs = nullptr;
        IDirect3DPixelShader9* glowPs = nullptr;
        IDirect3DTexture9* halo = nullptr;
        IDirect3DTexture9* haloColours = nullptr; // Only views containing both magma palettes need local colour coverage.
        bool ordinarySeen = false, felSeen = false;
        IDirect3DTexture9* mask = nullptr;
        bool drewThisFrame = false, maskReady = false;
        // Whether any of the lava drawn lately reached the screen: counted by the GPU and read back when it is ready, without
        // waiting. The client also draws lava that lies behind a mountain.
        IDirect3DQuery9* visibleQuery = nullptr;
        bool visiblePending = false, hidden = false;
        double contactDue = 0.0;
        LavaWork work;
        IDirect3DPixelShader9* bakeCellsPs = nullptr;
        IDirect3DPixelShader9* bakeSlopePs = nullptr;
        IDirect3DPixelShader9* bakeNoisePs = nullptr;
        IDirect3DPixelShader9* bakeFoamPs = nullptr;
        IDirect3DPixelShader9* bakeFlowPs = nullptr;
        // The baked patterns are rebuilt after Reset. The two tables of random
        // numbers survive a Reset.
        IDirect3DTexture9* cells = nullptr;
        IDirect3DTexture9* noise = nullptr;
        IDirect3DTexture9* foam = nullptr;
        IDirect3DTexture9* flow = nullptr;
        IDirect3DTexture9* rand = nullptr;
        IDirect3DTexture9* table = nullptr;
        void EndFrame();
        void ReleaseDefaultPool();
        void ReleaseManaged();
        void PollVisibility(IDirect3DDevice9* dev);
        bool EnsureHalo(IDirect3DDevice9* dev,UINT width,UINT height);
        bool EnsureMask(IDirect3DDevice9* dev,UINT width,UINT height);
        bool BakePatterns(Renderer& renderer,IDirect3DDevice9* dev);
    };
    LavaResources m_lava;
    IDirect3DPixelShader9* m_ripplePs = nullptr;
    IDirect3DPixelShader9* m_aoPs = nullptr;
    IDirect3DPixelShader9* m_aoBlurPs = nullptr;
    IDirect3DPixelShader9* m_puddlePs = nullptr;
    IDirect3DPixelShader9* m_ringPs = nullptr;
    IDirect3DPixelShader9* m_reflectionSceneryPs = nullptr;
    IDirect3DPixelShader9* m_slimeReflectionPs = nullptr;
    IDirect3DPixelShader9* m_surfaceDetailPs = nullptr;
    // The pass drawing the noise lattice and the foam's wandering points the surface detail pass reads (see
    // DrawNoiseLattice).
    IDirect3DPixelShader9* m_noiseLatticePs = nullptr;
    IDirect3DTexture9* m_noiseLattice[2] = {};
    // The ships' wakes' slope (or the whole surface's) and the water's foam at each pixel, and the slope foam lies on,
    // and whether they were drawn this frame (see DrawSurfaceDetail).
    IDirect3DTexture9* m_surfaceDetail[2] = {};
    bool m_surfaceDetailDrawn = false;
    // Each ship's place along its path over the square round its wake (see DrawShipWakeMap): two targets, a square
    // each ship, one under the other.
    IDirect3DPixelShader9* m_wakeMapPs = nullptr;
    IDirect3DTexture9* m_wakeMap[2] = {};
    IDirect3DPixelShader9* m_fillPullPs = nullptr;
    IDirect3DPixelShader9* m_fillPushPs = nullptr;
    IDirect3DPixelShader9* m_underwaterScenePs = nullptr;
    IDirect3DPixelShader9* m_contactPs = nullptr;
    IDirect3DPixelShader9* m_contactSpreadPs = nullptr;
    // Where something standing in the water crosses its surface this frame, and that blurred one way (MarkContacts).
    IDirect3DTexture9* m_contactMarks = nullptr;
    IDirect3DTexture9* m_contactSpread = nullptr;
    // Levels for filling in what is not known (fill_pull_ps, fill_push_ps), each a pair of targets: the colours, and
    // the states. The pull's finest level is the input; the push's finest, the filled result.
    struct FillChain
    {
        static constexpr int kLevels = 8;
        IDirect3DTexture9* pull[kLevels][2] = {};
        IDirect3DTexture9* push[kLevels][2] = {};
        UINT widths[kLevels] = {};
        UINT heights[kLevels] = {};
    };
    // The water's reflections, their colour and what each ray met (see DrawReflections); and what lies under it for the
    // refraction (DrawUnderwaterScene).
    IDirect3DTexture9* m_reflections[2] = {};
    FillChain m_underwaterFill;
    bool m_underwaterSceneDrawn = false;
    bool EnsureFillChain(IDirect3DDevice9* dev, FillChain& chain, UINT width, UINT height);
    static void ReleaseFillChain(FillChain& chain);
    bool FillIn(IDirect3DDevice9* dev, FillChain& chain);
    bool DrawUnderwaterScene(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                             const float (&frame)[9][4], const float (&constants)[kWaterConstants][4]);
    IDirect3DVertexShader9* m_ringVs = nullptr;
    IDirect3DVertexDeclaration9* m_ringDecl = nullptr;
    // The ring map (see DrawRainRings): where its corner lies, and how far from the camera it has rings this frame.
    IDirect3DTexture9* m_ringMap = nullptr;
    float m_ringMapOrigin[2] = {};
    float m_ringMapReach = 0.0f;
    unsigned m_ringCount = 0;
    // How full the puddles are (0 dry .. 1 full), and when that was last brought up to date.
    float m_wetness = 0.0f;
    long long m_wetnessTicks = 0;
    // The ambient occlusion as worked out, and after smoothing across the screen.
    IDirect3DTexture9* m_occlusion[2] = {};
    UINT m_occlusionWidth = 0;
    UINT m_occlusionHeight = 0;
    SurfaceSim m_waterSurface;
    SurfaceSim m_slimeSurface;
    // The ground heights around the camera (engine::UpdateGround) as a texture: height, and whether it is known.
    IDirect3DTexture9* m_ground = nullptr;
    IDirect3DTexture9* m_shore = nullptr;
    // The ships' paths for their wakes (see BoatPathTexture).
    IDirect3DTexture9* m_boatPaths = nullptr;
    IDirect3DTexture9* m_farGround = nullptr;
    uint64_t m_farGroundSource = 0;
    unsigned m_farGroundRevision = 0;
    uint64_t m_groundSource = 0;
    unsigned m_groundRevision = 0;
    IDirect3DTexture9* m_sky[2] = {};
    int m_skyIndex = 0;
    IDirect3DVertexDeclaration9* m_decl = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    IDirect3DTexture9* m_snapshot = nullptr;
    UINT m_snapshotWidth = 0;
    UINT m_snapshotHeight = 0;
    bool m_snapshotValid = false;
    IDirect3DTexture9* m_sceneBefore = nullptr;
    IDirect3DTexture9* m_sceneWithWater = nullptr;
    IDirect3DSurface9* m_multisampleRollback = nullptr;
    IDirect3DTexture9* m_compositeDepth = nullptr;
    IDirect3DTexture9* m_compositeScene = nullptr;
    D3DSURFACE_DESC m_compositeDesc = {};
    D3DSURFACE_DESC m_sceneDesc = {};
    bool m_sceneValid = false;
    unsigned m_worldFrame = 0;
    unsigned m_ringFrame = 0; // the world frame whose rain rings are drawn
    unsigned m_composites = 0;
    long long m_startTicks = 0;
    IDirect3DTexture9* m_probeTarget = nullptr;
    IDirect3DSurface9* m_probeReadback = nullptr;
    unsigned m_frame = 0;
    int m_reflectionCaptureRequest = 0;
    int m_reflectionCaptureRemaining = 0;
    int m_reflectionCaptureIndex = 0;
    std::string m_reflectionCaptureDirectory;
    void BeginReflectionCapture(const Config& cfg, const FrameInputs& in);
    std::vector<std::string> m_hostCapturePasses;
    bool m_hostCaptureFailed=false;
    void EndReflectionCapture(bool aborted = false);
    bool m_reflectionCaptureBurst = false;
    reflection::CaptureBurst m_captureBurst;
    std::vector<std::string> m_burstFrames;
    reflection::CaptureBurst* ActiveCaptureBurst() { return m_reflectionCaptureBurst ? &m_captureBurst : nullptr; }
    int m_capturedRequest = 0;
    int m_pendingCapture = 0;
    // A capture of several frames in a row: how many are still to save, and how many have been.
    GpuTimer* m_timer = nullptr;
    // A part of the water timed (GpuTimer::Pass), when there is a timer.
    void TimeBegin(int pass);
    void TimeEnd(int pass);
    int m_captureLeft = 0;
    int m_captureSaved = 0;
    unsigned m_rendered = 0;
    unsigned m_snapshots = 0;
    long long m_lastSummary = 0;
    long long m_lastProbe = 0;
};
