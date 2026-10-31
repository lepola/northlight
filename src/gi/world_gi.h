#pragma once
#include "adaptive_geometry_array.h"

#include <cstdint>
#include <functional>
#include <string>
#include <cstring>
#include <vector>
#include <memory>
#include <atomic>
#include "memory_footprint.h"

// Independent, portable world-geometry transport. No D3D or game addresses.
namespace NorthlightGI {
namespace detail {template<bool> struct PairRay;}

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
};
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a);
Vec3 operator*(Vec3 a, float b);
Vec3 operator*(float b, Vec3 a);
Vec3 operator*(Vec3 a, Vec3 b);
Vec3 operator/(Vec3 a, float b);
float dot(Vec3 a, Vec3 b);
Vec3 cross(Vec3 a, Vec3 b);
Vec3 normalized(Vec3 a);

struct WorldVertex { Vec3 position, normal; float u = 0, v = 0; };
struct TexelView {
    const uint8_t* pointer=nullptr;size_t count=0;
    const uint8_t* data()const{return pointer;}size_t size()const{return count;}bool empty()const{return !count;}
    const uint8_t* begin()const{return pointer;}const uint8_t* end()const{return count?pointer+count:pointer;}
    const uint8_t& operator[](size_t i)const{return pointer[i];}
    bool operator==(TexelView other)const{return count==other.count&&(!count||!std::memcmp(pointer,other.pointer,count));}
    bool operator!=(TexelView other)const{return !(*this==other);}
};
struct WorldMaterial {
    // Linear diffuse reflectance multiplied by decoded sRGB texture color.
    Vec3 albedo = Vec3(.5f, .5f, .5f);
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    // Pressure builds share only completed, byte-identical immutable texels.
    std::shared_ptr<const uint8_t> sharedTexels;size_t sharedTexelBytes=0;
    TexelView texels()const{return {sharedTexels?sharedTexels.get():rgba.data(),sharedTexels?sharedTexelBytes:rgba.size()};}
    size_t texelCapacity()const{return sharedTexels?sharedTexelBytes:rgba.capacity();}
    float alphaCutoff = .5f;
    // Runtime sampler modes, matching D3DTEXTUREADDRESS: wrap=1, mirror=2, clamp=3.
    // Not serialized; cached FGS2/FGS3 materials retain the wrap default.
    uint32_t addressU = 1, addressV = 1;
    // Runtime FGS3 descriptor metadata. Category 0 is ADT terrain; plain FGS2
    // has no category field and remains false. Not serialized in FGS2.
    bool terrain = false;
    // Runtime-only FGS3 category 2. Needed to distinguish city streets/roofs
    // from trees, actors and doodads when constructing the outdoor fog floor.
    bool wmo = false;
    bool pointOnly = false; // Runtime auxiliary transmission geometry; never part of the GI BVH.
    uint32_t lampLocalId = 0; // FGS2 lamp fixture tag, zero for ordinary materials.
    uint64_t lampSourceId = 0; // Exact FGL source for this FGS3 instance, or zero when unknown.
};
struct WorldTriangle { uint32_t v0 = 0, v1 = 0, v2 = 0, material = 0; };
// Exact placements whose entire model triangle set survived the local crop.
// Runtime-only ownership evidence; FGS cache files are immutable during play.
struct WorldPlacementCoverage {
    uint64_t uid=0; uint32_t category=0; std::string modelKey;
    float matrix[9]={}; Vec3 translation,low,high;
};
struct SourcePieceRange {
    uint64_t sourceIdentity=0,membership[2]={};
    WorldPlacementCoverage placement;
    uint32_t vertexBase=0,vertexCount=0,triangleBase=0,triangleCount=0;
    std::vector<uint32_t> materialMap; // Ordered global material IDs for preparation signatures; may omit unused slots.
    bool complete=false;
};
struct WorldScene {
    uint64_t sourceIdentity=0; // Validated decoded-source revision; 0 means unknown.
    AdaptiveGeometryArray<WorldVertex> vertices;
    AdaptiveGeometryArray<WorldTriangle> triangles;
    std::vector<WorldMaterial> materials;
    std::vector<WorldPlacementCoverage> completePlacements;
    std::vector<SourcePieceRange> sourcePieces; // Lightweight loader provenance; no raw source ownership.
    // Dynamic storage only: the containing BVH/cache already counts the object.
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const {
        using namespace NorthlightMemoryFootprint;
        vertices.accountMemory(memory,Vertices);triangles.accountMemory(memory,Triangles);memory.vector(Metadata,materials);
        for(const auto& material:materials){if(material.sharedTexels){if(memory.visit(material.sharedTexels.get())){memory.add(Texels,material.texelCapacity());memory.add(Metadata,64);}}else memory.vector(Texels,material.rgba);}
        memory.vector(Metadata,completePlacements);for(const auto& p:completePlacements)memory.string(p.modelKey);
        memory.vector(Metadata,sourcePieces);for(const auto& p:sourcePieces){memory.string(p.placement.modelKey);memory.vector(Metadata,p.materialMap);}
    }
    size_t retainedBytes()const {
        size_t n=sizeof(*this)+vertices.capacity()*sizeof(WorldVertex)+vertices.overhead()+triangles.capacity()*sizeof(WorldTriangle)+triangles.overhead()+materials.capacity()*sizeof(WorldMaterial)+completePlacements.capacity()*sizeof(WorldPlacementCoverage);
        for(size_t i=0;i<materials.size();++i){const auto& m=materials[i];bool seen=false;if(m.sharedTexels)for(size_t j=0;j<i;++j)if(materials[j].sharedTexels==m.sharedTexels){seen=true;break;}if(!seen)n+=m.texelCapacity()+(m.sharedTexels?64:0);}
        for(const auto& p:completePlacements)n+=p.modelKey.capacity();
        n+=sourcePieces.capacity()*sizeof(SourcePieceRange);
        for(const auto& p:sourcePieces)n+=p.placement.modelKey.capacity()+p.materialMap.capacity()*sizeof(uint32_t);
        return n;
    }
};

// FGS2 little-endian binary produced by world_scene_builder.py. Failure leaves
// output unchanged. Limits prevent corrupt counts from requesting unbounded RAM.
using AllocationAdmission = std::function<bool(uint64_t)>;
using SceneSourceRevision = std::function<uint64_t(const std::string&)>;
using SceneSourceProvider = std::function<std::shared_ptr<const WorldScene>(const std::string&,std::string&,const AllocationAdmission&)>;
// Optional stage-local guard immediately before actual large allocations.
bool loadScene(const char* path, WorldScene& output, std::string& error,
               const AllocationAdmission& admit = {});
// Keep triangles whose AABBs overlap the local-world region. Remaps referenced
// vertices/materials and moves textures. The box should include a caster/indirect
// margin around the probe grid. Returns false without modifying data on error.
bool cropScene(WorldScene& scene, Vec3 low, Vec3 high, std::string& error);
// FGS3 tile descriptors reference shared FGS2 models by SHA-256 filename. The
// caller selects existing nearby tile descriptor paths from one map. Instances
// are culled by world bounds and deduplicated across tiles by category and UID.
// Shared models are loaded once per request, one at a time to bound peak RAM.
// modelDirectory is the cache's "models" directory, without a required slash.
// Optional in/out sizing hint for repeated builds of one overlapping region (the world terrain builder). The
// caller sets `key` (e.g. map + reach) before the call; the hint applies only when it equals `sizedKey` and the
// requested region overlaps the recorded one (IoU >= 1/2): vertices/triangles are then reserved once at the
// recorded counts + 1/16 (caps 4,000,000 / 6,000,000, under `admit`) and later growth is +1/4 instead of x2.
// A denied reserve falls back to normal growth without an error. Success records counts/region/key; `hinted` is
// an out flag for the last call. Failure leaves everything but `hinted` untouched.
struct SceneSizeHint {
    uint64_t key=0,sizedKey=0;bool sized=false,hinted=false;
    uint64_t vertices=0,triangles=0;Vec3 low,high;
};
bool loadInstancedScenes(const std::vector<std::string>& tileFiles,
                         const std::string& modelDirectory,
                         Vec3 low, Vec3 high, WorldScene& output,
                         std::string& error, uint32_t categoryMask = 15,
                         const AllocationAdmission& admit = {},
                         const std::function<bool(Vec3,Vec3)>& instanceFilter = {},
                         const std::function<bool(Vec3,Vec3)>& terrainChunkFilter = {},
                         SceneSizeHint* sizeHint = nullptr,
                         const SceneSourceProvider& sourceProvider = {},
                         bool terrainChunkProvenance = false, bool chunkedTerrain = false, bool spooledTerrain = false);

// Counts actual live optional acceleration storage, including published regions
// after a cache reset. A refused reservation selects the legacy full-scene path.
struct SceneMemoryBudget {
    const uint64_t limit;
    std::atomic<uint64_t> used{0};
    explicit SceneMemoryBudget(uint64_t bytes):limit(bytes){}
};
struct SceneMemoryCharge {
    std::shared_ptr<SceneMemoryBudget> budget;uint64_t bytes=0;
    ~SceneMemoryCharge(){if(budget)budget->used.fetch_sub(bytes,std::memory_order_relaxed);}
    static std::shared_ptr<const SceneMemoryCharge> acquire(const std::shared_ptr<SceneMemoryBudget>& budget,uint64_t bytes){
        auto used=budget->used.load(std::memory_order_relaxed);
        do{if(bytes>budget->limit||used>budget->limit-bytes)return {};}
        while(!budget->used.compare_exchange_weak(used,used+bytes,std::memory_order_relaxed));
        try{auto charge=std::make_shared<SceneMemoryCharge>();charge->budget=budget;charge->bytes=bytes;return charge;}
        catch(...){budget->used.fetch_sub(bytes,std::memory_order_relaxed);throw;}
    }
};

struct RayHit {
    bool hit = false;
    // Original outward shading-normal side, before double-sided face forwarding.
    bool backFace = false;
    float distance = 0;
    Vec3 position, normal, geometricNormal, albedo;
    uint32_t triangle = UINT32_MAX;
};

class BVH {
public:
    struct PieceReference {
        std::shared_ptr<const BVH> geometry;
        uint32_t vertexBase=0,triangleBase=0;
        std::vector<uint32_t> materialMap;
        WorldPlacementCoverage placement;
        bool complete=false;
    };
    BVH()=default;
    BVH(const BVH&)=delete;BVH& operator=(const BVH&)=delete;
    BVH(BVH&&)=default;BVH& operator=(BVH&&)=default;
    // Takes scene ownership. A completed BVH is immutable and concurrent reads
    // are safe. Do not call build concurrently with trace/solveProbe.
    // fast=false (northlight-quality GIFastBVH=0) builds the 0.3.137 median tree;
    // every RayHit and Probe is then bit-identical to 0.3.137. fast=true builds
    // the SAH tree: equal-distance ties go to the lowest triangle index.
    bool build(WorldScene&& scene, std::string& error, bool fast = true,const AllocationAdmission& admit = {});
    // Direction is normalized internally, so distances are in world units.
    // Surfaces are double-sided; texture alpha cutouts apply to every ray type.
    RayHit trace(Vec3 origin, Vec3 direction, float minDistance = .001f,
                 float maxDistance = 256.f) const;
    bool occluded(Vec3 origin, Vec3 direction, float maxDistance = 256.f,
                  float minDistance = .001f) const;
    const WorldScene& scene() const { return scene_; }
    const std::vector<PieceReference>& pieces()const{return pieces_;}
    const WorldMaterial& material(size_t index)const;
    size_t triangleCount() const { return region_?regionTriangles_:order_.size(); }
    size_t fallbackTriangleCount()const{return region_?order_.size():0;}
    // Call only on a completed pressure candidate before publication.
    void shareMaterialTexels();
    size_t ownBytes()const;
    size_t retainedBytes()const;
    void accountMemory(NorthlightMemoryFootprint::Counter&)const;
    static uint64_t buildScratchBytes(size_t triangles);
private:
    friend class LocalSceneCache;
    template<bool AnyHit,bool Sah>
    RayHit traceImpl(Vec3 origin,Vec3 direction,float minDistance,float maxDistance,bool normalizedRay=false,float checkedEntry=-1,float* barycentricOut=nullptr,const detail::PairRay<Sah>* preparedRay=nullptr)const;
    template<bool AnyHit> RayHit traceRegion(Vec3 origin,Vec3 direction,float minDistance,float maxDistance)const;
    struct MaterialBank {std::vector<WorldMaterial> values;std::shared_ptr<const SceneMemoryCharge> charge;};
    std::shared_ptr<const MaterialBank> materialBank_;
    std::vector<uint32_t> materialSources_;
    std::shared_ptr<const SceneMemoryCharge> retainedCharge_;
    std::vector<PieceReference> pieces_;
    std::vector<uint32_t> regionOrder_;
    bool region_=false;size_t regionTriangles_=0;
    bool buildImpl(WorldScene&&,std::string&,bool,std::shared_ptr<const MaterialBank>,std::vector<uint32_t>,const AllocationAdmission& admit={});
    bool buildRegion(WorldScene&&,std::vector<PieceReference>&&,std::string&,const AllocationAdmission&,const std::shared_ptr<SceneMemoryBudget>&);
    void buildSahTree();
    void shadeHit(RayHit&,Vec3 origin,Vec3 direction,uint32_t triangle,float u,float v,float distance)const;
    uint32_t makeRegionNode(uint32_t start,uint32_t count,uint32_t depth=0);
    struct Node { Vec3 low, high; uint32_t start = 0, count = 0, left = 0, right = 0; };
    // Interior node holding both child boxes (lanes per axis: left low, right
    // low, left high, right high) so one fetch serves one two-box slab test.
    // Median tree (identical to Node) or SAH tree (TraceSah, world_gi.cpp).
    // child: pair index, or LeafBit|start<<3|count.
    struct Pair { float axis[3][4]; uint32_t child[2]; uint32_t pad[2]; };
    static constexpr uint32_t LeafBit = 0x80000000u;
    WorldScene scene_;
    AdaptiveGeometryArray<Node> nodes_;
    AdaptiveGeometryArray<Pair> pairs_;
    AdaptiveGeometryArray<Pair> regionPairs_;
    Vec3 regionLow_,regionHigh_;uint32_t regionRoot_=0;
    Vec3 rootLow_, rootHigh_;
    uint32_t root_ = 0, depth_ = 0;
    bool sah_ = false;
    AdaptiveGeometryArray<uint32_t> order_;
    // Per material: no bilinear alpha sample can be below alphaCutoff.
    std::vector<uint8_t> opaque_;
    AdaptiveGeometryArray<Vec3> centers_; // build-time only
    uint32_t partition(uint32_t start, uint32_t count, Vec3& low, Vec3& high);
    struct BuildBox { Vec3 low, high, cl, ch; };
    BuildBox bounds(uint32_t start, uint32_t count) const;
    uint32_t sahSplit(uint32_t start, uint32_t count, const BuildBox& box, BuildBox (&child)[2]);
    uint32_t makeSah(uint32_t start, uint32_t count, const BuildBox& box);
    uint32_t makeNode(uint32_t start, uint32_t count);
    uint32_t makePair(uint32_t start, uint32_t count, Vec3& low, Vec3& high);
};

// Worker-owned incremental local geometry. Immutable placement pieces share
// transformed geometry between adjacent crops.
// Fast regions reuse admitted piece BLAS and build a TLAS plus a flat fallback
// tree over unadmitted triangles. Median regions retain the original flat BVH.
// scene() remains the exact legacy flat transport used by GPU upload and fog.
class LocalSceneCache {
public:
    void accountMemory(NorthlightMemoryFootprint::Counter&)const;
    struct Limits { size_t retainedBytes=32u*1024u*1024u; };
    struct Stats {
        uint64_t modelReads=0,pieceBuilds=0,pieceReuses=0,reusedTriangles=0,builtTriangles=0,hintedBuilds=0;
        // Capacity-based payload estimate; peak excludes prior published
        // generations, allocator overhead and temporary vector growth.
        uint64_t retainedBytes=0,peakBytes=0,flatBytes=0,bvhBytes=0;
        double loadMs=0,pieceMs=0,assembleMs=0,bvhMs=0,totalMs=0;
        uint64_t blasBuilds=0,blasReuses=0,blasTrianglesBuilt=0,blasTrianglesReused=0,tlasPieces=0,legacyFallbacks=0;
        uint64_t sharedLiveBytes=0,compatibilityBytes=0,tlasBytes=0,fallbackTriangles=0,mandatoryTreeBytes=0;
        uint64_t chunkBuilds=0,chunkReuses=0,chunkTrianglesBuilt=0,chunkTrianglesReused=0;
    };
    LocalSceneCache();
    explicit LocalSceneCache(Limits);
    ~LocalSceneCache();
    LocalSceneCache(const LocalSceneCache&)=delete;
    LocalSceneCache& operator=(const LocalSceneCache&)=delete;
    bool build(const std::vector<std::string>& tiles,const std::string& modelDirectory,
               const std::string& map,Vec3 low,Vec3 high,BVH& output,
               std::string& error,const AllocationAdmission& admit={},bool fast=true,
               const SceneSourceProvider& sourceProvider={},const SceneSourceRevision& sourceRevision={},bool chunked=false);
    void reset();
    const Stats& stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct DirectionalLight { Vec3 direction, irradiance; };
struct PointLight {
    Vec3 position, irradiance;
    // Authored attenuation: full inside start, linear falloff to zero at end.
    float attenuationStart=0, attenuationEnd=0;
};
struct Lighting {
    // Unit vector toward the sun/moon; scene convention is Z up.
    Vec3 sunDirection = Vec3(.35f, -.4f, .85f);
    // Irradiance on a plane perpendicular to the directional light.
    Vec3 sunIrradiance = Vec3(1.f, .95f, .85f);
    // Constant upper-hemisphere environmental radiance. Lower hemisphere is 0.
    Vec3 skyRadiance = Vec3(.10f, .14f, .20f);
    Vec3 worldUp = Vec3(0, 0, 1);
    float maxDistance = 256.f;
    float rayBias = .006f;
    unsigned maxBounces = 3;
    std::vector<DirectionalLight> additionalDirections;
    std::vector<PointLight> points;
    // Borrowed immutable overlay, owned by the solve request's worker scope.
    const BVH* movingGeometry = nullptr;
};

// Immutable solve setup for one static lighting generation. The moving overlay
// is deliberately supplied per solve; preparing never retains a borrowed BVH.
class PreparedLighting {
    Lighting lighting_;
    bool valid_ = false;
    friend PreparedLighting prepareLighting(const Lighting&);
public:
    bool valid() const { return valid_; }
    const Lighting& values() const { return lighting_; }
};
PreparedLighting prepareLighting(const Lighting& lighting);

struct DistanceMoment { float mean = 0, meanSquare = 0; };
struct Probe {
    Vec3 position;
    // Real SH radiance coefficients ordered Y00, Y1x, Y1y, Y1z. Projection uses
    // solid angle 4*pi/N. Evaluate with the cosine convolution below.
    Vec3 sh[4];
    // +X,-X,+Y,-Y,+Z,-Z; cosine-power-weighted first-hit distances.
    DistanceMoment moments[6];
    unsigned samples = 0;
    unsigned backFaceSamples = 0;
    float maxDistance = 0;
    bool valid = false;
};

// Deterministic stratified sphere rays and cosine-weighted diffuse scattering.
// Each path includes up to maxBounces actual material interactions, directional
// light visibility at every interaction, and sky at escape. No screen buffers.
// Probes with >25% first-hit back faces or a surface within .02 units are marked
// invalid to keep subterranean/inside-solid probes out of interpolation.
Probe solveProbe(const BVH& bvh, Vec3 position, const Lighting& lighting,
                 unsigned rays = 32, uint32_t seed = 1);
Probe solveProbePrepared(const BVH& bvh, Vec3 position, const PreparedLighting& lighting,
                         unsigned rays = 32, uint32_t seed = 1,
                         const BVH* movingGeometry = nullptr);
// Colour-independent transport for the renderer's sun, sky and one secondary
// direction. Point-light irradiance is fixed by the cache's geometry signature.
// Relighting changes only floating-point grouping, not paths or sample counts.
struct ProbeResponse { Probe fixed; Vec3 sources[3][4]; };
Probe solveProbeResponse(const BVH& bvh,Vec3 position,const PreparedLighting& lighting,
                         unsigned rays,uint32_t seed,ProbeResponse& response);
Probe relightProbe(const ProbeResponse& response,const PreparedLighting& lighting);
// Static-only paths of one probe's rays, stored as the exact moving-geometry
// queries a moving solve issues while its path equals the static path. Owned
// per probe by the caller; valid for one static BVH/lighting generation.
struct StaticPathRecord {
    struct Query { Vec3 origin, direction; float maxDistance = 0; };
    struct Ray { Vec3 color; float firstDistance = 0; uint32_t queryEnd = 0; bool backFace = false; };
    std::vector<Ray> rays;
    std::vector<Query> queries;
    uint64_t generation = 0; const BVH* bvh = nullptr; Vec3 position; unsigned rayCount = 0; uint32_t seed = 0;
    size_t retainedBytes() const { return rays.capacity()*sizeof(Ray)+queries.capacity()*sizeof(Query); }
};
struct MovingSolveStats { uint64_t recorded = 0, replayed = 0, retraced = 0; };
// Bit-identical to solveProbePrepared(..., &moving). A ray whose recorded
// moving queries all miss reuses its static result; any hit retraces the ray.
// The record is (re)built when generation (nonzero), bvh, position, rays or
// seed differ. The caller must change generation whenever lighting changes.
Probe solveProbeMoving(const BVH& bvh, Vec3 position, const PreparedLighting& lighting,
                       unsigned rays, uint32_t seed, const BVH& moving,
                       StaticPathRecord& record, uint64_t generation,
                       MovingSolveStats* stats = nullptr);
Vec3 evaluateIrradiance(const Probe& probe, Vec3 surfaceNormal);
// Chebyshev-style leak rejection; a heuristic, not an exact visibility query.
float probeVisibility(const Probe& probe, Vec3 probeToReceiver, float distance,
                      float bias = .05f);

} // namespace NorthlightGI
