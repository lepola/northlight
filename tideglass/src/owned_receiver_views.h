#pragma once
#include "owned_water_receivers.h"

namespace water
{
// Flat receiving patches share a mirror at their own elevation, even when a
// sloping channel connects them. Connectivity alone cannot choose a mirror:
// warping a bank toward another pool's height stretches its reflection into a
// column. Sloped patches keep their body's established height field. These
// groups never control asset residency.
struct OwnedReceiverView
{
    unsigned body=OwnedWaterReceivers::Missing;
    bool horizontal=false;
    float height=0;
    // Only flat views continue beyond imported water. Disjoint native-height
    // intervals prevent a nearby canal or pond from taking the ocean fallback.
    std::array<float,2> continuation={1,0};
    std::array<float,6> bounds={};
    std::vector<unsigned> surfaces;
    // Immutable spatial groups accelerate the exact wet-quad screen union.
    // They only reject work; a group's empty land never becomes receiving water.
    struct SurfaceGroup {std::array<float,6> bounds;unsigned first,count;};
    std::vector<SurfaceGroup> surfaceGroups;
    // Sloped views only. Packed in the same terrain/tile/vertex order as the
    // shared meshes, so switching views changes a small height stream, not meshes.
    std::vector<float> terrainHeights,instanceHeights;
    std::vector<std::array<float,6>> reflectedBounds,terrainExtents,terrainGroups,instanceGroupBounds;
    std::size_t Bytes()const {
        return surfaceGroups.capacity()*sizeof(SurfaceGroup)+surfaces.capacity()*sizeof(unsigned)+(terrainHeights.capacity()+instanceHeights.capacity())*sizeof(float)+
            (reflectedBounds.capacity()+terrainExtents.capacity()+terrainGroups.capacity()+instanceGroupBounds.capacity())*sizeof(std::array<float,6>);
    }
};
struct OwnedReceiverVertex {float position[3],view;};

inline bool GroupReceiverViews(const std::vector<reflection::WaterSurface>& water,
                              const OwnedWaterReceivers& bodies,const std::atomic<bool>& cancelled,
                              std::vector<OwnedReceiverView>& views,std::vector<OwnedReceiverVertex>& vertices)
{
    views.clear();vertices.clear();
    for(unsigned i=0;i<water.size();++i){
        if(cancelled.load())return false;
        const auto& s=water[i];const auto body=bodies.Body(i);
        if(body>=water.size())return false;
        // Test the authored height range, not just the fitted normal: a warped
        // quad can fit a horizontal plane while its corners are not level.
        const bool flat=s.bounds[5]-s.bounds[2]<.02f;
        const float height=.5f*(s.bounds[2]+s.bounds[5]);
        auto match=std::find_if(views.begin(),views.end(),[&](const auto& v){
            return v.horizontal==flat && (flat?std::abs(v.height-height)<.02:v.body==body);
        });
        if(match==views.end()){
            OwnedReceiverView view;view.body=body;view.horizontal=flat;view.height=height;view.bounds=s.bounds;
            views.push_back(std::move(view));match=views.end()-1;
        }
        match->surfaces.push_back(i);
        for(unsigned c=0;c<3;++c){match->bounds[c]=std::min(match->bounds[c],s.bounds[c]);match->bounds[c+3]=std::max(match->bounds[c+3],s.bounds[c+3]);}
    }
    for(auto& view:views){
        const auto key=[&](unsigned index){const auto& s=water[index];
            return std::make_pair(std::floor(s.anchor[0]/64),std::floor(s.anchor[1]/64));};
        std::sort(view.surfaces.begin(),view.surfaces.end(),[&](unsigned a,unsigned b){
            const auto ka=key(a),kb=key(b);return ka==kb?a<b:ka<kb;});
        for(unsigned first=0;first<view.surfaces.size();){
            if(cancelled.load())return false;
            const auto bucket=key(view.surfaces[first]);
            OwnedReceiverView::SurfaceGroup group{water[view.surfaces[first]].bounds,first,0};
            do {
                const auto& bounds=water[view.surfaces[first+group.count]].bounds;
                for(unsigned c=0;c<3;++c){group.bounds[c]=std::min(group.bounds[c],bounds[c]);
                    group.bounds[c+3]=std::max(group.bounds[c+3],bounds[c+3]);}
                ++group.count;
            }while(first+group.count<view.surfaces.size() && key(view.surfaces[first+group.count])==bucket);
            first+=group.count;view.surfaceGroups.push_back(group);
        }
    }
    // The mask uses source water geometry, not the opaque scene or a character
    // silhouette. Colour IDs start at one; zero is clear/no receiving water.
    for(unsigned i=0;i<views.size();++i)for(unsigned index:views[i].surfaces){
        if(cancelled.load())return false;
        const auto& s=water[index];OwnedReceiverVertex corners[4];
        for(unsigned c=0;c<4;++c){
            const float x=s.bounds[(c&1)?3:0],y=s.bounds[(c&2)?4:1];
            const float z=float(s.anchor[2]-(s.normal[0]*(x-s.anchor[0])+s.normal[1]*(y-s.anchor[1]))/s.normal[2]);
            corners[c]={{x,y,z},float(i+1)};
            if(s.cornersKnown)for(unsigned axis=0;axis<3;++axis)corners[c].position[axis]=s.corners[c][axis];
        }
        for(unsigned c:{0u,1u,2u,2u,1u,3u})vertices.push_back(corners[c]);
    }
    return !views.empty();
}
// Only receivers that reach outside the completely admitted water square need
// continuation. Never turn every small local pond into an unbounded mirror.
inline void SetReceiverContinuations(std::vector<OwnedReceiverView>& views,
                                     const std::array<float,2>& centre,float completeReach)
{
    for(auto& view:views){
        view.continuation={1,0};
        bool complete=completeReach>0;
        for(unsigned c=0;c<2;++c)complete &= view.bounds[c]>centre[c]-completeReach+.02f &&
                                               view.bounds[c+3]<centre[c]+completeReach-.02f;
        if(complete)continue;
        if(view.horizontal){
            // Match the established same-water tolerance, bounded at the
            // midpoint to every other flat level so fallback ownership is unique.
            view.continuation={view.height-.75f,view.height+.75f};
            for(const auto& other:views)if(other.horizontal && &other!=&view){
                const float middle=.5f*(view.height+other.height);
                if(other.height<view.height)view.continuation[0]=std::max(view.continuation[0],middle);
                else view.continuation[1]=std::min(view.continuation[1],middle);
            }
        }

    }
}

}
