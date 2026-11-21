#include "world_terrain_material.h"
#include "owned_world_gpu.h"
#include "owned_receiver_visibility.h"
#include "log.h"
#include "math/projection.h"
#include "math/clip_volume.h"
#include "scenery_water_planes.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>

namespace water
{
namespace
{
constexpr std::size_t stagingLimit=16*1024*1024;
// The instance stream holds every placed copy plus room for live objects' parts; more wrap within a frame.
unsigned InstanceCapacity(const world::WorldScene& scene) {return unsigned(std::max<std::size_t>(256,scene.region->instances.size()+256));}
}
std::size_t OwnedWorldGpu::InstanceStreamBytes(const world::WorldScene& scene) {return InstanceCapacity(scene)*sizeof(OwnedWorldInstance);}
namespace
{
template<class T> void Drop(T*& p) {if (p) p->Release();p=nullptr;}
D3DFORMAT Format(world::TextureFormat f)
{
    switch (f) {
    case world::TextureFormat::Bgra8:return D3DFMT_A8R8G8B8;
    case world::TextureFormat::Bc1:return D3DFMT_DXT1;
    case world::TextureFormat::Bc2:return D3DFMT_DXT3;
    case world::TextureFormat::Bc3:return D3DFMT_DXT5;
    }
    return D3DFMT_UNKNOWN;
}
// The larger side, in viewport pixels, of the screen rectangle around a box; infinite when it reaches behind the camera.
double ScreenSize(const std::array<float,6>& box,const float* camera,const double (&clip)[4][4],const D3DVIEWPORT9& viewport)
{
    double low[2]={std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()},high[2]={-low[0],-low[1]};
    for (unsigned corner=0;corner<8;++corner) {
        double q[4]={};
        for (unsigned c=0;c<4;++c) {
            q[c]=clip[3][c];
            for (unsigned r=0;r<3;++r) q[c]+=(double(box[r+((corner&(1u<<r))?3:0)])-camera[r])*clip[r][c];
        }
        if (!(q[3]>1e-3)) return std::numeric_limits<double>::infinity();
        for (unsigned axis=0;axis<2;++axis) {low[axis]=std::min(low[axis],q[axis]/q[3]);high[axis]=std::max(high[axis],q[axis]/q[3]);}
    }
    return std::max((high[0]-low[0])*viewport.Width,(high[1]-low[1])*viewport.Height)*.5;
}
unsigned SizeBucket(double pixels)
{
    unsigned bucket=0;
    for (double limit=2;bucket+1<OwnedWorldDrawStats::kSizes && !(pixels<limit);limit*=2) ++bucket;
    return bucket;
}

}
template<class T> static T* RetainResource(T* resource)
{
    if (resource) resource->AddRef();
    return resource;
}
void OwnedWorldGpu::Mesh::ShareBuffers(const Mesh& source)
{
    vertices=RetainResource(source.vertices);
    indices=RetainResource(source.indices);
    sharedBuffers=true;
}
void OwnedWorldGpu::Release()
{
    assert(GetCurrentThreadId()==m_owner);
    for (auto* collection : {&m_terrain,&m_models}) for (auto& mesh : *collection) {Drop(mesh.vertices);Drop(mesh.indices);}
    for(auto*& buffer:m_receiverBuffers)Drop(buffer);
    m_receiverBuffers.clear();m_terrainVertexOffsets.clear();m_terrainCellOffsets.clear();
    Drop(m_instances);m_instanceCapacity=0;m_queued.clear();
    m_modelOffsets.clear();m_instanceOrder.clear();
    for (auto& t : m_textures) Drop(t);
    for (auto& t : m_alpha) Drop(t);
    Drop(m_staging);
    m_terrain.clear();m_models.clear();m_textures.clear();m_alpha.clear();m_textureFirstMip.clear();m_textureReused.clear();
    m_scene=nullptr;m_render=nullptr;m_device=nullptr;m_ready=m_failed=m_poseReady=false;
    m_phase=Phase::TerrainVertices;m_item=m_offset=m_level=m_row=0;
    m_stageWidth=m_stageHeight=m_stageLevels=0;m_stageFormat=D3DFMT_UNKNOWN;
    m_bytes=m_stagingBytes=m_expectedBytes=0;
    m_reusedTextureBytes=m_reusedGeometryBytes=0;m_reusedTextures=0;m_sharedRegion=false;
}
bool OwnedWorldGpu::Fail(const char* reason)
{
    WATER_LOG_INFO("owned GPU upload refused reason=%s planned=%zu limit=%zu",reason,m_planned,m_gpuLimit);
    return false;
}
bool OwnedWorldGpu::Initialize(const world::WorldScene& scene,const OwnedWorldRenderData& data,std::size_t resourceBudget,const OwnedWorldGpu* resident)
{
    m_sharedRegion=resident!=nullptr;
    if (scene.region->terrain.size()!=data.terrain.size() || scene.region->instances.size()!=data.instanceHeights.size() ||
        scene.region->terrain.size()>world::OwnedWorldRegion::MaxExtendedTiles || scene.models.size()>world::MaxSceneModels || scene.textures.size()>world::MaxSceneTextures || scene.region->instances.size()>world::MaxSceneInstances) return Fail("scene counts");
    const auto gpuLimit=std::min(resourceBudget,ResidencyLimit);
    m_gpuLimit=gpuLimit;auto& planned=m_planned;planned=0;
    const auto add=[&](std::size_t count,std::size_t size) {
        if (count>(gpuLimit-planned)/size) return false;planned+=count*size;return true;
    };
    m_terrain.resize(scene.region->terrain.size());m_models.resize(scene.models.size());
    m_textures.resize(scene.textures.size());m_alpha.resize(scene.region->terrain.size());
    m_textureFirstMip.assign(scene.textures.size(),0);
    m_textureReused.assign(scene.textures.size(),0);
    for (unsigned i=0;i<scene.region->terrain.size();++i) {
        const auto& tile=scene.region->terrain[i].tile;const auto& render=data.terrain[i];auto& mesh=m_terrain[i];
        if (resident) {
            const auto& old=resident->m_terrain[i];
            mesh.vertexCount=old.vertexCount;mesh.indexCount=old.indexCount;
            const auto bytes=std::size_t(mesh.vertexCount)*(sizeof(OwnedWorldVertex)+kTerrainAlphaStride)+
                std::size_t(mesh.indexCount)*2+render.alphaAtlas.size();
            if (!add(bytes,1)) return Fail("shared terrain over budget");
            mesh.layout=old.layout;
            mesh.ShareBuffers(old);
            m_alpha[i]=RetainResource(resident->m_alpha[i]);
            m_reusedGeometryBytes+=bytes;
            continue;
        }
        if(tile.cells.empty()||tile.cells.size()>256||render.vertexOffsets.size()!=tile.cells.size()+1||render.vertexOffsets[0]!=0||
           render.waterHeights.size()!=render.vertexOffsets.back()||render.reflectedBounds.size()!=tile.cells.size()||render.alphaRects.size()!=tile.cells.size()||
           !render.alphaWidth||!render.alphaHeight||render.alphaWidth>1024||render.alphaHeight>2048||
           render.alphaAtlas.size()!=std::size_t(render.alphaWidth)*render.alphaHeight*4)return Fail("terrain data");
        auto layout=std::make_shared<TerrainLayout>();
        layout->cells.resize(tile.cells.size());
        for (unsigned c=0;c<tile.cells.size();++c) {
            const auto& cell=tile.cells[c];
            if (cell.x>=16 || cell.y>=16 || cell.indices.size()%3 || cell.indices.size()>768 || cell.layers.empty() || cell.layers.size()>4) return Fail("terrain cell");
            if(cell.vertices.empty()||cell.vertices.size()>145||render.vertexOffsets[c]!=mesh.vertexCount||render.vertexOffsets[c+1]!=mesh.vertexCount+cell.vertices.size())return Fail("terrain cell");
            layout->cells[c]={0,static_cast<unsigned>(cell.indices.size()),mesh.vertexCount,static_cast<unsigned>(cell.vertices.size())};
            mesh.indexCount+=static_cast<unsigned>(cell.indices.size());mesh.vertexCount+=static_cast<unsigned>(cell.vertices.size());
            for (auto index : cell.indices) if (index>=cell.vertices.size()) return Fail("terrain index");
            if(!cell.indices.empty())for (const auto& layer : cell.layers)
                if (layer.texture>=scene.region->terrain[i].textures.size() || scene.region->terrain[i].textures[layer.texture]>=scene.textures.size()) return Fail("terrain layer");
        }
        // Cells with the same four layers (texture and flags, as drawn) take neighbouring index ranges, so visible
        // runs of them draw at once; indices address the whole tile, which 256 cells of 145 vertices fit.
        if (mesh.vertexCount>65536) return Fail("terrain vertices");
        std::vector<std::array<uint32_t,8>> keys(tile.cells.size());
        for (unsigned c=0;c<tile.cells.size();++c) {
            const auto& cell=tile.cells[c];
            if (cell.indices.empty()) {keys[c].fill(~0u);continue;}
            for (unsigned layer=0;layer<4;++layer) {
                const auto& source=cell.layers[layer<cell.layers.size()?layer:0];
                keys[c][layer*2]=scene.region->terrain[i].textures[source.texture];keys[c][layer*2+1]=source.flags;
            }
        }
        layout->order.resize(tile.cells.size());layout->sets.assign(tile.cells.size(),0);
        for (unsigned c=0;c<tile.cells.size();++c) layout->order[c]=c;
        std::stable_sort(layout->order.begin(),layout->order.end(),[&](unsigned a,unsigned b){return keys[a]<keys[b];});
        unsigned first=0,set=0;
        for (unsigned k=0;k<layout->order.size();++k) {
            const auto c=layout->order[k];
            if (k && keys[c]!=keys[layout->order[k-1]]) ++set;
            layout->sets[c]=set;layout->cells[c].first=first;first+=layout->cells[c].count;
        }
        mesh.layout=std::move(layout);
        if (!add(mesh.vertexCount,sizeof(OwnedWorldVertex)+kTerrainAlphaStride) || !add(mesh.indexCount,2) || !add(render.alphaAtlas.size(),1)) return Fail("terrain over budget");
    }
    for (unsigned i=0;i<scene.models.size();++i) {
        const auto& model=scene.models[i];auto& mesh=m_models[i];
        // Animated vertices are rewritten on the device owner for each pose.
        if (resident && i<resident->m_scene->models.size() &&
            model.geometry.SharesStorage(resident->m_scene->models[i].geometry) &&
            !model.geometry->motion.duration && model.textures==resident->m_scene->models[i].textures) {
            const auto& old=resident->m_models[i];
            mesh.vertexCount=old.vertexCount;mesh.indexCount=old.indexCount;
            const auto bytes=std::size_t(mesh.vertexCount)*sizeof(OwnedWorldVertex)+std::size_t(mesh.indexCount)*2;
            if (!add(bytes,1)) return Fail("shared model over budget");
            mesh.ShareBuffers(old);m_reusedGeometryBytes+=bytes;
            continue;
        }
        if (model.geometry->mesh.vertices.empty() || model.geometry->mesh.vertices.size()>65536 || model.geometry->mesh.indices.empty() || model.geometry->mesh.indices.size()%3) return Fail("model mesh");
        mesh.vertexCount=static_cast<unsigned>(model.geometry->mesh.vertices.size());mesh.indexCount=static_cast<unsigned>(model.geometry->mesh.indices.size());
        for (auto index : model.geometry->mesh.indices) if (index>=mesh.vertexCount) return Fail("model index");
        for (const auto& draw : model.geometry->mesh.draws)
            if (!draw.indexCount || draw.indexCount%3 || draw.firstIndex>mesh.indexCount || draw.indexCount>mesh.indexCount-draw.firstIndex ||
                draw.texture>=model.textures.size() || draw.texture>=model.geometry->mesh.textures.size() || model.textures[draw.texture]>=scene.textures.size() ||
                draw.blend>1 || (draw.materialFlags&~7u)) return Fail("model draw");
        if(model.geometry->motion.duration && (model.geometry->motion.vertices.size()!=mesh.vertexCount || model.geometry->motion.bones.empty() || model.geometry->motion.bones.size()>256))return Fail("model motion");
        if (!add(mesh.vertexCount,sizeof(OwnedWorldVertex)) || !add(mesh.indexCount,2)) return Fail("models over budget");
    }
    // The instance stream is created at the first instanced draw; its bytes are reserved now.
    if (!add(InstanceCapacity(scene),sizeof(OwnedWorldInstance))) return Fail("instances over budget");
    m_bytes+=InstanceStreamBytes(scene);
    // Receiver streams are mandatory geometry too. Reserve them before choosing
    // texture mip residency, so a crowded scene cannot consume their allowance.
    m_receiverBuffers.resize(data.views.empty()?0:data.views.size()+1);
    if(!add(data.receiverVertices.size(),sizeof(OwnedReceiverVertex)))return Fail("receiver mask budget");
    unsigned vertexOffset=0,cellOffset=0;
    for(const auto& tile:data.terrain){m_terrainVertexOffsets.push_back(vertexOffset);m_terrainCellOffsets.push_back(cellOffset);
        vertexOffset+=unsigned(tile.waterHeights.size());cellOffset+=unsigned(tile.reflectedBounds.size());}
    for(const auto& view:data.views){
        if(!view.horizontal && (view.terrainHeights.size()!=vertexOffset || view.reflectedBounds.size()!=cellOffset ||
           view.instanceHeights.size()!=scene.region->instances.size() || view.terrainExtents.size()!=scene.region->terrain.size() ||
           view.terrainGroups.size()!=scene.region->terrain.size()*OwnedTerrainRenderData::GroupCount ||
           view.instanceGroupBounds.size()!=data.instanceGroups.size()))return Fail("receiver field layout");
        if(!add(view.terrainHeights.size(),sizeof(float)))return Fail("receiver field budget");
    }
    if (resident) {
        for (unsigned i=0;i<m_receiverBuffers.size();++i) {
            m_receiverBuffers[i]=RetainResource(resident->m_receiverBuffers[i]);
            m_reusedGeometryBytes+=i ? data.views[i-1].terrainHeights.size()*sizeof(float) :
                data.receiverVertices.size()*sizeof(OwnedReceiverVertex);
        }
    }
    for (const auto& source : scene.textures) {
        if (source.pixels->mips.empty() || source.pixels->mips.size()>16 || Format(source.pixels->format)==D3DFMT_UNKNOWN) return Fail("texture format");
        std::size_t bytes=0;
        for (const auto& mip : source.pixels->mips) {
            world::MipLayout layout;
            if (!world::GetMipLayout(source.pixels->format,mip.width,mip.height,layout)) return Fail("texture size");
            const auto required=layout.Bytes();
            if (mip.bytes.size()!=required || required>stagingLimit-bytes ||
                required>std::numeric_limits<std::size_t>::max()-planned) return Fail("texture staging");
            bytes+=required;planned+=required;
        }
    }
    // The final render targets share this budget. Geometry is kept; textures use
    // lower authored mips instead, distant-only materials first, then near/shared
    // ones (a crowded harbour plus a ship), so the scene fits rather than fails.
    // Prefer one reduction across large textures before reducing any twice.
    for(bool shared=false;planned>gpuLimit;){
        unsigned selected=static_cast<unsigned>(scene.textures.size());
        std::size_t saved=0;
        for(unsigned i=0;i<scene.textures.size();++i){
            const auto& source=scene.textures[i];const auto first=m_textureFirstMip[i];
            if((!shared && !source.maxDimension) || first+1>=source.pixels->mips.size())continue;
            // A compressed texture's top level must stay a whole 4x4 block; the device refuses smaller ones.
            const auto& next=source.pixels->mips[first+1];
            if(source.pixels->format!=world::TextureFormat::Bgra8 && (next.width<4 || next.height<4))continue;
            const auto bytes=source.pixels->mips[first].bytes.size();
            if(selected==scene.textures.size() || first<m_textureFirstMip[selected] ||
               (first==m_textureFirstMip[selected] && bytes>saved)){selected=i;saved=bytes;}
        }
        if(selected==scene.textures.size()){if(shared)return Fail("textures over budget at their last mips");shared=true;continue;}
        ++m_textureFirstMip[selected];planned-=saved;
    }
    for (const auto& instance : scene.region->instances) if (instance.model>=scene.models.size()) return Fail("instance model");
    // Optional horizon uses the same bounded buffers/upload loop, with no
    // textures. Admit it last so pressure cannot evict usable local scenery.
    const auto& horizon=scene.region->horizon;
    const auto horizonBytes=horizon.vertices.size()*sizeof(OwnedWorldVertex)+horizon.indices.size()*2;
    if(data.HasHorizontalView() && !horizon.indices.empty() && horizonBytes<=gpuLimit-planned){
        if(horizon.vertices.empty()||horizon.vertices.size()>65536||horizon.indices.size()%3)return Fail("horizon mesh");
        for(auto index:horizon.indices)if(index>=horizon.vertices.size())return Fail("horizon index");
        Mesh mesh;mesh.vertexCount=unsigned(horizon.vertices.size());mesh.indexCount=unsigned(horizon.indices.size());
        if (resident && resident->m_models.size()>resident->m_scene->models.size()) {
            const auto& old=resident->m_models.back();
            // Reserve before acquiring COM references: push_back must not strand them on allocation failure.
            m_models.reserve(m_models.size()+1);
            mesh.ShareBuffers(old);m_reusedGeometryBytes+=horizonBytes;
        }
        m_models.push_back(std::move(mesh));planned+=horizonBytes;
    }
    m_bytes+=m_reusedGeometryBytes;
    m_expectedBytes=planned;m_scene=&scene;m_render=&data;return true;
}
void OwnedWorldGpu::ReuseTextures(const OwnedWorldGpu& source)
{
    // Both slots retain independent COM ownership. Retirement/cancellation of
    // either one cannot invalidate the other; Reset releases both normally.
    // Count shared bytes in both slots conservatively under the existing caps.
    if(&source==this || source.m_owner!=m_owner || !source.m_ready || source.m_device!=m_device)return;
    std::map<world::SceneTextureKey,unsigned> available;
    const world::SceneTextureKey missing{};
    try {
        for(unsigned i=0;i<source.m_textures.size();++i)
            if(source.m_textures[i] && source.m_scene->textures[i].contentKey!=missing)
                available.emplace(source.m_scene->textures[i].contentKey,i);
    } catch(const std::bad_alloc&) { available.clear(); }
    for(unsigned i=0;i<m_textures.size();++i){
        const auto& texture=m_scene->textures[i];
        unsigned other=i;
        if (!m_sharedRegion || i>=source.m_textures.size() || !source.m_textures[i] ||
            !texture.pixels.SharesStorage(source.m_scene->textures[i].pixels) ||
            m_textureFirstMip[i]!=source.m_textureFirstMip[i]) {
            const auto found=available.find(texture.contentKey);
            if(found==available.end())continue;
            other=found->second;
            if(m_textureFirstMip[i]!=source.m_textureFirstMip[other])continue;
        }
        m_textures[i]=RetainResource(source.m_textures[other]);m_textureReused[i]=1;
        for(unsigned level=m_textureFirstMip[i];level<m_scene->textures[i].pixels->mips.size();++level)
            m_reusedTextureBytes+=m_scene->textures[i].pixels->mips[level].bytes.size();
        ++m_reusedTextures;
    }
    m_bytes+=m_reusedTextureBytes;
}
bool OwnedWorldGpu::Buffer(IDirect3DDevice9* device,const world::WorldScene& scene,const OwnedWorldRenderData& data,unsigned& budget)
{
    const bool terrain=m_phase==Phase::TerrainVertices || m_phase==Phase::TerrainIndices;
    const bool vertices=m_phase==Phase::TerrainVertices || m_phase==Phase::ModelVertices;
    auto& meshes=terrain ? m_terrain : m_models;
    if (m_item==meshes.size()) {m_phase=static_cast<Phase>(static_cast<unsigned>(m_phase)+1);m_item=m_offset=0;return true;}
    auto& mesh=meshes[m_item];const unsigned count=vertices ? mesh.vertexCount : mesh.indexCount,stride=vertices ? sizeof(OwnedWorldVertex) : 2;
    const unsigned written=stride+(terrain && vertices ? kTerrainAlphaStride : 0);
    if (!count || mesh.sharedBuffers) {++m_item;m_offset=0;return true;}
    const unsigned amount=std::min(count-m_offset,budget/written);
    if (!amount) return true;
    if (vertices && !mesh.vertices) {
        const DWORD usage=D3DUSAGE_WRITEONLY|(!terrain && m_item<scene.models.size() && scene.models[m_item].geometry->motion.duration?D3DUSAGE_DYNAMIC:0);
        if (FAILED(m_result=device->CreateVertexBuffer(count*written,usage,0,D3DPOOL_DEFAULT,&mesh.vertices,nullptr))) return false;
        m_bytes+=count*written;
    }
    if (!vertices && !mesh.indices) {
        if (FAILED(m_result=device->CreateIndexBuffer(count*stride,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&mesh.indices,nullptr))) return false;
        m_bytes+=count*stride;
    }
    void* target=nullptr;
    if (vertices) {
        if (FAILED(m_result=mesh.vertices->Lock(m_offset*stride,amount*stride,&target,0))) return false;
        auto* output=static_cast<OwnedWorldVertex*>(target);
        struct TerrainExtra {float alpha[2];uint32_t colour;};
        static_assert(sizeof(TerrainExtra)==kTerrainAlphaStride,"stream 1 of terrain");
        std::vector<TerrainExtra> alpha(terrain ? amount : 0);
        for (unsigned v=0;v<amount;++v) {
            const unsigned at=m_offset+v;
            if (terrain) {
                const auto& offsets=data.terrain[m_item].vertexOffsets;
                const auto cell=static_cast<unsigned>(std::upper_bound(offsets.begin(),offsets.end(),at)-offsets.begin()-1);
                const auto& source=scene.region->terrain[m_item].tile.cells[cell].vertices[at-offsets[cell]];
                std::memcpy(output[v].position,source.position.data(),12);std::memcpy(output[v].normal,source.normal.data(),12);
                std::memcpy(output[v].uv,source.uv.data(),8);output[v].water=data.terrain[m_item].waterHeights[at];
                const auto& rect=data.terrain[m_item].alphaRects[cell];
                const auto& colours=scene.region->terrain[m_item].tile.cells[cell].colours;
                const unsigned inCell=at-offsets[cell];
                // Half of full scale is unpainted: the shader doubles it.
                alpha[v]={{rect[0]+source.uv[0]*rect[2],rect[1]+source.uv[1]*rect[3]},inCell<colours.size()?colours[inCell]:0xff808080u};
            } else {
                const auto& source=(m_item<scene.models.size()?scene.models[m_item].geometry->mesh:scene.region->horizon).vertices[at];
                std::memcpy(output[v].position,source.position.data(),12);std::memcpy(output[v].normal,source.normal.data(),12);
                std::memcpy(output[v].uv,source.uv.data(),8);output[v].water=0;
            }
        }
        if (FAILED(mesh.vertices->Unlock())) return false;
        if (terrain) {
            if (FAILED(mesh.vertices->Lock(count*stride+m_offset*kTerrainAlphaStride,amount*kTerrainAlphaStride,&target,0))) return false;
            std::memcpy(target,alpha.data(),amount*kTerrainAlphaStride);
            if (FAILED(mesh.vertices->Unlock())) return false;
        }
    } else {
        if (FAILED(m_result=mesh.indices->Lock(m_offset*stride,amount*stride,&target,0))) return false;
        auto* output=static_cast<uint16_t*>(target);
        if (!terrain) std::memcpy(output,(m_item<scene.models.size()?scene.models[m_item].geometry->mesh:scene.region->horizon).indices.data()+m_offset,amount*2);
        else {
            unsigned copied=0;
            for (unsigned k=0;k<mesh.layout->order.size() && copied<amount;++k) {
                const auto c=mesh.layout->order[k];const auto range=mesh.layout->cells[c];
                if (m_offset+copied>=range.first+range.count) continue;
                const auto local=m_offset+copied-range.first,n=std::min(amount-copied,range.count-local);
                const auto& indices=scene.region->terrain[m_item].tile.cells[c].indices;
                for (unsigned j=0;j<n;++j) output[copied+j]=static_cast<uint16_t>(indices[local+j]+range.vertex);
                copied+=n;
            }
        }
        if (FAILED(mesh.indices->Unlock())) return false;
    }
    budget-=amount*written;m_offset+=amount;
    if (m_offset==count) {++m_item;m_offset=0;}
    return true;
}
bool OwnedWorldGpu::Texture(IDirect3DDevice9* device,const world::WorldScene& scene,const OwnedWorldRenderData& data,unsigned& budget)
{
    const bool alpha=m_phase==Phase::Alpha;auto& resources=alpha ? m_alpha : m_textures;
    if (m_item==resources.size()) {m_phase=alpha ? Phase::Receivers : Phase::Alpha;m_item=m_level=m_row=0;return true;}
    if((alpha && m_sharedRegion) || (!alpha && m_textureReused[m_item])){++m_item;return true;}
    const auto* texture=alpha ? nullptr : &scene.textures[m_item].pixels.Read();
    const auto format=alpha ? D3DFMT_A8R8G8B8 : Format(texture->format);
    const unsigned first=alpha ? 0 : m_textureFirstMip[m_item];
    const unsigned levels=alpha ? 1 : static_cast<unsigned>(texture->mips.size())-first;
    const unsigned baseWidth=alpha ? data.terrain[m_item].alphaWidth : texture->mips[first].width,baseHeight=alpha ? data.terrain[m_item].alphaHeight : texture->mips[first].height;
    const unsigned width=alpha ? baseWidth : texture->mips[first+m_level].width,height=alpha ? baseHeight : texture->mips[first+m_level].height;
    const auto& pixels=alpha ? data.terrain[m_item].alphaAtlas : texture->mips[first+m_level].bytes;
    world::MipLayout layout;
    if (!world::GetMipLayout(alpha?world::TextureFormat::Bgra8:texture->format,width,height,layout)) return false;
    const unsigned rowBytes=layout.rowBytes,rows=layout.rows;
    const unsigned count=std::min(rows-m_row,budget/rowBytes);
    if (!count) return true;
    if (!resources[m_item]) {
        if (FAILED(m_result=device->CreateTexture(baseWidth,baseHeight,levels,0,format,D3DPOOL_DEFAULT,&resources[m_item],nullptr)) ||
            resources[m_item]->GetLevelCount()!=levels) return false;
        if (alpha) m_bytes+=data.terrain[m_item].alphaAtlas.size();
        else for (unsigned i=first;i<texture->mips.size();++i) m_bytes+=texture->mips[i].bytes.size();
    }
    if (m_stageWidth!=baseWidth || m_stageHeight!=baseHeight || m_stageLevels!=levels || m_stageFormat!=format) {
        Drop(m_staging);m_stagingBytes=0;m_stageWidth=m_stageHeight=m_stageLevels=0;
    }
    if (!m_staging) {
        if (FAILED(m_result=device->CreateTexture(baseWidth,baseHeight,levels,0,format,D3DPOOL_SYSTEMMEM,&m_staging,nullptr))) return false;
        m_stageWidth=baseWidth;m_stageHeight=baseHeight;m_stageLevels=levels;m_stageFormat=format;
        if (alpha) m_stagingBytes=data.terrain[m_item].alphaAtlas.size();
        else for (unsigned i=first;i<texture->mips.size();++i) m_stagingBytes+=texture->mips[i].bytes.size();
    }
    const unsigned unit=layout.blockHeight;
    const RECT rectangle={0,static_cast<LONG>(m_row*unit),static_cast<LONG>(width),static_cast<LONG>(std::min(height,(m_row+count)*unit))};
    D3DLOCKED_RECT lock{};
    if (FAILED(m_result=m_staging->LockRect(m_level,&lock,&rectangle,0))) return false;
    bool ok=lock.Pitch>=static_cast<INT>(rowBytes);
    if (ok) for (unsigned row=0;row<count;++row)
        std::memcpy(static_cast<uint8_t*>(lock.pBits)+std::size_t(row)*lock.Pitch,pixels.data()+std::size_t(m_row+row)*rowBytes,rowBytes);
    ok=SUCCEEDED(m_staging->UnlockRect(m_level)) && ok;
    IDirect3DSurface9 *source=nullptr,*target=nullptr;
    if (ok) ok=SUCCEEDED(m_staging->GetSurfaceLevel(m_level,&source)) && SUCCEEDED(resources[m_item]->GetSurfaceLevel(m_level,&target));
    const POINT point={0,rectangle.top};
    if (ok) ok=SUCCEEDED(device->UpdateSurface(source,&rectangle,target,&point));
    Drop(source);Drop(target);
    if (!ok) return false;
    budget-=count*rowBytes;m_row+=count;
    if (m_row==rows) {m_row=0;++m_level;if (m_level==levels) {m_level=0;++m_item;}}
    return true;
}
OwnedWorldUpload OwnedWorldGpu::Upload(IDirect3DDevice9* device,const world::WorldScene& scene,const OwnedWorldRenderData& data,unsigned byteBudget,std::size_t resourceBudget,const OwnedWorldGpu* reusable)
{
    if (GetCurrentThreadId()!=m_owner || !device || (m_device && m_device!=device) ||
        (m_scene && (m_scene!=&scene || m_render!=&data)) || m_failed) return OwnedWorldUpload::Failed;
    if (m_ready) return OwnedWorldUpload::Ready;
    if (!byteBudget) return OwnedWorldUpload::Pending;
    try {
        m_device=device;
        if (!m_scene) {
            const auto* resident=reusable && reusable!=this && reusable->m_owner==m_owner && reusable->m_ready &&
                reusable->m_device==device && reusable->m_render==&data &&
                scene.region.SharesStorage(reusable->m_scene->region) &&
                reusable->m_scene->textures.size()<=scene.textures.size() ? reusable : nullptr;
            if(!Initialize(scene,data,resourceBudget,resident)){Release();m_failed=true;return OwnedWorldUpload::Failed;}
            if(reusable)ReuseTextures(*reusable);
        }
        unsigned budget=std::min(byteBudget,4u*1024*1024);
        // The byte budget paces the upload; a step is one mip level or buffer slice,
        // so a step cap would hold small mips to a few per frame.
        for (unsigned step=0;step<4096 && m_phase!=Phase::Complete;++step) {
            const auto oldPhase=m_phase;const auto oldItem=m_item,oldOffset=m_offset,oldLevel=m_level,oldRow=m_row;
            const bool ok=m_phase==Phase::Receivers ? ReceiverBuffer(device,data,budget) :
                m_phase==Phase::Textures || m_phase==Phase::Alpha ? Texture(device,scene,data,budget) : Buffer(device,scene,data,budget);
            if (!ok) {
                unsigned width=0,height=0,first=0,format=0;
                if(m_phase==Phase::Textures && m_scene && m_item<m_scene->textures.size()){
                    const auto& t=m_scene->textures[m_item].pixels.Read();first=m_textureFirstMip[m_item];
                    if(first<t.mips.size()){width=t.mips[first].width;height=t.mips[first].height;}format=unsigned(t.format);
                }
                WATER_LOG_INFO("owned GPU upload device call failed phase=%u item=%u level=%u result=0x%08lx staging=%zu texture=%ux%u first_mip=%u format=%u",
                    unsigned(m_phase),m_item,m_level,static_cast<unsigned long>(m_result),m_stagingBytes,width,height,first,format);
                Fail("device call");Release();m_failed=true;return OwnedWorldUpload::Failed;
            }
            if (m_phase==oldPhase && m_item==oldItem && m_offset==oldOffset && m_level==oldLevel && m_row==oldRow) break;
        }
        if (m_phase==Phase::Complete) {
            if (m_bytes!=m_expectedBytes) {Fail("uploaded bytes differ from the plan");Release();m_failed=true;return OwnedWorldUpload::Failed;}
            Drop(m_staging);m_stagingBytes=0;m_stageWidth=m_stageHeight=m_stageLevels=0;
            m_ready=true;++m_uploads;return OwnedWorldUpload::Ready;
        }
    } catch (...) {Fail("allocation");Release();m_failed=true;return OwnedWorldUpload::Failed;}
    return OwnedWorldUpload::Pending;
}
// Receiver data shares the ordinary paced upload and residency budget.
bool OwnedWorldGpu::ReceiverBuffer(IDirect3DDevice9* device,const OwnedWorldRenderData& data,unsigned& budget)
{
    if(m_sharedRegion || m_item==m_receiverBuffers.size()){m_phase=Phase::Complete;m_item=m_offset=0;return true;}
    const void* source=m_item?static_cast<const void*>(data.views[m_item-1].terrainHeights.data()):data.receiverVertices.data();
    const unsigned bytes=m_item?unsigned(data.views[m_item-1].terrainHeights.size()*sizeof(float)):
        unsigned(data.receiverVertices.size()*sizeof(OwnedReceiverVertex));
    if(!bytes){++m_item;m_offset=0;return true;}
    auto*& buffer=m_receiverBuffers[m_item];
    if(!buffer){m_result=device->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr);
        if(FAILED(m_result))return false;m_bytes+=bytes;}
    const unsigned take=std::min(bytes-m_offset,budget);if(!take)return true;
    void* target=nullptr;m_result=buffer->Lock(m_offset,take,&target,0);if(FAILED(m_result))return false;
    std::memcpy(target,static_cast<const char*>(source)+m_offset,take);
    m_result=buffer->Unlock();if(FAILED(m_result))return false;
    m_offset+=take;budget-=take;if(m_offset==bytes){++m_item;m_offset=0;}return true;
}
bool OwnedWorldGpu::DrawReceivers(IDirect3DDevice9* device,const FrameInputs& frame)
{
    if(!m_ready || m_receiverBuffers.empty() || !m_receiverBuffers[0])return false;
    float projection[16],columns[16]={};
    if(!math::ClientToDeviceProjection(frame.glProjection,projection))return false;
    for(unsigned c=0;c<4;++c){
        for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k)columns[c*4+r]+=frame.cameraRelativeView[r*4+k]*projection[k*4+c];
        columns[c*4+3]=projection[12+c];
    }
    const float camera[]={frame.cameraPosition[0],frame.cameraPosition[1],frame.cameraPosition[2],0};
    return SUCCEEDED(device->SetVertexShaderConstantF(0,columns,4)) && SUCCEEDED(device->SetVertexShaderConstantF(4,camera,1)) &&
        SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) &&
        SUCCEEDED(device->SetStreamSource(0,m_receiverBuffers[0],0,sizeof(OwnedReceiverVertex))) &&
        SUCCEEDED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,unsigned(m_render->receiverVertices.size()/3)));
}
bool OwnedWorldGpu::Draw(IDirect3DDevice9* device,const FrameInputs& frame,const world::WorldScene& scene,const OwnedWorldRenderData& data,
                        const OwnedWorldShaders& shaders)
{
    if (GetCurrentThreadId()!=m_owner || device!=m_device || !Ready(scene) || m_render!=&data) return false;
    // Reuse the existing mesh/material path. Tiny decorative loops deform on
    // the GPU-owner thread into a dynamic VB, without extra shader constants.
    using Part=OwnedWorldDrawOptions::Part;
    const auto parts=m_options.parts;
    m_stats={};
    LARGE_INTEGER poseStart,poseEnd,frequency;QueryPerformanceCounter(&poseStart);
    world::ModelPose pose;
    for(unsigned i=0;(!m_poseReady || m_poseMilliseconds!=frame.modelMilliseconds) && i<scene.models.size() && (parts&Part::Animation);++i){
        const auto& model=scene.models[i];if(!model.geometry->motion.duration)continue;
        if(!world::SampleModelPose(model.geometry->motion,frame.modelMilliseconds,pose))return false;
        void* target=nullptr;
        if(FAILED(m_models[i].vertices->Lock(0,0,&target,D3DLOCK_DISCARD)))return false;
        auto* vertices=static_cast<OwnedWorldVertex*>(target);
        for(unsigned j=0;j<model.geometry->mesh.vertices.size();++j){
            const auto v=world::DeformModelVertex(model.geometry->mesh.vertices[j],model.geometry->motion.vertices[j],pose);
            std::memcpy(vertices[j].position,v.position.data(),12);std::memcpy(vertices[j].normal,v.normal.data(),12);
            std::memcpy(vertices[j].uv,v.uv.data(),8);vertices[j].water=0;
        }
        if(FAILED(m_models[i].vertices->Unlock()))return false;
    }
    m_poseReady=true;m_poseMilliseconds=frame.modelMilliseconds;
    QueryPerformanceCounter(&poseEnd);QueryPerformanceFrequency(&frequency);
    m_stats.poseMs=double(poseEnd.QuadPart-poseStart.QuadPart)*1000.0/double(frequency.QuadPart);
    const auto* v=frame.cameraRelativeView;
    const double determinant=double(v[0])*(double(v[5])*v[10]-double(v[6])*v[9])-double(v[1])*(double(v[4])*v[10]-double(v[6])*v[8])+double(v[2])*(double(v[4])*v[9]-double(v[5])*v[8]);
    if (!std::isfinite(determinant) || std::fabs(determinant)<1e-6) return false;
    const bool below=frame.CameraUnderSurface();
    const auto* receiver=m_options.receiver<data.views.size()?&data.views[m_options.receiver]:nullptr;
    const bool horizontal=receiver?receiver->horizontal:data.horizontalReceiver;
    const float receiverHeight=receiver?receiver->height:data.receiverHeight;
    // Fogged reflections are skipped only over a horizontal receiver whose water carries the game's fog (WaterGameFog).
    const bool fogSkip=m_options.fogSkip && !below && horizontal && frame.fogEnd>frame.fogStart;
    // Every visible copy of a model in one draw per batch where the device can instance; one draw per copy otherwise.
    const bool instancing=shaders.instancedVs && shaders.instancedDeclaration;
    const auto counted=[&](unsigned bucket,unsigned indexCount) {
        ++m_stats.batches;m_stats.triangles+=indexCount/3;
        ++m_stats.sizeBatches[bucket];m_stats.sizeTriangles[bucket]+=indexCount/3;
    };
    const auto submitted=[&]() {++m_draws;++m_stats.draws;};
    const auto cull=determinant<0 ? D3DCULL_CCW : D3DCULL_CW;
    double clip[4][4]={};float columns[16],projection[16];
    if (!math::ClientToDeviceProjection(frame.glProjection,projection)) return false;
    for (unsigned c=0;c<4;++c) {
        for (unsigned r=0;r<3;++r) for (unsigned k=0;k<3;++k) clip[r][c]+=double(frame.cameraRelativeView[r*4+k])*projection[k*4+c];
        clip[3][c]=projection[12+c];
    }
    // Share the validated client-to-device depth convention with the fixture.
    for (unsigned c=0;c<4;++c) for (unsigned r=0;r<4;++r) columns[c*4+r]=static_cast<float>(clip[r][c]);
    // The reflected scene fogs by its own axial view depth (owned_world_ps). A mirror image lying wholly past the fog's
    // end draws as solid fog colour, which the fogged horizon (or the sky behind it) already gives, so it is not drawn.
    // The test is exact: a linear depth over a box is least at a corner. Unfogged materials still show through fog.
    const bool pastFogSkip=!below && frame.fogEnd>frame.fogStart;
    const auto pastFog=[&](const std::array<float,6>& r) {
        for (unsigned corner=0;corner<8;++corner) {
            const double x=double(r[(corner&1)?3:0])-frame.cameraPosition[0],y=double(r[(corner&2)?4:1])-frame.cameraPosition[1],
                z=double(r[(corner&4)?5:2])-frame.cameraPosition[2];
            const double depth=clip[3][3]+x*clip[0][3]+y*clip[1][3]+z*clip[2][3];
            if (!(depth>frame.fogEnd)) return false;
        }
        return true;
    };
    const float light[]={frame.toLight[0],frame.toLight[1],frame.toLight[2],0};
    const auto colour=[](uint32_t value,float* output) {output[0]=((value>>16)&255)/255.f;output[1]=((value>>8)&255)/255.f;output[2]=(value&255)/255.f;output[3]=1;};
    float ambient[4],direct[4],specular[4];colour(frame.ambientColor,ambient);colour(frame.directColor,direct);
    colour(frame.sunColor,specular);specular[3]=20; // native terrain c27, captured with its material shader
    const float night=std::isfinite(frame.daylight)?1-std::clamp(frame.daylight,0.f,1.f):0.f;
    const float neutralLayers[16]={};
    const float noEmission[4]={};
    const float receiverConstants[]={receiver&&receiver->horizontal?1.f:0.f,receiverHeight,0,0};
    bool ok=SUCCEEDED(device->SetVertexShaderConstantF(13,receiverConstants,1)) && SUCCEEDED(device->SetPixelShaderConstantF(10,noEmission,1)) &&
        SUCCEEDED(device->SetPixelShaderConstantF(6,neutralLayers,4)) &&
        SUCCEEDED(device->SetVertexShaderConstantF(0,columns,4)) && SUCCEEDED(device->SetVertexShaderConstantF(11,light,1)) &&
        SUCCEEDED(device->SetVertexShaderConstantF(12,specular,1)) &&
        SUCCEEDED(device->SetPixelShaderConstantF(4,ambient,1)) && SUCCEEDED(device->SetPixelShaderConstantF(5,direct,1));
    const auto matrix=[&](const float* input) {
        float values[16];for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)values[c*4+r]=input[r*4+c];
        return SUCCEEDED(device->SetVertexShaderConstantF(6,values,4));
    };
    const auto address=[&](unsigned slot,unsigned flags) {
        return SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_ADDRESSU,flags&1 ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP)) &&
            SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_ADDRESSV,flags&2 ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP));
    };
    for (unsigned slot=0;slot<5;++slot) {
        ok=address(slot,slot==4 ? 0 : 3) &&
            SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_LINEAR)) && SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR)) &&
            SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_MIPFILTER,slot==4 ? D3DTEXF_NONE : D3DTEXF_LINEAR)) &&
            SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_MAXMIPLEVEL,0)) && SUCCEEDED(device->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE)) && ok;
    }
    // Cull submissions and raster work to the receiving water's projected area,
    // enlarged for wave lookup displacement. Geometry and materials remain shared.
    std::array<double,4> extent={-1,-1,1,1};
    RECT screen{};D3DVIEWPORT9 target{};
    // ReflectionBend adds a ten-yard slope correction before its bounded wave
    // shift. Its corrected point lies within ten yards of the wet quad; include
    // that world-space envelope so cropping cannot cut off a bent river mirror.
    const bool cachedArea=receiver && receiver->horizontal && m_options.receiverAreaKnown;
    const bool limited=receiver && !below && (cachedArea ||
        ReceiverViewScreenArea(frame,*receiver,scene.region->water,screen,receiver->horizontal?0.f:10.f));
    if(cachedArea)screen=m_options.receiverArea;
    if(limited){
        const LONG pad=LONG(std::ceil(m_options.warp*frame.viewport.Width))+3;
        screen.left=std::max(LONG(frame.viewport.X),screen.left-pad);
        screen.top=std::max(LONG(frame.viewport.Y),screen.top-pad);
        screen.right=std::min(LONG(frame.viewport.X+frame.viewport.Width),screen.right+pad);
        screen.bottom=std::min(LONG(frame.viewport.Y+frame.viewport.Height),screen.bottom+pad);
    }
    RECT savedScissor{},scissor{};
    if(limited){
        const auto& v=frame.viewport;
        const double left=double(screen.left-v.X)/v.Width,right=double(screen.right-v.X)/v.Width;
        const double top=double(screen.top-v.Y)/v.Height,bottom=double(screen.bottom-v.Y)/v.Height;
        extent={2*left-1,1-2*bottom,2*right-1,1-2*top};
        ok=SUCCEEDED(device->GetViewport(&target)) && ok;
        scissor={LONG(std::floor(left*target.Width)),LONG(std::floor(top*target.Height)),
                 LONG(std::ceil(right*target.Width)),LONG(std::ceil(bottom*target.Height))};
    }
    if(parts&Part::Scissor)scissor={0,0,1,1};
    if(limited || (parts&Part::Scissor)){
        ok=SUCCEEDED(device->GetScissorRect(&savedScissor)) && SUCCEEDED(device->SetScissorRect(&scissor)) &&
            SUCCEEDED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE)) && ok;
    }
    const math::ClipVolume visible(frame.cameraPosition,clip,extent);
    const ReceiverFogVolume fogVolume(frame.cameraPosition,clip,frame.viewport,receiverHeight,frame.fogEnd,m_options.warp);
    std::vector<uint64_t> terrainKeys,modelKeys;terrainKeys.swap(m_census);terrainKeys.clear();
    const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},terrainMode[]={0,0,below?1.f:0.f,frame.cameraWaterHeight},terrainMaterial[]={1,0,.3f,0};
    ok=matrix(identity) && SUCCEEDED(device->SetVertexShaderConstantF(10,terrainMode,1)) &&
        SUCCEEDED(device->SetPixelShaderConstantF(2,terrainMaterial,1)) && SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,cull)) && ok;
    std::array<std::array<float,4>,4> previousMaterials{};
    // Visibility can split one material set into several draws. Bind its four
    // layers only on a set change; this cache is local to this terrain pass.
    unsigned materialTile=~0u,materialSet=~0u;
    // Cells in index order (grouped by layer set); visible neighbours of one set extend a run drawn at once.
    struct Run {unsigned first=0,count=0,low=0,high=0,cell=0;
        unsigned gap=0,gapLow=0,gapHigh=0,gapCells=0;} run;
    unsigned tile=0;
    const auto flush=[&]() {
        if (!run.count || !ok) {run.count=run.gap=run.gapCells=0;return;}
        const auto set=m_terrain[tile].layout->sets[run.cell];
        if(materialTile!=tile || materialSet!=set){
            const auto& cell=scene.region->terrain[tile].tile.cells[run.cell];
            std::array<std::array<float,4>,4> materials{};
            for (unsigned layer=0;layer<4;++layer) {
                const auto& source=cell.layers[layer<cell.layers.size()?layer:0];
                materials[layer]=world::TerrainLayerParameters(source.flags,frame.modelMilliseconds);
                ok=SUCCEEDED(device->SetTexture(layer,m_textures[scene.region->terrain[tile].textures[source.texture]])) && ok;
            }
            if(materials!=previousMaterials){
                ok=SUCCEEDED(device->SetPixelShaderConstantF(6,materials[0].data(),4)) && ok;
                previousMaterials=materials;
            }
            materialTile=tile;materialSet=set;
        }
        if (ok) {ok=SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,run.low,run.high-run.low,run.first,run.count/3));if(ok)submitted();}
        run.count=run.gap=run.gapCells=0;
    };
    // A frustum-rejected cell writes no pixels inside this view's scissor.
    // Bridge such holes within a material run instead of paying another draw
    // call. Commit a gap only when another visible cell follows; fog rejection
    // still flushes, because fogged geometry can cover otherwise empty pixels.
    const auto outside=[&](unsigned c){
        if(!run.count)return;
        const auto& mesh=m_terrain[tile];const auto range=mesh.layout->cells[c];
        if(mesh.layout->sets[c]!=mesh.layout->sets[run.cell] || range.first!=run.first+run.count+run.gap){flush();return;}
        run.gapLow=run.gap?std::min(run.gapLow,range.vertex):range.vertex;
        run.gapHigh=run.gap?std::max(run.gapHigh,range.vertex+range.vertices):range.vertex+range.vertices;
        run.gap+=range.count;++run.gapCells;
    };
    const bool terrainDrawn=(parts&Part::Terrain) && shaders.terrainVs && shaders.terrainDeclaration;
    if (terrainDrawn) ok=SUCCEEDED(device->SetVertexShader(shaders.terrainVs)) && SUCCEEDED(device->SetVertexDeclaration(shaders.terrainDeclaration)) && ok;
    for (unsigned t=0;t<m_terrain.size() && ok && terrainDrawn;++t) {
        const auto& mesh=m_terrain[t];if (!mesh.indexCount) continue;
        if(receiver && !below){
            auto box=receiver->horizontal?data.terrain[t].sourceExtent:receiver->terrainExtents[t];
            if(receiver->horizontal){const auto low=box[2];box[2]=2*receiverHeight-box[5];box[5]=2*receiverHeight-low;}
            if(visible.Outside(box))continue;
        }
        uint16_t visibleGroups=0xffff;
        if(receiver && !below){
            visibleGroups=0;
            for(unsigned g=0;g<OwnedTerrainRenderData::GroupCount;++g){
                if(!(data.terrain[t].groupMask&(1u<<g)))continue;
                auto box=receiver->horizontal?data.terrain[t].sourceGroups[g]:
                    receiver->terrainGroups[t*OwnedTerrainRenderData::GroupCount+g];
                if(receiver->horizontal){const auto low=box[2];box[2]=2*receiverHeight-box[5];box[5]=2*receiverHeight-low;}
                if(!visible.Outside(box))visibleGroups|=uint16_t(1u<<g);
            }
            if(!visibleGroups)continue;
        }
        tile=t;
        ok=SUCCEEDED(device->SetStreamSource(0,mesh.vertices,0,sizeof(OwnedWorldVertex))) &&
            SUCCEEDED(device->SetStreamSource(1,mesh.vertices,mesh.vertexCount*sizeof(OwnedWorldVertex),kTerrainAlphaStride)) &&
            SUCCEEDED(device->SetStreamSource(2,receiver&&!receiver->horizontal?m_receiverBuffers[m_options.receiver+1]:mesh.vertices,
                receiver&&!receiver->horizontal?m_terrainVertexOffsets[t]*sizeof(float):32,
                receiver&&!receiver->horizontal?sizeof(float):sizeof(OwnedWorldVertex))) &&
            SUCCEEDED(device->SetIndices(mesh.indices)) && SUCCEEDED(device->SetTexture(4,m_alpha[t]));
        for (unsigned k=0;k<mesh.layout->order.size() && ok;++k) {
            const auto c=mesh.layout->order[k];const auto range=mesh.layout->cells[c];
            if (!range.count) continue;
            const auto& sourceCell=scene.region->terrain[t].tile.cells[c];
            if(!(visibleGroups&(1u<<OwnedTerrainRenderData::Group(sourceCell.x,sourceCell.y)))){outside(c);continue;}
            auto bounds=data.terrain[t].reflectedBounds[c];
            if(receiver){
                if(receiver->horizontal){bounds=data.terrain[t].sourceBounds[c];const float low=bounds[2];
                    bounds[2]=2*receiverHeight-bounds[5];bounds[5]=2*receiverHeight-low;}
                else bounds=receiver->reflectedBounds[m_terrainCellOffsets[t]+c];
            }
            if (!below && visible.Outside(bounds)) {outside(c);continue;}
            if (fogSkip && fogVolume.Contains(bounds)) {++m_stats.fogSkipped;flush();continue;}
            if (pastFogSkip && pastFog(bounds)) {++m_stats.pastFog;flush();continue;}
            const auto& cell=scene.region->terrain[t].tile.cells[c];
            if (m_options.census) {
                uint64_t key=t;
                for (unsigned layer=0;layer<4;++layer) {
                    const auto& source=cell.layers[layer<cell.layers.size()?layer:0];
                    key=key*1000003u+scene.region->terrain[t].textures[source.texture];key=key*31u+source.flags;
                }
                terrainKeys.push_back(key);
            }
            counted(OwnedWorldDrawStats::kSizes-1,range.count);++m_stats.terrainCells;
            if (run.count && mesh.layout->sets[c]==mesh.layout->sets[run.cell] && range.first==run.first+run.count+run.gap) {
                // Diagnostics count submitted geometry, including cells left to
                // hardware clipping; the saved draw calls are not free GPU work.
                if(run.gap){
                    m_stats.terrainCells+=run.gapCells;m_stats.batches+=run.gapCells;
                    m_stats.sizeBatches[OwnedWorldDrawStats::kSizes-1]+=run.gapCells;
                    m_stats.triangles+=run.gap/3;m_stats.sizeTriangles[OwnedWorldDrawStats::kSizes-1]+=run.gap/3;
                    run.low=std::min(run.low,run.gapLow);run.high=std::max(run.high,run.gapHigh);
                }
                run.count+=run.gap+range.count;run.gap=run.gapCells=0;
                run.low=std::min(run.low,range.vertex);run.high=std::max(run.high,range.vertex+range.vertices);
            } else {
                flush();run={range.first,range.count,range.vertex,range.vertex+range.vertices,c};
            }
        }
        flush();
    }
    if (terrainDrawn) ok=SUCCEEDED(device->SetStreamSource(2,nullptr,0,0)) && SUCCEEDED(device->SetStreamSource(1,nullptr,0,0)) && SUCCEEDED(device->SetVertexShader(shaders.vs)) &&
        SUCCEEDED(device->SetVertexDeclaration(shaders.declaration)) && ok;
    if(ok && (parts&Part::Horizon) && m_models.size()>scene.models.size() && !below && horizontal && frame.fogEnd>frame.fogStart){
        const auto& mesh=m_models.back();
        const float mode[]={1,receiverHeight,0,0},material[]={-1,0,0,0};
        ok=matrix(identity) && SUCCEEDED(device->SetVertexShaderConstantF(10,mode,1)) &&
            SUCCEEDED(device->SetPixelShaderConstantF(2,material,1)) &&
            SUCCEEDED(device->SetPixelShaderConstantF(10,noEmission,1)) &&
            SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) &&
            SUCCEEDED(device->SetStreamSource(0,mesh.vertices,0,sizeof(OwnedWorldVertex))) && SUCCEEDED(device->SetIndices(mesh.indices)) &&
            SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,mesh.vertexCount,0,mesh.indexCount/3));
        if(ok){counted(OwnedWorldDrawStats::kSizes-1,mesh.indexCount);submitted();}
    }
    // One model batch's material state, shared by single and instanced draws.
    unsigned modelAddress=3; // slot zero starts wrapped on both axes in this pass
    const auto batchState=[&](const world::SceneModel& model,const world::ModelDraw& draw){
        const float material[]={0,draw.blend==1 ? .5f : 0.f,0,float(draw.materialFlags&3)};
        float emission[4];colour(draw.nightEmission,emission);
        for(unsigned channel=0;channel<3;++channel)emission[channel]*=night;
        const unsigned flags=model.geometry->mesh.textures[draw.texture].flags&3u;
        if (flags!=modelAddress) {
            if (!address(0,flags)) return false;
            modelAddress=flags;
        }
        return SUCCEEDED(device->SetTexture(0,m_textures[model.textures[draw.texture]])) &&
            SUCCEEDED(device->SetPixelShaderConstantF(2,material,1)) &&
            SUCCEEDED(device->SetPixelShaderConstantF(10,emission,1)) &&
            SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,draw.materialFlags&4 ? D3DCULL_NONE : cull));
    };
    m_queued.clear();
    const auto drawInstance=[&](const world::SceneInstance& instance,float height){
        if(instance.model>=m_models.size())return false;
        if (below?instance.minimum[2]>height:instance.maximum[2]<height) return true;
        const std::array<float,6> reflected={instance.minimum[0],instance.minimum[1],2*height-instance.maximum[2],
                                          instance.maximum[0],instance.maximum[1],2*height-instance.minimum[2]};
        if (visible.Outside(reflected)) return true;
        if (fogSkip && fogVolume.Contains(reflected)) {++m_stats.fogSkipped;return true;}
        if (pastFogSkip && pastFog(reflected)) {
            const auto& draws=scene.models[instance.model].geometry->mesh.draws;
            if (std::none_of(draws.begin(),draws.end(),[](const auto& draw){return (draw.materialFlags&2)!=0;})) {++m_stats.pastFog;return true;}
        }
        ++m_stats.instances;
        const auto bucket=m_options.census ? SizeBucket(ScreenSize(reflected,frame.cameraPosition,clip,frame.viewport)) : OwnedWorldDrawStats::kSizes-1;
        if (m_options.census) modelKeys.push_back(instance.model);
        const auto& mesh=m_models[instance.model];const auto& model=scene.models[instance.model];
        for (const auto& draw : model.geometry->mesh.draws) counted(bucket,draw.indexCount);
        const float mode[]={1,height,below?1.f:0.f,frame.cameraWaterHeight};
        if (instancing) {
            Queued queued{instance.model,{}};
            for (unsigned row=0;row<3;++row) for (unsigned c=0;c<4;++c) queued.instance.world[row][c]=instance.transform[c*4+row];
            std::copy(mode,mode+4,queued.instance.draw);
            m_queued.push_back(queued);
            return true;
        }
        ok=matrix(instance.transform.data()) && SUCCEEDED(device->SetVertexShaderConstantF(10,mode,1)) &&
            SUCCEEDED(device->SetStreamSource(0,mesh.vertices,0,sizeof(OwnedWorldVertex))) && SUCCEEDED(device->SetIndices(mesh.indices));
        for (const auto& draw : model.geometry->mesh.draws) {
            if (!ok) break;
            ok=batchState(model,draw);
            if (ok) {ok=SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,mesh.vertexCount,draw.firstIndex,draw.indexCount/3));if(ok)submitted();}
        }
        return ok;
    };
    const auto instanceAt=[&](unsigned i){return drawInstance(scene.region->instances[i],below?frame.cameraWaterHeight:receiver?
        (receiver->horizontal?receiver->height:receiver->instanceHeights[i]):data.instanceHeights[i]);};
    if(receiver && !below && !data.instanceGroups.empty()){
        for(unsigned g=0;g<data.instanceGroups.size() && ok && (parts&Part::Models);++g){
            const auto& group=data.instanceGroups[g];
            auto box=receiver->horizontal?group.bounds:receiver->instanceGroupBounds[g];
            if(receiver->horizontal){const auto low=box[2];box[2]=2*receiverHeight-box[5];box[5]=2*receiverHeight-low;}
            if(visible.Outside(box))continue;
            for(unsigned i:group.instances){ok=instanceAt(i);if(!ok)break;}
        }
    }else for(unsigned i=0;i<scene.region->instances.size() && ok && (parts&Part::Models);++i)ok=instanceAt(i);
    for(unsigned i=0;i<std::min(frame.reflectionObjectCount,kMaxReflectionObjects) && ok && (parts&Part::Live);++i){
        const auto& object=frame.reflectionObjects[i];if(!ValidReflectionObject(object))continue;
        const auto asset=std::find_if(scene.liveAssets.begin(),scene.liveAssets.end(),[&](const auto& a){return a.display==object.display;});
        if(asset==scene.liveAssets.end())continue;
        if(asset->source>=scene.wmoSources.size())return false;
        float height=frame.cameraWaterHeight;
        if(!below){
            if(receiver&&receiver->horizontal)height=receiver->height;
            else if(data.receivers.Empty()){
                if(!reflection::WorldWaterReference(scene.region->water,object.transform+12,height))continue;
            }else if(!data.receivers.HeightAt(scene.region->water,receiver?receiver->body:data.receiverBody,object.transform[12],object.transform[13],height))continue;
        }
        const auto drawLive=[&](uint32_t model,const float* local){
            if(model>=scene.models.size())return false;
            world::SceneInstance current;current.model=model;
            if(local){if(!world::ComposePlacement(local,object.transform,current.transform))return false;}
            else std::copy(object.transform,object.transform+16,current.transform.begin());
            const auto& mesh=scene.models[model].geometry->mesh;
            if(!world::TransformBounds(mesh.minimum,mesh.maximum,current.transform.data(),current.minimum,current.maximum))return false;
            const auto before=m_stats.batches;const bool drawn=drawInstance(current,height);m_stats.liveDraws+=m_stats.batches-before;return drawn;
        };
        const auto& source=scene.wmoSources[asset->source];
        for(auto model:source.models){ok=drawLive(model,nullptr);if(!ok)break;}
        for(const auto& attachment:source.attachments){
            if(!ok)break;
            if(attachment.source>=scene.modelSources.size())return false;
            const auto model=scene.modelSources[attachment.source].model;
            if(model!=world::MissingSceneAsset)ok=drawLive(model,attachment.transform.data());
        }
    }
    if (ok && !m_queued.empty()) ok=DrawQueued(device,scene,shaders,batchState,submitted);
    m_queued.clear();
    if (limited || (parts&Part::Scissor)) ok=SUCCEEDED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)) && SUCCEEDED(device->SetScissorRect(&savedScissor)) && ok;
    std::sort(terrainKeys.begin(),terrainKeys.end());
    m_stats.terrainSets=unsigned(std::unique(terrainKeys.begin(),terrainKeys.end())-terrainKeys.begin());
    std::sort(modelKeys.begin(),modelKeys.end());modelKeys.erase(std::unique(modelKeys.begin(),modelKeys.end()),modelKeys.end());
    m_stats.models=unsigned(modelKeys.size());
    for (auto model : modelKeys) if (model<scene.models.size()) m_stats.modelBatches+=unsigned(scene.models[model].geometry->mesh.draws.size());
    m_census.swap(terrainKeys);
    return ok;
}
// Every queued copy of a model in one draw per batch: copies grouped by model (draw order within a model kept), their
// placement rows written into a dynamic instance stream that wraps (discarding) when full.
template<class BatchState,class Submitted>
bool OwnedWorldGpu::DrawQueued(IDirect3DDevice9* device,const world::WorldScene& scene,const OwnedWorldShaders& shaders,
                               const BatchState& batchState,const Submitted& submitted)
{
    // Model indices are dense. Scatter indices backwards to preserve placement
    // order within each model without sorting or moving the transforms.
    m_modelOffsets.assign(scene.models.size()+1,0);
    for (const auto& item : m_queued) ++m_modelOffsets[item.model];
    std::partial_sum(m_modelOffsets.begin(),m_modelOffsets.end(),m_modelOffsets.begin());
    m_instanceOrder.resize(m_queued.size());
    for (std::size_t i=m_queued.size();i>0;--i)
        m_instanceOrder[--m_modelOffsets[m_queued[i-1].model]]=static_cast<unsigned>(i-1);
    if (!m_instances) {
        m_instanceCapacity=InstanceCapacity(scene);
        if (FAILED(device->CreateVertexBuffer(m_instanceCapacity*sizeof(OwnedWorldInstance),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&m_instances,nullptr)))
            return false;
    }
    bool ok=SUCCEEDED(device->SetVertexShader(shaders.instancedVs)) && SUCCEEDED(device->SetVertexDeclaration(shaders.instancedDeclaration));
    // Upload the visible instances together. Issue every draw that references
    // a chunk before discarding the stream for the next one.
    for (std::size_t begin=0;begin<m_queued.size() && ok;) {
        const auto count=unsigned(std::min<std::size_t>(m_instanceCapacity,m_queued.size()-begin));
        void* target=nullptr;
        ok=SUCCEEDED(m_instances->Lock(0,count*sizeof(OwnedWorldInstance),&target,D3DLOCK_DISCARD));
        if (!ok) break;
        auto* output=static_cast<OwnedWorldInstance*>(target);
        for (unsigned i=0;i<count;++i) output[i]=m_queued[m_instanceOrder[begin+i]].instance;
        ok=SUCCEEDED(m_instances->Unlock()) &&
            SUCCEEDED(device->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1u));
        for (unsigned first=0;first<count && ok;) {
            const auto modelIndex=m_queued[m_instanceOrder[begin+first]].model;
            const auto end=static_cast<unsigned>(std::min<std::size_t>(begin+count,m_modelOffsets[modelIndex+1])-begin);
            const auto& mesh=m_models[modelIndex];const auto& model=scene.models[modelIndex];
            ok=SUCCEEDED(device->SetStreamSource(0,mesh.vertices,0,sizeof(OwnedWorldVertex))) &&
                SUCCEEDED(device->SetIndices(mesh.indices)) &&
                SUCCEEDED(device->SetStreamSource(1,m_instances,first*sizeof(OwnedWorldInstance),sizeof(OwnedWorldInstance))) &&
                SUCCEEDED(device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|(end-first)));
            for (const auto& draw : model.geometry->mesh.draws) {
                if (!ok) break;
                ok=batchState(model,draw) &&
                    SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,mesh.vertexCount,draw.firstIndex,draw.indexCount/3));
                if (ok) submitted();
            }
            first=end;
        }
        begin+=count;
    }
    // The enclosing pass restores the host's state; leave this pass's own non-instanced state for anything after it.
    const bool restored=SUCCEEDED(device->SetStreamSourceFreq(0,1)) && SUCCEEDED(device->SetStreamSourceFreq(1,1)) &&
        SUCCEEDED(device->SetStreamSource(1,nullptr,0,0)) &&
        SUCCEEDED(device->SetVertexShader(shaders.vs)) && SUCCEEDED(device->SetVertexDeclaration(shaders.declaration));
    return ok && restored;
}
}
