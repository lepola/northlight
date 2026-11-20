#include "owned_world_render_data.h"
#include <map>
#include "scenery_water_heights.h"
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <wincrypt.h>

namespace water
{
void PrepareOwnedWorldHorizon(world::ArchiveReader& archives,world::WorldScene& scene,const OwnedWorldRenderData& data,
                             const std::atomic<bool>& cancelled,world::SceneResult& result)
{
    if(!scene.sourceRegion.Valid() || !data.HasHorizontalView())return;
    // Leave bookkeeping/scratch room inside the existing budgets. Mandatory
    // geometry and materials were admitted first; this never lowers their detail.
    constexpr std::size_t resident=world::SceneBytes,workspace=world::SceneWorkspaceBytes,scratch=64*1024;
    const auto used=scene.Bytes()+data.Bytes();
    scene.horizonError=world::HorizonError::Budget;
    if(used>resident || resident-used<scratch || scene.sources.size()==scene.sources.capacity())return;
    try {
        const std::string map=world::OwnedWorldRegion::Directory(scene.sourceRegion.map);
        world::ArchiveEntry entry;
        const auto read=archives.ReadSnapshot("world\\maps\\"+map+"\\"+map+".wdl",cancelled,entry,
                                             std::min(world::HorizonInputLimit,workspace-used-scratch));
        if(read!=world::ArchiveError::None){
            scene.horizonError=read==world::ArchiveError::Missing?world::HorizonError::Missing:
                read==world::ArchiveError::Cancelled?world::HorizonError::Cancelled:world::HorizonError::Archive;
            return;
        }
        result.sourceBytes+=entry.bytes.size();
        const auto limit=std::min(resident-used-scratch,workspace-used-scratch-entry.bytes.capacity());
        world::StaticModel mesh;unsigned tiles=0;
        scene.horizonError=world::DecodeHorizon({entry.bytes.data(),entry.bytes.size()},scene.sourceRegion,cancelled,limit,mesh,tiles);
        result.peakWorkspaceBytes=std::max(result.peakWorkspaceBytes,used+scratch+entry.bytes.capacity()+mesh.Bytes());
        if(scene.horizonError!=world::HorizonError::None)return;
        scene.sources.push_back({std::move(entry.name),std::move(entry.source)});
        scene.region.Edit().horizon=std::move(mesh);scene.horizonTiles=tiles;
    }catch(const std::bad_alloc&){scene.horizonError=world::HorizonError::Budget;}
}
bool PrepareOwnedTextureKeys(world::WorldScene& scene,const std::atomic<bool>& cancelled,bool extension)
{
    if(scene.textures.size()>world::MaxSceneTextures)return false;
    if (!extension) for (auto& source : scene.textures) source.contentKey = {};
    struct Provider {
        HCRYPTPROV value=0;
        ~Provider(){if(value)CryptReleaseContext(value,0);}
    } provider;
    if(!CryptAcquireContextW(&provider.value,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return !cancelled.load();
    for(unsigned i=0;i<scene.textures.size();++i){
        if(cancelled.load())return false;
        if (extension && scene.textures[i].contentKey != world::SceneTextureKey{}) continue;
        struct Hash {
            HCRYPTHASH value=0;
            ~Hash(){if(value)CryptDestroyHash(value);}
            bool Bytes(const void* p,std::size_t size){return size<=MAXDWORD && CryptHashData(value,static_cast<const BYTE*>(p),DWORD(size),0);}
            bool Number(uint32_t n){return Bytes(&n,sizeof(n));}
        } hash;
        if(!CryptCreateHash(provider.value,CALG_SHA_256,0,0,&hash.value))continue;
        const auto& texture=scene.textures[i].pixels.Read();
        bool ok=hash.Number(uint32_t(texture.format)) && hash.Number(uint32_t(texture.mips.size()));
        for(const auto& mip:texture.mips){
            if(cancelled.load())return false;
            ok=ok && hash.Number(mip.width) && hash.Number(mip.height) &&
                hash.Number(uint32_t(mip.bytes.size())) && hash.Bytes(mip.bytes.data(),mip.bytes.size());
        }
        world::SceneTextureKey key{};DWORD size=DWORD(key.size());
        if(ok && CryptGetHashParam(hash.value,HP_HASHVAL,key.data(),&size,0) && size==key.size())scene.textures[i].contentKey=key;
    }
    return !cancelled.load();
}
std::size_t OwnedWorldRenderData::Bytes() const
{
    std::size_t bytes=sizeof(*this)+terrain.capacity()*sizeof(OwnedTerrainRenderData)+instanceHeights.capacity()*sizeof(float)+receivers.Bytes();
    for (const auto& tile : terrain)
        bytes+=tile.waterHeights.capacity()*sizeof(float)+tile.vertexOffsets.capacity()*sizeof(uint32_t)+tile.alphaAtlas.capacity()+
               tile.alphaRects.capacity()*sizeof(std::array<float,4>)+(tile.reflectedBounds.capacity()+tile.sourceBounds.capacity())*sizeof(std::array<float,6>);
    bytes+=views.capacity()*sizeof(OwnedReceiverView)+receiverVertices.capacity()*sizeof(OwnedReceiverVertex);
    for(const auto& view:views)bytes+=view.Bytes();
    bytes+=instanceGroups.capacity()*sizeof(OwnedInstanceGroup);
    for(const auto& group:instanceGroups)bytes+=group.instances.capacity()*sizeof(unsigned);
    return bytes;
}
OwnedWorldDataResult PrepareOwnedWorldRenderData(world::WorldScene& scene,const std::atomic<bool>& cancelled,
                                OwnedWorldRenderData& output,std::size_t residentLimit,std::size_t workspaceLimit,const float* receiver)
{
    // Source-scene bounds also bound the reused field's maps/index. Reserve a
    // conservative work area; allocator overhead and process RSS need separate
    // measurement. The field exists only on this worker during preparation.
    constexpr std::size_t fieldReservation=16*1024*1024;
    if (cancelled.load()) return OwnedWorldDataResult::Cancelled;
    if (scene.region->water.empty() || scene.region->water.size()>8192 || scene.region->terrain.size()>world::OwnedWorldRegion::MaxTiles ||
        scene.region->instances.size()>world::MaxSceneInstances) return OwnedWorldDataResult::Invalid;
    OwnedWorldRenderData candidate;
    const auto room=[&](std::size_t extra) {
        const auto assets=scene.Bytes(),derived=candidate.Bytes();
        const bool fits=assets<=workspaceLimit && derived<=workspaceLimit-assets && extra<=workspaceLimit-assets-derived &&
                        fieldReservation<=workspaceLimit-assets-derived-extra;
        if (fits) candidate.workspaceReservationBytes=std::max(candidate.workspaceReservationBytes,assets+derived+extra+fieldReservation);
        return fits;
    };
    if (!room(scene.region->terrain.size()*sizeof(OwnedTerrainRenderData)+scene.region->instances.size()*sizeof(float))) return OwnedWorldDataResult::Budget;
    reflection::WaterHeightField field;
    const float origin[]={-9410.65f,-121.241f,76.3078f}; // retention is irrelevant to this first complete observation
    std::vector<reflection::WaterSurface> receivingWater;
    if(receiver){
        if(!candidate.receivers.Build(scene.region->water,cancelled))return cancelled.load()?OwnedWorldDataResult::Cancelled:OwnedWorldDataResult::Invalid;
        candidate.receiverBody=candidate.receivers.Select(scene.region->water,receiver);
        if(candidate.receiverBody==OwnedWaterReceivers::Missing)return OwnedWorldDataResult::Invalid;
        candidate.horizontalReceiver=candidate.receivers.HorizontalPlane(scene.region->water,candidate.receiverBody,candidate.receiverHeight);
        receivingWater.reserve(scene.region->water.size());
        for(unsigned i=0;i<scene.region->water.size();++i)if(candidate.receivers.Body(i)==candidate.receiverBody)receivingWater.push_back(scene.region->water[i]);
    }
    field.Update(receiver?receivingWater:scene.region->water,0,receiver?receiver:origin,true);
    candidate.terrain.resize(scene.region->terrain.size());candidate.instanceHeights.resize(scene.region->instances.size());
    for (unsigned index=0;index<scene.region->terrain.size();++index) {
        if (cancelled.load()) return OwnedWorldDataResult::Cancelled;
        auto& tile=scene.region.Edit().terrain[index].tile;auto& render=candidate.terrain[index];
        if(tile.cells.empty()||tile.cells.size()>256)return OwnedWorldDataResult::Invalid;
        unsigned vertices=0,area=0;
        for(const auto& cell:tile.cells){
            if(((cell.flags&1)&&cell.shadow.size()!=512)||cell.vertices.empty()||cell.vertices.size()>145||(cell.alphaSize!=4&&cell.alphaSize!=16&&cell.alphaSize!=64))return OwnedWorldDataResult::Invalid;
            vertices+=static_cast<unsigned>(cell.vertices.size());area+=cell.alphaSize*cell.alphaSize;
        }
        // Descending power-of-two blocks pack without overlap. Cell order stays
        // unchanged for source identities and draw ranges; only UV rectangles move.
        unsigned atlasWidth=64;while(atlasWidth*atlasWidth<area)atlasWidth*=2;
        std::array<std::array<unsigned,2>,256> positions{};unsigned row=0;
        for(unsigned size:{64u,16u,4u}){unsigned column=0;bool any=false;
            for(unsigned i=0;i<tile.cells.size();++i)if(tile.cells[i].alphaSize==size){
                positions[i]={column,row};column+=size;any=true;
                if(column==atlasWidth){column=0;row+=size;any=false;}
            }
            if(any)row+=size;
        }
        unsigned atlasHeight=4;while(atlasHeight<row)atlasHeight*=2;
        const auto atlasBytes=std::size_t(atlasWidth)*atlasHeight*4;
        if(!room(atlasBytes+vertices*sizeof(float)+(tile.cells.size()+1)*sizeof(uint32_t)+
                 tile.cells.size()*(2*sizeof(std::array<float,6>)+sizeof(std::array<float,4>))))return OwnedWorldDataResult::Budget;
        render.alphaWidth=atlasWidth;render.alphaHeight=atlasHeight;
        render.waterHeights.resize(vertices);render.vertexOffsets.resize(tile.cells.size()+1);
        render.sourceBounds.resize(tile.cells.size());render.reflectedBounds.resize(tile.cells.size());render.alphaRects.resize(tile.cells.size());render.alphaAtlas.resize(atlasBytes);
        unsigned offset=0;
        for (unsigned i=0;i<tile.cells.size();++i) {
            if (cancelled.load()) return OwnedWorldDataResult::Cancelled;
            auto& cell=tile.cells[i];
            if (cell.x>=16 || cell.y>=16 || cell.layers.empty() || cell.layers.size()>4) return OwnedWorldDataResult::Invalid;
            const auto size=cell.alphaSize;
            for(unsigned layer=1;layer<cell.layers.size();++layer)if(cell.layers[layer].alpha.size()!=size*size)return OwnedWorldDataResult::Invalid;
            render.vertexOffsets[i]=offset;
            render.alphaRects[i]={(positions[i][0]+.5f)/atlasWidth,(positions[i][1]+.5f)/atlasHeight,(size-1.f)/atlasWidth,(size-1.f)/atlasHeight};
            auto& bounds=render.reflectedBounds[i];auto& sourceBounds=render.sourceBounds[i];
            for (unsigned v=0;v<cell.vertices.size();++v) {
                const auto& p=cell.vertices[v].position;auto& height=render.waterHeights[offset+v];
                if (!field.HeightAtIndexed(p[0],p[1],height) || !std::isfinite(height)) return OwnedWorldDataResult::Invalid;
                const float reflected[]={p[0],p[1],2*height-p[2]};
                for (unsigned axis=0;axis<3;++axis) {
                    if (!std::isfinite(reflected[axis])) return OwnedWorldDataResult::Invalid;
                    sourceBounds[axis]=v?std::min(sourceBounds[axis],p[axis]):p[axis];
                    sourceBounds[axis+3]=v?std::max(sourceBounds[axis+3],p[axis]):p[axis];
                    if (!v) bounds[axis]=bounds[axis+3]=reflected[axis];
                    else { bounds[axis]=std::min(bounds[axis],reflected[axis]);bounds[axis+3]=std::max(bounds[axis+3],reflected[axis]); }
                }
            }
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
                const unsigned sx=size==64&&cell.FixAlphaEdges()&&x==63?62:x;
                const unsigned sy=size==64&&cell.FixAlphaEdges()&&y==63?62:y;
                const auto pixel=(positions[i][1]+y)*atlasWidth+positions[i][0]+x;
                auto* dest=render.alphaAtlas.data()+pixel*4;
                for(unsigned layer=1;layer<cell.layers.size();++layer)dest[3-layer]=cell.layers[layer].alpha[sy*size+sx];
                const unsigned block=64/size;unsigned shadow=0;
                if(cell.flags&1)for(unsigned j=0;j<block;++j)for(unsigned k=0;k<block;++k){const auto bit=(y*block+j)*64+x*block+k;
                    shadow+=(cell.shadow[bit/8]>>(bit%8))&1u;}
                dest[3]=static_cast<uint8_t>((shadow*255+block*block/2)/(block*block));
            }
            offset+=static_cast<unsigned>(cell.vertices.size());
            for (auto& layer : cell.layers) std::vector<uint8_t>().swap(layer.alpha);
            std::vector<uint8_t>().swap(cell.shadow);
        }
        render.vertexOffsets.back()=offset;
    }
    for (unsigned i=0;i<scene.region->instances.size();++i) {
        if (cancelled.load()) return OwnedWorldDataResult::Cancelled;
        const auto& instance=scene.region->instances[i];
        // All groups of a WMO share one rigid receiver. Per-group centres can
        // otherwise split a building across the river's receiving heights.
        const float x=instance.group==0xffff?(instance.minimum[0]+instance.maximum[0])*.5f:instance.transform[12];
        const float y=instance.group==0xffff?(instance.minimum[1]+instance.maximum[1])*.5f:instance.transform[13];
        if (!field.HeightAtIndexed(x,y,candidate.instanceHeights[i]) ||
            !std::isfinite(candidate.instanceHeights[i])) return OwnedWorldDataResult::Invalid;
    }
    if(receiver){
        // Spatial membership is immutable scenery data. Heights only transform
        // the groups' bounds; no rebucketing or per-view instance copies.
        if(!room(scene.region->instances.size()*(sizeof(OwnedInstanceGroup)+80)))return OwnedWorldDataResult::Budget;
        std::map<std::pair<int,int>,unsigned> buckets;
        candidate.instanceGroups.reserve(scene.region->instances.size());
        for(unsigned i=0;i<scene.region->instances.size();++i){
            if(cancelled.load())return OwnedWorldDataResult::Cancelled;
            const auto& instance=scene.region->instances[i];
            const auto key=std::make_pair(int(std::floor((instance.minimum[0]+instance.maximum[0])/128.f)),
                                          int(std::floor((instance.minimum[1]+instance.maximum[1])/128.f)));
            auto added=buckets.emplace(key,unsigned(candidate.instanceGroups.size()));
            if(added.second)candidate.instanceGroups.push_back({});
            auto& group=candidate.instanceGroups[added.first->second];const bool first=group.instances.empty();
            for(unsigned c=0;c<3;++c){group.bounds[c]=first?instance.minimum[c]:std::min(group.bounds[c],instance.minimum[c]);
                group.bounds[c+3]=first?instance.maximum[c]:std::max(group.bounds[c+3],instance.maximum[c]);}
            group.instances.push_back(i);
        }
        for(unsigned t=0;t<candidate.terrain.size();++t){
            auto& tile=candidate.terrain[t];
            for(unsigned i=0;i<tile.sourceBounds.size();++i){
                const auto& cell=scene.region->terrain[t].tile.cells[i];
                if(cell.x>=16 || cell.y>=16)return OwnedWorldDataResult::Invalid;
                const auto g=OwnedTerrainRenderData::Group(cell.x,cell.y);
                if(g>=OwnedTerrainRenderData::GroupCount)return OwnedWorldDataResult::Invalid;
                const bool first=!(tile.groupMask&(1u<<g));
                for(unsigned c=0;c<3;++c){
                    tile.sourceExtent[c]=i?std::min(tile.sourceExtent[c],tile.sourceBounds[i][c]):tile.sourceBounds[i][c];
                    tile.sourceExtent[c+3]=i?std::max(tile.sourceExtent[c+3],tile.sourceBounds[i][c+3]):tile.sourceBounds[i][c+3];
                    tile.sourceGroups[g][c]=first?tile.sourceBounds[i][c]:std::min(tile.sourceGroups[g][c],tile.sourceBounds[i][c]);
                    tile.sourceGroups[g][c+3]=first?tile.sourceBounds[i][c+3]:std::max(tile.sourceGroups[g][c+3],tile.sourceBounds[i][c+3]);
                }
                tile.groupMask|=uint16_t(1u<<g);
            }
        }
    }
    field.Reset(); // only one temporary height field is included in the worker reservation
    if(receiver){
        if(!room(scene.region->water.size()*(sizeof(OwnedReceiverView)+2*sizeof(OwnedReceiverView::SurfaceGroup)+sizeof(unsigned)+sizeof(float)+sizeof(int8_t)+6*sizeof(OwnedReceiverVertex))))
            return OwnedWorldDataResult::Budget;
        if(!GroupReceiverViews(scene.region->water,candidate.receivers,cancelled,candidate.views,candidate.receiverVertices))
            return cancelled.load()?OwnedWorldDataResult::Cancelled:OwnedWorldDataResult::Invalid;
        if(scene.sourceRegion.Valid())SetReceiverContinuations(candidate.views,
            {scene.sourceRegion.Centre(0),scene.sourceRegion.Centre(1)},scene.waterCompleteReach);
        std::size_t vertices=0,cells=0;
        for(const auto& tile:candidate.terrain){vertices+=tile.waterHeights.size();cells+=tile.reflectedBounds.size();}
        for(auto& view:candidate.views){
            if(view.horizontal)continue;
            const auto extra=(vertices+scene.region->instances.size())*sizeof(float)+(cells+scene.region->terrain.size()*(1+OwnedTerrainRenderData::GroupCount)+candidate.instanceGroups.size())*sizeof(std::array<float,6>);
            if(!room(extra))return OwnedWorldDataResult::Budget;
            receivingWater.clear();
            for(unsigned i=0;i<scene.region->water.size();++i)if(candidate.receivers.Body(i)==view.body)receivingWater.push_back(scene.region->water[i]);
            reflection::WaterHeightField receivingField;
            receivingField.Update(receivingWater,0,receiver,true);
            view.terrainHeights.reserve(vertices);view.reflectedBounds.reserve(cells);
            for(const auto& tile:scene.region->terrain)for(const auto& cell:tile.tile.cells){
                if(cancelled.load())return OwnedWorldDataResult::Cancelled;
                std::array<float,6> bounds{};bool first=true;
                for(const auto& vertex:cell.vertices){
                    const auto& p=vertex.position;float height=0;
                    if(!receivingField.HeightAtIndexed(p[0],p[1],height)||!std::isfinite(height))return OwnedWorldDataResult::Invalid;
                    view.terrainHeights.push_back(height);
                    const float reflected[]={p[0],p[1],2*height-p[2]};
                    for(unsigned axis=0;axis<3;++axis){
                        bounds[axis]=first?reflected[axis]:std::min(bounds[axis],reflected[axis]);
                        bounds[axis+3]=first?reflected[axis]:std::max(bounds[axis+3],reflected[axis]);
                    }
                    first=false;
                }
                view.reflectedBounds.push_back(bounds);
            }
            view.instanceHeights.reserve(scene.region->instances.size());
            for(const auto& instance:scene.region->instances){
                const float x=instance.group==0xffff?(instance.minimum[0]+instance.maximum[0])*.5f:instance.transform[12];
                const float y=instance.group==0xffff?(instance.minimum[1]+instance.maximum[1])*.5f:instance.transform[13];
                float height=0;if(!receivingField.HeightAtIndexed(x,y,height)||!std::isfinite(height))return OwnedWorldDataResult::Invalid;
                view.instanceHeights.push_back(height);
            }
            view.terrainExtents.reserve(scene.region->terrain.size());
            view.terrainGroups.resize(scene.region->terrain.size()*OwnedTerrainRenderData::GroupCount);
            view.instanceGroupBounds.reserve(candidate.instanceGroups.size());
            unsigned cellOffset=0;
            for(unsigned t=0;t<candidate.terrain.size();++t){
                const auto& tile=candidate.terrain[t];
                std::array<float,6> box{};uint16_t groups=0;
                for(unsigned i=0;i<tile.reflectedBounds.size();++i){
                    const auto& cell=view.reflectedBounds[cellOffset++];
                    const auto& source=scene.region->terrain[t].tile.cells[i];
                    const auto g=OwnedTerrainRenderData::Group(source.x,source.y);
                    auto& group=view.terrainGroups[t*OwnedTerrainRenderData::GroupCount+g];
                    const bool first=!(groups&(1u<<g));
                    for(unsigned c=0;c<3;++c){
                        box[c]=i?std::min(box[c],cell[c]):cell[c];box[c+3]=i?std::max(box[c+3],cell[c+3]):cell[c+3];
                        group[c]=first?cell[c]:std::min(group[c],cell[c]);
                        group[c+3]=first?cell[c+3]:std::max(group[c+3],cell[c+3]);
                    }
                    groups|=uint16_t(1u<<g);
                }
                view.terrainExtents.push_back(box);
            }
            for(const auto& group:candidate.instanceGroups){
                auto box=group.bounds;bool first=true;
                for(unsigned i:group.instances){const auto& instance=scene.region->instances[i];const float height=view.instanceHeights[i];
                    const float low=2*height-instance.maximum[2],high=2*height-instance.minimum[2];
                    box[2]=first?low:std::min(box[2],low);box[5]=first?high:std::max(box[5],high);first=false;}
                view.instanceGroupBounds.push_back(box);
            }
        }
    }
    if (cancelled.load()) return OwnedWorldDataResult::Cancelled;
    if (!room(0) || scene.Bytes()>residentLimit || candidate.Bytes()>residentLimit-scene.Bytes()) return OwnedWorldDataResult::Budget;
    if(!PrepareOwnedTextureKeys(scene,cancelled))return cancelled.load()?OwnedWorldDataResult::Cancelled:OwnedWorldDataResult::Invalid;
    output=std::move(candidate);return OwnedWorldDataResult::Ready;
}
}
