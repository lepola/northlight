#pragma once
#include "reflection/owned_world_render_data.h"
#include "frame/frame_inputs.h"
#include <d3d9.h>

namespace water
{
enum class OwnedWorldUpload { Pending, Ready, Failed };
struct OwnedWorldVertex { float position[3],normal[3],uv[2],water; };
// One model copy in an instanced draw: placement rows (as c6-c8) and draw mode (as c10).
struct OwnedWorldInstance { float world[3][4],draw[4]; };
// The enclosing renderer's shaders: one draw per placement, instanced (models) and terrain (alpha atlas per vertex).
struct OwnedWorldShaders
{
    IDirect3DVertexShader9* vs=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
    IDirect3DVertexShader9* instancedVs=nullptr;IDirect3DVertexDeclaration9* instancedDeclaration=nullptr;
    IDirect3DVertexShader9* terrainVs=nullptr;IDirect3DVertexDeclaration9* terrainDeclaration=nullptr;
};
// Each terrain vertex's alpha atlas coordinate, stored after the tile's vertices.
constexpr unsigned kTerrainAlphaStride=12; // alpha atlas coordinate and painted colour
// How a reflection draw may be cut down.
struct OwnedWorldDrawOptions
{
    // Skip terrain cells and objects whose whole reflection lands on water the game's fog fully covers, allowing the
    // reflection lookup's largest shift (with WaterGameFog), a fraction of the view's width.
    bool fogSkip=false;float warp=0;
    // ReflectionSceneParts, a diagnostic: which parts of the scene are drawn (all by default). Scissor keeps every
    // draw but rasterises one pixel, separating submission cost from pixel cost.
    enum Part : unsigned {Terrain=1,Models=2,Horizon=4,Live=8,Animation=16,All=31,Scissor=32};
    unsigned parts=All;
    // Also fill the stats' screen sizes and merge census (only for frames whose stats are logged).
    bool census=false;
    unsigned receiver=OwnedWaterReceivers::Missing;
    RECT receiverArea={};bool receiverAreaKnown=false;
};
// What the last draw did. With census, batches are bucketed by the screen size of their object's reflected box, in full-screen
// pixels: below 2, 4, 8, 16, 32, 64 and from 64 up (terrain and horizon go in the last).
struct OwnedWorldDrawStats
{
    static constexpr unsigned kSizes=7;
    // Batches are the copies' batches drawn (one draw each without instancing); draws are the draw calls made.
    // fogSkipped: reflections landing only on fully fogged water; pastFog: mirror images wholly past the fog's end.
    unsigned terrainCells=0,instances=0,batches=0,draws=0,fogSkipped=0,pastFog=0,liveDraws=0;
    // How far the draws could merge: distinct (tile, layer textures, layer motion) among drawn cells; distinct
    // models among drawn instances and the batches those distinct models have.
    unsigned terrainSets=0,models=0,modelBatches=0;
    uint64_t triangles=0;
    unsigned sizeBatches[kSizes]={};
    uint64_t sizeTriangles[kSizes]={};
    double poseMs=0;
};
static_assert(sizeof(OwnedWorldVertex)==36,"owned world vertex declaration");
// GPU-owner resources only. The enclosing owned renderer captures/restores host
// state and owns common targets/shaders. This component never reads files or
// submits work on a second D3D device/thread. CPU inputs remain immutable/alive.
class OwnedWorldGpu
{
public:
    static constexpr std::size_t ResidencyLimit=128*1024*1024;
    // Reserved for the instanced model stream, part of Bytes() from the start of an upload.
    static std::size_t InstanceStreamBytes(const world::WorldScene&);
    OwnedWorldGpu() : m_owner(GetCurrentThreadId()) {}
    ~OwnedWorldGpu() { Release(); }
    OwnedWorldGpu(const OwnedWorldGpu&)=delete;
    OwnedWorldGpu& operator=(const OwnedWorldGpu&)=delete;
    OwnedWorldUpload Upload(IDirect3DDevice9*,const world::WorldScene&,const OwnedWorldRenderData&,unsigned byteBudget,
                            std::size_t resourceBudget=ResidencyLimit,const OwnedWorldGpu* reusable=nullptr);
    bool Draw(IDirect3DDevice9*,const FrameInputs&,const world::WorldScene&,const OwnedWorldRenderData&,const OwnedWorldShaders&);
    bool DrawReceivers(IDirect3DDevice9*,const FrameInputs&);
    void Release();
    bool Ready(const world::WorldScene& scene) const {return m_ready && m_scene==&scene;}
    std::size_t Bytes() const {return m_bytes;}
    std::size_t StagingBytes() const {return m_stagingBytes;}
    unsigned Uploads() const {return m_uploads;}
    unsigned Draws() const {return m_draws;}
    unsigned LiveDraws() const {return m_stats.liveDraws;}
    void SetOptions(const OwnedWorldDrawOptions& options) {m_options=options;}
    const OwnedWorldDrawStats& Stats() const {return m_stats;}
    std::size_t ReusedTextureBytes() const {return m_reusedTextureBytes;}
    std::size_t ReusedGeometryBytes() const { return m_reusedGeometryBytes; }
    unsigned ReusedTextures() const {return m_reusedTextures;}
private:
    struct Range {unsigned first=0,count=0,vertex=0,vertices=0;};
    // Terrain: cells' index ranges are laid out in order (cells grouped by layer set), and indices are tile-absolute.
    struct TerrainLayout {
        std::vector<Range> cells;
        std::vector<unsigned> order,sets;
    };
    struct Mesh {
        IDirect3DVertexBuffer9* vertices=nullptr;
        IDirect3DIndexBuffer9* indices=nullptr;
        unsigned vertexCount=0,indexCount=0;
        std::shared_ptr<const TerrainLayout> layout;
        uint32_t poseTime=0;
        bool sharedBuffers=false,hasPose=false;
        void ShareBuffers(const Mesh& source);
    };
    enum class Phase { TerrainVertices,TerrainIndices,ModelVertices,ModelIndices,Textures,Alpha,Receivers,Complete };
    bool Initialize(const world::WorldScene&,const OwnedWorldRenderData&,std::size_t resourceBudget,const OwnedWorldGpu* resident);
    void ReuseTextures(const OwnedWorldGpu&);
    bool Buffer(IDirect3DDevice9*,const world::WorldScene&,const OwnedWorldRenderData&,unsigned& budget);
    bool m_newPoseScene=true;
    bool CascadeClock(const FrameInputs&,const world::SceneInstance&,const world::SceneModel&,bool nativeVisible,uint32_t&);
    bool UpdatePose(Mesh&,const world::SceneModel&,uint32_t time);
    bool ReceiverBuffer(IDirect3DDevice9*,const OwnedWorldRenderData&,unsigned&);
    bool Texture(IDirect3DDevice9*,const world::WorldScene&,const OwnedWorldRenderData&,unsigned& budget);
    template<class BatchState,class Submitted>
    bool DrawQueued(IDirect3DDevice9*,const world::WorldScene&,const OwnedWorldShaders&,const BatchState&,const Submitted&);
    const DWORD m_owner;
    const world::WorldScene* m_scene=nullptr;
    const OwnedWorldRenderData* m_render=nullptr;
    IDirect3DDevice9* m_device=nullptr; // borrowed
    std::vector<Mesh> m_terrain,m_models;
    std::vector<IDirect3DVertexBuffer9*> m_receiverBuffers; // mask, then each view
    std::vector<unsigned> m_terrainVertexOffsets,m_terrainCellOffsets;
    std::vector<IDirect3DTexture9*> m_textures,m_alpha;
    std::vector<unsigned> m_textureFirstMip;
    std::vector<uint8_t> m_textureReused;
    IDirect3DPixelShader9* m_cascadeShader=nullptr;
    bool m_cascadeReported=false, m_cascadeUnavailable=false;
    IDirect3DTexture9* m_staging=nullptr;
    unsigned m_stageWidth=0,m_stageHeight=0,m_stageLevels=0;
    D3DFORMAT m_stageFormat=D3DFMT_UNKNOWN;
    Phase m_phase=Phase::TerrainVertices;
    unsigned m_item=0,m_offset=0,m_level=0,m_row=0;
    unsigned m_uploads=0,m_draws=0;
    OwnedWorldDrawOptions m_options;
    OwnedWorldDrawStats m_stats;
    std::vector<uint64_t> m_census;
    // Visible model copies of the current draw, grouped by model when drawn; the instance stream they fill.
    struct Queued {unsigned model;OwnedWorldInstance instance;};
    std::vector<Queued> m_queued;
    std::vector<unsigned> m_modelOffsets;
    std::vector<unsigned> m_instanceOrder;
    IDirect3DVertexBuffer9* m_instances=nullptr;unsigned m_instanceCapacity=0;
    std::size_t m_bytes=0,m_stagingBytes=0,m_expectedBytes=0;
    std::size_t m_reusedTextureBytes=0,m_reusedGeometryBytes=0;
    bool m_sharedRegion=false;
    unsigned m_reusedTextures=0;
    bool m_ready=false,m_failed=false;
    HRESULT m_result=S_OK; // last resource creation/lock result, for the refusal log
    std::size_t m_planned=0,m_gpuLimit=0; // the last upload plan and its budget, for the refusal log
    bool Fail(const char* reason);
};
}
