#pragma once
#include "world_adt.h"
#include "owned_world_streaming.h"
#include "world_archive.h"
#include "world_model.h"
#include "world_texture_cache.h"
#include "world_wmo.h"
#include "world_horizon.h"
#include "scenery_water_surface.h"
#include <atomic>
#include <limits>
#include <memory>

namespace water::world
{
constexpr unsigned MaxLiveAssets=4;
// The reflection scene's memory, inside the game's own 32-bit address space (4 GiB, about 2.3 GiB free in play, measured at
// the Twilight Rivulet): the scene with its derived data, and that plus what one file being read can need.
constexpr std::size_t SceneBytes=128*1024*1024,SceneWorkspaceBytes=SceneBytes+32*1024*1024;
constexpr unsigned MaxSceneModels=1024,MaxSceneBuildings=256,MaxSceneTextures=1024,MaxSceneInstances=16384;
constexpr uint32_t MissingSceneAsset = std::numeric_limits<uint32_t>::max();
// Published scenes share these large CPU payloads. Builders explicitly detach before editing.
template<class T> class ScenePayload
{
public:
    const T* operator->() const { return m_value.get(); }
    bool SharesStorage(const ScenePayload& other) const { return m_value == other.m_value; }
    const T& Read() const { return *m_value; }
    T& Edit()
    {
        if (m_value.use_count()!=1) m_value = std::make_shared<T>(*m_value);
        return *m_value;
    }
    void Replace(T value) { m_value = std::make_shared<T>(std::move(value)); }
private:
    std::shared_ptr<T> m_value = std::make_shared<T>();
};
struct SceneTerrain
{
    AdtTile tile;
    std::vector<uint32_t> textures; // tile-local index -> shared scene texture
};
struct ModelGeometry
{
    StaticModel mesh;
    ModelMotion motion; // optional embedded loop for live attachments; immutable source
};
struct SceneModel
{
    ScenePayload<ModelGeometry> geometry;
    std::vector<uint32_t> textures; // model-local index -> shared scene texture
};
using SceneTextureKey = std::array<uint8_t,32>;
struct SceneTexture
{
    std::string name;
    ScenePayload<Texture> pixels;
    unsigned maxDimension=0;
    SceneTextureKey contentKey{}; // decoded mip identity for reuse across independently imported regions
};
struct SceneTextureIssue { std::string name; TextureError error = TextureError::None; unsigned retainedLevels=0, rejectedLevel=0; };
struct SceneModelSource
{
    std::string name;
    uint32_t model = MissingSceneAsset;
    ModelError unsupported = ModelError::None;
};
struct SceneWmoSource
{
    struct Attachment
    {
        uint32_t source=MissingSceneAsset; // modelSources index; shares decoding/material admission
        std::array<float,16> transform{};
        uint16_t set=0;
        bool placedAttachment=false, terrainShoreOnly=false;
    };
    std::string name;
    std::vector<uint32_t> models; // admitted group order -> owned geometry; unsupported groups reject the root
    WmoError unsupported=WmoError::None;
    uint32_t omittedDoodads=0;
    std::vector<Attachment> attachments; // group-referenced attachments for live transports and selected building placements
    WmoError attachmentError=WmoError::None; // optional metadata never retires containing geometry
};
struct SceneSource { std::string name; ArchiveIdentity archive; };
struct SceneInstance
{
    uint32_t model = 0, uid = 0;
    uint16_t group=0xffff; // M2 sentinel; WMO groups share root UID/receiver anchor
    PlacementPose pose; // stable cross-tile duplicate verification, independent of main-view draws
    std::array<float,16> transform{};
    std::array<float,3> minimum{}, maximum{};
};
struct SceneLiveAsset {uint32_t display=0,source=0;};
struct RegionGeometry
{
    std::vector<SceneTerrain> terrain;
    std::vector<SceneInstance> instances;
    std::vector<reflection::WaterSurface> water;
    std::vector<uint16_t> waterTypes; // source liquid IDs, parallel to water
    StaticModel horizon; // optional untextured WDL silhouette; separate from detailed assets
};
struct WorldScene
{
    ScenePayload<RegionGeometry> region;
    std::string sourceIdentity; // complete ordered archive view for in-process reuse
    OwnedWorldRegion sourceRegion;
    // All supported water inside this XY square around sourceRegion was admitted.
    // Withheld quads shrink it; flat receivers touching its edge may continue
    // beyond the import. Fully imported ponds keep finite reflection coverage.
    float waterCompleteReach=0;
    uint32_t sourcePolicy=0;
    std::vector<SceneModel> models;
    std::vector<SceneTexture> textures;
    std::vector<SceneModelSource> modelSources;
    std::vector<SceneWmoSource> wmoSources;
    std::vector<SceneSource> sources;
    std::vector<SceneTextureIssue> textureIssues; // unsupported optional material inputs, never GPU placeholders
    std::vector<SceneLiveAsset> liveAssets;
    HorizonError horizonError=HorizonError::Unsupported;
    unsigned horizonTiles=0;
    uint32_t omittedModelReferences = 0, omittedWmoReferences = 0, omittedFlagReferences = 0;
    uint32_t omittedWaterSurfaces = 0; // valid steep surfaces outside the receiver model
    std::size_t Bytes() const;
    std::size_t BytesExcludingTerrainPayload() const;
};
enum class SceneError { None, Descriptor, Budget, Cancelled, Archive, Terrain, Water, Model, Texture, Placement, Required, Internal, NoWater };
struct SceneResult
{
    SceneError error = SceneError::None;
    ArchiveError archiveError = ArchiveError::None;
    std::string asset;
    uint64_t sourceBytes = 0;
    double archiveMilliseconds=0, assetMilliseconds=0, derivedMilliseconds=0;
    unsigned textureCacheHits=0, textureCacheMisses=0, textureCacheFailures=0;
    TexturePackError textureCacheOpen=TexturePackError::None, textureCacheLast=TexturePackError::None;
    uint64_t textureCacheBytes=0, textureCacheBuckets=0;
    std::vector<unsigned> textureBakeCandidates; // successful source decodes, indices into immutable scene
    double textureCacheMilliseconds=0, textureSourceMilliseconds=0, textureDecodeMilliseconds=0;
    std::size_t peakWorkspaceBytes = 0; // known scene/input/candidate payload or preparation reservation; excludes archive tables/allocator
    bool reusedScenery=false;
    unsigned textureMipBias=0; // the bias the scene was imported with (raised to 1 when full detail did not fit)
    float modelReach=0; // yards models were kept to when half-resolution textures did not make it fit either; 0: all in the scene's reach
    std::array<double,6> stageMilliseconds{}; // tiles, live assets, models, buildings, textures, instances
    ArchiveStats archive;
    SceneError reuseError=SceneError::None; // refused extension falls back to complete import
};
struct SceneOptions {bool buildings=false, texturePrefix=false, lake=false, snapshotReads=false;
    bool streaming=false;OwnedWorldRegion region;std::array<uint32_t,MaxLiveAssets> liveDisplays={};unsigned textureMipBias=0;std::wstring texturePack={};std::wstring textureCacheDirectory={};bool populateTextures=false;float modelReach=0,buildingReach=0;
    ReflectionDistance distance=ReflectionDistance::Standard;
    float WaterReachLimit() const {
        if(distance==ReflectionDistance::Far)return OwnedWorldRegion::ExtendedWaterReach;
        return distance==ReflectionDistance::Short?OwnedWorldRegion::ShortWaterFarReach:OwnedWorldRegion::WaterFarReach;
    }
    float SourceReachLimit() const { return distance==ReflectionDistance::Far?OwnedWorldRegion::ExtendedSourceReach:OwnedWorldRegion::SourceReach; }
    unsigned TileLimit() const { return distance==ReflectionDistance::Far?OwnedWorldRegion::MaxExtendedTiles:OwnedWorldRegion::MaxTiles; }
};
// Bounded synchronous CPU preparation, called by the existing Water worker.
// The caller supplies an immutable archive view and cancellation epoch. No game
// pointers, D3D objects, visibility, host scheduler or public cache format.
// Regional mode selects bounded near/middle/distant ADTs around a stable local key;
// the original two-ADT scene remains the explicit comparison. Unsupported
// sources remain listed in modelSources; failures never publish partial output.
// Scene and preparation caps include payload/capacity/metadata, not allocator
// overhead or ArchiveReader tables/name cache. Failure preserves prior output.
// Texture mip bias a prepared scene was imported with (0 full, 1 authored half).
unsigned SceneTextureMipBias(const WorldScene& scene);
SceneResult LoadRiverScene(ArchiveReader& archives, const std::atomic<bool>& cancelled, WorldScene& output,
                          std::size_t sceneLimit = SceneBytes, std::size_t workspaceLimit = SceneWorkspaceBytes,
                          std::atomic<uint64_t>* sourceProgress = nullptr,SceneOptions options={});
// Extends a disposable asset catalog sharing the immutable regional payloads, importing newly observed
// live assets. Success preserves all static model/texture/terrain indices.
// Failure leaves no publishable output; callers may retry the complete importer.
SceneResult ExtendLiveScene(ArchiveReader&,const std::atomic<bool>&,WorldScene&,
                           std::size_t sceneLimit,std::size_t workspaceLimit,std::atomic<uint64_t>*,SceneOptions);
}
