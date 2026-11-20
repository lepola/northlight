#pragma once
#include "world_scene.h"
#include "owned_receiver_views.h"
#include <atomic>

namespace water
{
struct OwnedTerrainRenderData
{
    std::vector<float> waterHeights; // one per retained vertex, established vertical warp
    std::vector<uint32_t> vertexOffsets; // cell starts plus terminal size
    std::vector<uint8_t> alphaAtlas; // BGRA: layer 3/2/1 and source shadow, packed admitted cells
    unsigned alphaWidth=0,alphaHeight=0;
    std::vector<std::array<float,4>> alphaRects; // UV origin and span to texel centres
    std::vector<std::array<float,6>> reflectedBounds,sourceBounds;
    std::array<float,6> sourceExtent={};
    // Four-by-four source-cell groups allow conservative rejection before the
    // material-ordered cell loop, preserving its batching and draw order.
    static constexpr unsigned GroupCount=16;
    static unsigned Group(unsigned x,unsigned y){return (y/4)*4+x/4;}
    std::array<std::array<float,6>,GroupCount> sourceGroups={};
    uint16_t groupMask=0;
};
// Static instances in a spatial bucket. One conservative test can reject all
// its copies for a small receiver; accepted copies still use the exact test.
struct OwnedInstanceGroup
{
    std::array<float,6> bounds={};
    std::vector<unsigned> instances;
};
struct OwnedWorldRenderData
{
    std::vector<OwnedTerrainRenderData> terrain;
    std::vector<float> instanceHeights;
    std::vector<OwnedInstanceGroup> instanceGroups;
    OwnedWaterReceivers receivers;
    std::vector<OwnedReceiverView> views;
    std::vector<OwnedReceiverVertex> receiverVertices;
    unsigned receiverBody=OwnedWaterReceivers::Missing;
    bool horizontalReceiver=false;
    float receiverHeight=0;
    std::size_t workspaceReservationBytes = 0;
    bool HasHorizontalView()const {
        return horizontalReceiver || std::any_of(views.begin(),views.end(),[](const auto& view){return view.horizontal;});
    }
    std::size_t Bytes() const;
};
enum class OwnedWorldDataResult { Ready, Cancelled, Invalid, Budget };
// CPU worker only. Optional SHA-256 identities never replace source validation;
// missing crypto support leaves zero keys and the ordinary upload path intact.
bool PrepareOwnedTextureKeys(world::WorldScene&,const std::atomic<bool>&,bool extension=false);
// Append optional far silhouettes after mandatory scenery/derived data fit. A
// missing, malformed or over-budget WDL never rejects the usable detailed scene.
void PrepareOwnedWorldHorizon(world::ArchiveReader&,world::WorldScene&,const OwnedWorldRenderData&,
                             const std::atomic<bool>&,world::SceneResult&);
// CPU worker only, before immutable publication. Reuses WaterHeightField for
// terrain/rigid-object placement; no camera, game pointers or D3D calls. Consumes
// raw layer alpha and shadow masks after their atlas is built. Caller exclusively owns the candidate
// asset scene and discards it on failure; accepted/active scenes are never edited.
OwnedWorldDataResult PrepareOwnedWorldRenderData(world::WorldScene& scene, const std::atomic<bool>& cancelled,
                                OwnedWorldRenderData& output, std::size_t residentLimit=world::SceneBytes,
                                std::size_t workspaceLimit=world::SceneWorkspaceBytes,const float* receiver=nullptr);
}
