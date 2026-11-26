// The moving surface: ripples from swimmers and ships, contact foam, ship wakes, and the wave detail the composite reads.
#include "render/renderer_internal.h"

using namespace render_detail;

bool Renderer::MeasureLevel(IDirect3DDevice9* dev, IDirect3DTexture9* surfaceDepth, IDirect3DTexture9* (&levels)[2],
                            int& index)
{
    if (!EnsureMemoryTargets(dev, levels, 1))
        return false;
    const int next = index ^ 1;
    if(!SetTarget(dev, levels[next]))return false;
    D3DVIEWPORT9 vp = {0, 0, 1, 1, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(BindPixelShader(dev, m_levelPs)))return false;
    if(!BindPointTexture(dev, 0, surfaceDepth))return false;
    if(!BindPointTexture(dev, 1, m_snapshot))return false;
    if(!BindPointTexture(dev, 2, levels[index]))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    index = next;
    return true;
}

Renderer::SurfacePhysics Renderer::WaterPhysics(const Config& cfg)
{
    return {kRippleWaveSpeed, kRippleDamping, cfg.shoreWaves, true, 0.0f, 1.0f, kContactFoamFade};
}

Renderer::SurfacePhysics Renderer::SlimePhysics(const Config& cfg, float regionalStrength)
{
    // Slime lies over building floors as often as terrain, so it does not use the ground map: where it meets the world
    // is found on screen (its depth, and the marks of contact_ps).
    return {cfg.slimeRippleSpeed * (1.0f - 0.25f * regionalStrength), kSlimeDamping, 0.0f, false, kSlimeViscosity, cfg.slimeStyle.rippleResponse, 0.0f};
}

bool Renderer::EnsureRippleTargets(IDirect3DDevice9* dev, SurfaceSim& sim)
{
    if (sim.ripples[0] && sim.ripples[1])
        return true;
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, target, nullptr)))
        {
            ReleaseSurface(sim);
            return Skip("ripple targets could not be created");
        }
    sim.valid = false;
    return true;
}

void Renderer::ReleaseSurface(SurfaceSim& sim)
{
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        SafeRelease(*target);
    sim.valid = false;
}

// The ships' paths for their wakes, a row each (in the order of the ship constants): each point where the ship was,
// how long ago (seconds) and how fast it was going, its position first. Past the known path the last point repeats
// (the wake fades out before the path ends). A texture rather than constants, so the shaders can walk a path in a loop.
IDirect3DTexture9* Renderer::BoatPathTexture(IDirect3DDevice9* dev, const FrameInputs& in)
{
    if (!m_boatPaths && FAILED(dev->CreateTexture(kBoatTrailPoints, kMaxBoats, 1, 0, D3DFMT_A32B32G32R32F,
                                                  D3DPOOL_MANAGED, &m_boatPaths, nullptr)))
    {
        Skip("ship path texture could not be created");
        return nullptr;
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_boatPaths->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    for (int i = 0; i < kMaxBoats; ++i)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + i * locked.Pitch);
        const bool sailing = i < in.boatCount;
        const int known = sailing ? std::clamp(in.boats[i].trailCount, 1, kBoatTrailPoints) : 1;
        for (int point = 0; point < kBoatTrailPoints; ++point)
        {
            const WakePoint from = sailing ? in.boats[i].trail[std::min(point, known - 1)] : WakePoint{};
            row[4 * point] = from.x;
            row[4 * point + 1] = from.y;
            row[4 * point + 2] = from.age;
            row[4 * point + 3] = from.speed;
        }
    }
    m_boatPaths->UnlockRect(0);
    return m_boatPaths;
}

IDirect3DTexture9* Renderer::GroundTexture(IDirect3DDevice9* dev, const GroundMap* ground)
{
    if (!ground)
        return nullptr;
    if (!m_ground && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_A32B32G32R32F,
                                               D3DPOOL_MANAGED, &m_ground, nullptr)))
    {
        Skip("ground texture could not be created");
        return nullptr;
    }
    if (!m_shore && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_G32R32F, D3DPOOL_MANAGED,
                                              &m_shore, nullptr)))
    {
        Skip("shore distance texture could not be created");
        return nullptr;
    }
    if (ground->Identity() == m_groundSource && ground->Revision() == m_groundRevision)
        return m_ground;
    D3DLOCKED_RECT shore = {};
    if (SUCCEEDED(m_shore->LockRect(0, &shore, nullptr, 0)))
    {
        // r: how far water is from the land. g: how far the land is from the water.
        const float* water = ground->ShoreDistances();
        const float* land = ground->LandDistances();
        for (int y = 0; y < GroundMap::kCells; ++y)
        {
            float* row = reinterpret_cast<float*>(static_cast<char*>(shore.pBits) + y * shore.Pitch);
            for (int x = 0; x < GroundMap::kCells; ++x)
            {
                row[2 * x] = water[y * GroundMap::kCells + x];
                row[2 * x + 1] = land[y * GroundMap::kCells + x];
            }
        }
        m_shore->UnlockRect(0);
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_ground->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    if(const auto* packed=ground->PackedTexture()){
        for(int y=0;y<GroundMap::kCells;++y)
            std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch,
                packed+size_t(y)*GroundMap::kCells*4,size_t(GroundMap::kCells)*4*sizeof(float));
    }else {
    const float* heights = ground->Heights();
    // The nearest water's height everywhere (so the surface carries on at it where the world gives no height), and
    // how far the world's own height there is trusted.
    const float* liquids = ground->NearestLiquidHeights();
    const float fallback = ground->NearbyWaterKnown() ? ground->NearbyWater() : 0.0f;
    const float* trust = ground->LiquidTrust();
    for (int y = 0; y < GroundMap::kCells; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < GroundMap::kCells; ++x)
        {
            const float h = heights[y * GroundMap::kCells + x];
            const float l = liquids[y * GroundMap::kCells + x];
            row[4 * x] = h > GroundMap::kUnknown ? h : 0.0f;
            row[4 * x + 1] = h > GroundMap::kUnknown ? 1.0f : 0.0f;
            row[4 * x + 2] = l > GroundMap::kUnknown ? l : fallback;
            row[4 * x + 3] = trust[y * GroundMap::kCells + x];
        }
    }
    }
    m_ground->UnlockRect(0);
    m_groundSource = ground->Identity();
    m_groundRevision = ground->Revision();
    return m_ground;
}

// The far ground map for the lava's glow. r: the ground's height; g: 1 where known; b: the liquid's; a: how far the land is from lava.
IDirect3DTexture9* Renderer::FarGroundTexture(IDirect3DDevice9* dev, const GroundMap* ground)
{
    if (!ground)
        return nullptr;
    if (!m_farGround && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &m_farGround, nullptr)))
    {
        Skip("far ground texture could not be created");
        return nullptr;
    }
    if (ground->Identity() == m_farGroundSource && ground->Revision() == m_farGroundRevision)
        return m_farGround;
    const float* land = ground->LandDistances();
    const float* heights = ground->Heights();
    const float* liquids = ground->NearestLiquidHeights();
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_farGround->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    for (int y = 0; y < GroundMap::kCells; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < GroundMap::kCells; ++x)
        {
            const int i = y * GroundMap::kCells + x;
            row[4 * x] = heights[i] > GroundMap::kUnknown ? heights[i] : 0.0f;
            row[4 * x + 1] = heights[i] > GroundMap::kUnknown ? 1.0f : 0.0f;
            row[4 * x + 2] = liquids[i] > GroundMap::kUnknown ? liquids[i] : 0.0f;
            row[4 * x + 3] = land[i];
        }
    }
    m_farGround->UnlockRect(0);
    m_farGroundSource = ground->Identity();
    m_farGroundRevision = ground->Revision();
    return m_farGround;
}

bool Renderer::AdvanceSurface(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level,
                              const SurfacePhysics& physics, const FrameInputs& in, const Config& cfg,
                              const float (&frame)[9][4], float (&constants)[kWaterConstants][4])
{
    if(!cfg.ripples)return true;
    if(!EnsureRippleTargets(dev,sim))return false;
    float* rippleArea = constants[kRippleAreaRegister - kFrameConstants];
    if (sim.advancedThisFrame)
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        rippleArea[3] = cfg.rippleStrength * physics.rippleResponse;
        return true;
    }
    // A failed GPU step may have changed either buffer. Never reuse that state,
    // including a retry before EndFrame; initialize both buffers on the next call.
    struct SimulationUpdate {
        SurfaceSim& sim;
        bool complete=false;
        ~SimulationUpdate(){if(!complete){sim.valid=false;sim.advancedThisFrame=false;}}
    } update{sim};
    const long long now = Ticks();
    // The harness sets the clock itself (testSeconds): the simulation goes on by as much as that clock moved, none while
    // it is held.
    const double frameSeconds = cfg.testSeconds >= 0.0f
                                    ? (sim.lastTestSeconds >= 0.0f ? std::max(cfg.testSeconds - sim.lastTestSeconds, 0.0f)
                                                                   : 0.0)
                                : sim.lastTicks ? SecondsSince(sim.lastTicks)
                                                : 0.0;
    sim.lastTestSeconds = cfg.testSeconds;
    sim.backlog += frameSeconds;
    sim.lastTicks = now;
    // The same square as the ground map, so the two line up cell for cell.
    static_assert(kRippleCells * kRippleCellSize == GroundMap::kArea, "the ripple grid covers the ground map");
    const float origin[2] = {in.ground ? in.ground->Origin()[0] : GroundMap::OriginFor(in.cameraPosition[0]),
                             in.ground ? in.ground->Origin()[1] : GroundMap::OriginFor(in.cameraPosition[1])};
    float shift[2] = {(origin[0] - sim.origin[0]) / kRippleCellSize,
                      (origin[1] - sim.origin[1]) / kRippleCellSize};
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if (!sim.valid || std::fabs(shift[0]) >= kRippleCells || std::fabs(shift[1]) >= kRippleCells)
    {
        for (auto* target : {sim.ripples[0], sim.ripples[1]})
        {
            if(!SetTarget(dev, target))return false;
            if(FAILED(dev->SetViewport(&grid)))return false;
            if(FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))return false;
        }
        shift[0] = shift[1] = 0.0f;
        sim.valid = true;
        sim.backlog = 0.0;
    }
    sim.origin[0] = origin[0];
    sim.origin[1] = origin[1];
    rippleArea[0] = origin[0];
    rippleArea[1] = origin[1];
    rippleArea[3] = cfg.rippleStrength * physics.rippleResponse;

    IDirect3DTexture9* seabed = physics.usesGround ? GroundTexture(dev, in.ground) : nullptr;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;

    int steps = static_cast<int>(sim.backlog / kRippleStepSeconds);
    sim.backlog -= steps * kRippleStepSeconds;
    steps = std::min(steps, kMaxRippleStepsPerFrame);
    const bool pulsePending = std::any_of(in.movers, in.movers + in.moverCount,
                                          [](const Mover& mover) { return !mover.continuous; });
    // Grid transport and one-shot impulses do not spend simulation time. Borrowing
    // a future tick here made the backlog negative at high frame rates; a later
    // negative step count then moved the origin without shifting the history.
    const bool transportOnly = steps == 0;
    const int passes = steps ? steps : (shift[0] != 0.0f || shift[1] != 0.0f || pulsePending ? 1 : 0);

    const float courant = std::pow(physics.waveSpeed * kRippleStepSeconds / kRippleCellSize, 2.0f);
    float movers[kMaxMovers][4] = {};
    float radii[kMaxMovers][4] = {};
    for (int i = 0; i < in.moverCount; ++i)
    {
        const auto& mover=in.movers[i];
        std::memcpy(movers[i], mover.position, sizeof(mover.position));
        movers[i][3] = mover.strength;
        radii[i][0] = mover.radius;
        radii[i][1] = mover.continuous ? 1.0f : 0.0f;
    }
    if(FAILED(BindPixelShader(dev, m_ripplePs)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kRippleMoverRegister, &movers[0][0], kMaxMovers)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kRippleRadiusRegister, &radii[0][0], kMaxMovers)))return false;
    const float firstStep[4] = {static_cast<float>(in.moverCount), 1.0f, transportOnly ? 1.0f : 0.0f, 0.0f};
    const float laterSteps[4] = {static_cast<float>(in.moverCount), 0.0f, 0.0f, 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(2, firstStep, 1)))return false;
    const float gridConstants[4] = {1.0f / kRippleCells, courant, physics.damping, kRippleCellSize};
    if(FAILED(dev->SetPixelShaderConstantF(0, gridConstants, 1)))return false;
    const float viscosity[4] = {physics.viscosity, physics.contactFade, 0.0f, 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(kRippleViscosityRegister, viscosity, 1)))return false;
    if(!BindPointTexture(dev, 1, level))return false;
    if(!BindPointTexture(dev, 2, seabed))return false;
    for (int step = 0; step < passes; ++step)
    {
        const float seconds = LiquidSeconds(cfg) + step * kRippleStepSeconds;
        const float shore[4] = {seconds, kShoreWaveAmplitude * physics.shoreWaves, kShoreWavesPerYard, kShoreWaveSpeed};
        if(FAILED(dev->SetPixelShaderConstantF(3, shore, 1)))return false;
        const float originConstants[4] = {origin[0], origin[1], step == 0 ? shift[0] : 0.0f,
                                          step == 0 ? shift[1] : 0.0f};
        if(FAILED(dev->SetPixelShaderConstantF(1, originConstants, 1)))return false;
        const int next = sim.rippleIndex ^ 1;
        if(!SetTarget(dev, sim.ripples[next]))return false;
        if(FAILED(dev->SetViewport(&grid)))return false;
        if(!BindPointTexture(dev, 0, sim.ripples[sim.rippleIndex]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
        sim.rippleIndex = next;
        if(FAILED(dev->SetPixelShaderConstantF(2, laterSteps, 1)))return false;
    }
    if(!MarkContacts(dev,sim,level,seabed,physics.usesGround,static_cast<float>(frameSeconds),frame,constants))return false;
    sim.advancedThisFrame=true;
    update.complete=true;
    return true;
}

// The foam where something stands in the water, on the ripple grid: where what is on screen crosses the water's
// surface this frame (contact_ps), spread by the distance from the nearest of them across the grid one way and then the
// other (contact_spread_ps), the second way into the grid's state, keeping the foam already there where it is more (it
// fades in the ripple steps). Leaves the render target, viewport, pixel shader, stages 0 to 7 and the frame and water
// constants changed.
// Where something crosses the liquid's surface (contact_ps), spread one way across the grid (contact_spread_ps), into
// m_contactMarks; the caller takes it the other way. remembered: the grid slime keeps its edge on where the camera has
// seen it, for the points it cannot look at this frame (none for water and lava, which look by the ground map);
// provenStep: how much what it keeps of a mark that stays grows this frame.
bool Renderer::SpreadContactMarks(IDirect3DDevice9* dev, IDirect3DTexture9* level, IDirect3DTexture9* seabed,
                                  const float (&frame)[9][4], const float (&constants)[kWaterConstants][4],
                                  IDirect3DTexture9* remembered, float provenStep)
{
    const float options[4] = {seabed ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(kContactOptionsRegister, options, 1)))return false;
    for (auto** target : {&m_contactMarks, &m_contactSpread})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F,
                                                  D3DPOOL_DEFAULT, target, nullptr)))
        {
            Skip("contact foam targets could not be created");
            return false;
        }
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if(!SetTarget(dev, m_contactMarks))return false;
    if(FAILED(dev->SetViewport(&grid)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_contactPs)))return false;
    if(!BindPointTexture(dev, 1, m_snapshot))return false;
    if(!BindPointTexture(dev, 4, level))return false;
    if(!BindLinearTexture(dev, 7, seabed))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;

    const float cell = 1.0f / kRippleCells;
    // Which pass each is (contact_spread_ps: cSpread.z).
    const float count[4] = {remembered ? 1.0f : 0.0f, provenStep, 0.0f, cell};
    const float across[4] = {cell, 0.0f, 1.0f, cell};
    // The crossings counted by their neighbours, into the second target; the nearest of them one way, back into the
    // first (the marks are done with); then the other way, into the grid.
    if(FAILED(BindPixelShader(dev, m_contactSpreadPs)))return false;
    if(!SetTarget(dev, m_contactSpread))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, count, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactMarks))return false;
    if(!BindPointTexture(dev, 1, remembered))return false;
    if(!BindPointTexture(dev, 2, nullptr))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(!BindPointTexture(dev, 1, nullptr))return false;
    if(!SetTarget(dev, m_contactMarks))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, across, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactSpread))return false;
    return SUCCEEDED(DrawScreenTriangle(dev));
}

bool Renderer::MarkContacts(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level,
                            IDirect3DTexture9* seabed, bool usesGround, float frameSeconds, const float (&frame)[9][4],
                            const float (&constants)[kWaterConstants][4])
{
    if(!m_snapshotValid||(usesGround&&!seabed))return true;
    if(!SpreadContactMarks(dev,level,seabed,frame,constants,usesGround?nullptr:sim.ripples[sim.rippleIndex],
                           frameSeconds/kSlimeEdgeProvenSeconds))return false;
    const float cell = 1.0f / kRippleCells;
    // How much of the way to the new foam it rises this frame.
    const float rise = std::clamp(1.0f - std::exp(-frameSeconds / (usesGround ? kContactFoamRiseSeconds : kSlimeEdgeEaseSeconds)), 0.0f, 1.0f);
    // The pass (contact_spread_ps: cSpread.z), and the rise on its own (a rise of nothing, when no time passed, once read
    // as the count and wrote the counted marks over the whole grid).
    const float down[4] = {0.0f, cell, 2.0f, cell};
    const float riseConstants[4] = {rise, usesGround ? 0.0f : 1.0f, 0.0f, 0.0f};
    const int next = sim.rippleIndex ^ 1;
    if(!SetTarget(dev, sim.ripples[next]))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, down, 1)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(1, riseConstants, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactMarks))return false;
    if(!BindPointTexture(dev, 1, sim.ripples[sim.rippleIndex]))return false;
    if(!BindPointTexture(dev, 2, m_contactSpread))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    sim.rippleIndex = next;
    return true;
}

// The wake map (wake_map_ps): each ship's place along its path at points over the square round its wake's box, walked
// once a frame rather than for every pixel of water (ShipWakeMap). The place changes smoothly (see water_common's
// AlongPath), so read between the points it is as good as walked for the pixel; each square's corner lies on a whole
// number of steps, so the points stay put in the world as the ship moves. Leaves the render targets, viewport, pixel
// shader, stage 10 and the first constants changed; false (and nothing drawn) when there are no ships.
bool Renderer::DrawShipWakeMap(IDirect3DDevice9* dev, const FrameInputs& in, const float (&frame)[9][4],
                               const float (&constants)[kWaterConstants][4], float (&squares)[kMaxBoats][4])
{
    const int ships = std::min(in.boatCount, kMaxBoats);
    if (ships <= 0)
        return false;
    for (IDirect3DTexture9*& map : m_wakeMap)
        if (!map && FAILED(dev->CreateTexture(kWakeMapSize, kWakeMapSize * kMaxBoats, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &map, nullptr)))
            return Skip("wake map could not be created");
    for (int i = 0; i < ships; ++i)
    {
        float* square = squares[i];
        const float across = std::max(square[2] - square[0], square[3] - square[1]);
        const float step = std::max(kWakeMapStep, across / (kWakeMapSize - 1));
        square[0] = std::floor(square[0] / step) * step;
        square[1] = std::floor(square[1] / step) * step;
        square[2] = square[0] + step * kWakeMapSize;
        square[3] = square[1] + step * kWakeMapSize;
    }
    if(!SetPairTargets(dev, m_wakeMap))return false;
    const D3DVIEWPORT9 vp = {0, 0, kWakeMapSize, kWakeMapSize * static_cast<DWORD>(ships), 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kShipBoxRegister, &squares[0][0], kMaxBoats)))return false;
    if(FAILED(BindPixelShader(dev, m_wakeMapPs)))return false;
    if(!BindPointTexture(dev, kBoatPathStage, BoatPathTexture(dev, in)))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

bool Renderer::PrepareShipWakes(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg,
                                const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    float boxes[kMaxBoats][4];
    ShipWakeBoxes(in, boxes);
    const bool mapped=in.boatCount>0;
    if(mapped&&!DrawShipWakeMap(dev,in,frame,constants,boxes))return false;
    if(!BindLiquidOptions(dev,cfg,mapped))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kShipBoxRegister, &boxes[0][0], kMaxBoats)))return false;
    if(!BindPointTexture(dev, kWakeMapPlaceStage, mapped ? m_wakeMap[0] : nullptr))return false;
    if(!BindPointTexture(dev, kWakeMapWayStage, mapped ? m_wakeMap[1] : nullptr))return false;
    return true;
}

// Candidate shader failures cannot disable ordinary water or retry creation every frame.
bool Renderer::PrepareWaveMoments(IDirect3DDevice9* dev, const Config& cfg)
{
    if (m_waveMomentsFailed) return false;
    const bool ok = EnsurePixelShader(dev, m_waterMomentsPs) && EnsurePixelShader(dev, m_surfaceMomentsPs);
    if (!ok) {
        m_waveMomentsFailed = true;
        WATER_LOG_ERROR("wave moment shader unavailable; the unfiltered water material stands in until Reset");
    }
    return ok;
}

bool Renderer::EnsureSurfaceDetailTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& full, bool needSlopes)
{
    D3DSURFACE_DESC have = {};
    if (m_surfaceDetail[0] && (FAILED(m_surfaceDetail[0]->GetLevelDesc(0, &have)) || have.Width != full.Width ||
                               have.Height != full.Height))
    {
        SafeRelease(m_surfaceDetail[0]);
        SafeRelease(m_surfaceDetail[1]);
    }
    for (unsigned i = 0; i < (needSlopes ? 2u : 1u); ++i)
        if (!m_surfaceDetail[i] && FAILED(dev->CreateTexture(full.Width, full.Height, 1, D3DUSAGE_RENDERTARGET,
                D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &m_surfaceDetail[i], nullptr)))
            return Skip("surface detail target could not be created");
    return true;
}

// The surface detail pass (surface_detail_ps): at each water pixel the ships' wakes' slope and all the water's foam,
// worked out once for the reflection pass and the composite to read (water_ps: READS_SURFACE_DETAIL); worked out in
// the composite as well as everything else, they ran it out of registers. Leaves the render target, viewport, pixel
// shader, stages 0 to 11 and the first constants changed.
bool Renderer::DrawSurfaceDetail(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                                 const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || FAILED(m_snapshot->GetLevelDesc(0, &full)) || !EnsureSurfaceDetailTargets(dev, full, true))
        return false;
    if(!m_noiseLattice[0]&&!DrawNoiseLattice(dev)){
        SafeRelease(m_noiseLattice[0]);SafeRelease(m_noiseLattice[1]);return false;
    }
    if(!SetPairTargets(dev, m_surfaceDetail))return false;
    const D3DVIEWPORT9 whole = {0, 0, full.Width, full.Height, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&whole)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_usingWaveMoments ? m_surfaceMomentsPs : m_surfaceDetailPs)))return false;
    if(!BindWaterTextures(dev, depthTexture, in, cfg))return false;
    if(!BindPointTexture(dev, kNoiseLatticeStage, m_noiseLattice[0]))return false;
    if(!BindPointTexture(dev, kFoamPointsStage, m_noiseLattice[1]))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

// The noise's lattice and the foam network's wandering points (noise_lattice_ps), drawn once (again after a Reset) for
// the surface detail pass to read instead of working them out (NoiseTexture, FoamPointTexture). Leaves the render
// targets, viewport and pixel shader changed.
bool Renderer::DrawNoiseLattice(IDirect3DDevice9* dev)
{
    // A texel per cell over the hash's period (water_common's kHashPeriod).
    constexpr UINT kHashPeriod = 289;
    for (IDirect3DTexture9*& target : m_noiseLattice)
        if (!target && FAILED(dev->CreateTexture(kHashPeriod, kHashPeriod, 1, D3DUSAGE_RENDERTARGET,
                                                 D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &target, nullptr)))
        {
            SafeRelease(m_noiseLattice[0]);
            SafeRelease(m_noiseLattice[1]);
            return Skip("noise lattice textures could not be created");
        }
    if(!SetPairTargets(dev, m_noiseLattice))return false;
    const D3DVIEWPORT9 whole = {0, 0, kHashPeriod, kHashPeriod, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&whole)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(BindPixelShader(dev, m_noiseLatticePs)))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    WATER_LOG_INFO("noise lattice drawn (%ux%u) for NoiseTexture / FoamPointTexture", kHashPeriod, kHashPeriod);
    return true;
}

// The surface detail pass's results for the pass about to draw (none when it did not draw: no wakes, no foam).
bool Renderer::BindSurfaceDetail(IDirect3DDevice9* dev)
{
    const bool detail=BindPointTexture(dev,kSurfaceDetailStage,m_surfaceDetailDrawn?m_surfaceDetail[0]:nullptr);
    return BindPointTexture(dev,kSurfaceSlopesStage,m_surfaceDetailDrawn?m_surfaceDetail[1]:nullptr)&&detail;
}
