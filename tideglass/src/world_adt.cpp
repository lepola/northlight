#include "world_adt.h"
#include "world_terrain_material.h"
#include "world_asset_name.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace water::world
{
namespace
{
uint32_t U32(Bytes b, std::size_t at) { uint32_t v=0; b.U32(at,v); return v; }
uint16_t U16(Bytes b, std::size_t at) { uint16_t v=0; b.U16(at,v); return v; }
bool Float(Bytes b, std::size_t at, float& v) { return b.F32(at,v) && std::isfinite(v); }
bool Subchunk(Bytes b, unsigned field, const char* tag, std::size_t expected, Bytes& out)
{
    const auto at=U32(b,field);
    if (at<136 || !b.Contains(at-8,8) || std::memcmp(b.data+at-8,tag,4)) return false;
    const auto n=U32(b,at-4);
    if (n!=expected || !b.Contains(at,n)) return false;
    out={b.data+at,n}; return true;
}
bool NameAt(Bytes b, std::size_t at, std::string_view& name)
{
    if (at>=b.size || (at && b.data[at-1])) return false;
    const auto* end=static_cast<const uint8_t*>(std::memchr(b.data+at,0,b.size-at));
    if (!end) return false;
    name={reinterpret_cast<const char*>(b.data+at),static_cast<std::size_t>(end-(b.data+at))};
    return ValidAssetName(name);
}
bool Extension(std::string_view name, std::string_view suffix)
{
    if (name.size()<suffix.size()) return false;
    const auto tail=name.substr(name.size()-suffix.size());
    for (unsigned i=0;i<tail.size();++i) {
        const auto c=tail[i];
        if ((c>='A' && c<='Z' ? char(c+'a'-'A') : c)!=suffix[i]) return false;
    }
    return true;
}
struct View
{
    Bytes version, textures, names, ids, placements, wmo, wmoNames, wmoIds;
    std::array<Bytes,256> cells{};
};
bool ReadView(Bytes input, View& view)
{
    unsigned cells=0;
    for (std::size_t at=0;at<input.size;) {
        if (!input.Contains(at,8)) return false;
        const auto n=U32(input,at+4);
        if (!input.Contains(at+8,n)) return false;
        const auto* tag=reinterpret_cast<const char*>(input.data+at);
        Bytes data{input.data+at+8,n}; Bytes* target=nullptr;
        if (!std::memcmp(tag,"REVM",4)) target=&view.version;
        else if (!std::memcmp(tag,"XETM",4)) target=&view.textures;
        else if (!std::memcmp(tag,"XDMM",4)) target=&view.names;
        else if (!std::memcmp(tag,"DIMM",4)) target=&view.ids;
        else if (!std::memcmp(tag,"FDDM",4)) target=&view.placements;
        else if (!std::memcmp(tag,"FDOM",4)) target=&view.wmo;
        else if (!std::memcmp(tag,"OMWM",4)) target=&view.wmoNames;
        else if (!std::memcmp(tag,"DIWM",4)) target=&view.wmoIds;
        else if (!std::memcmp(tag,"KNCM",4)) {
            if (n<128 || U32(data,4)>=16 || U32(data,8)>=16) return false;
            target=&view.cells[U32(data,8)*16+U32(data,4)]; ++cells;
        }
        if (target) { if (target->data) return false; *target=data; }
        at+=8+std::size_t(n);
    }
    return cells==256;
}
// Run lengths to 4096 bytes: a control byte's top bit says repeat the next byte, else copy; its low seven the count. Sizes only when
// output is null, which is how the whole tile is checked before anything is allocated.
bool ExpandAlpha(Bytes input,std::size_t at,uint8_t* output)
{
    for(unsigned written=0;written<4096;){
        if(at>=input.size)return false;
        const auto control=input.data[at++];const unsigned count=control&0x7f;
        if(!count||written+count>4096)return false;
        const bool repeat=control&0x80;
        if(!input.Contains(at,repeat?1:count))return false;
        if(output){if(repeat)std::memset(output+written,input.data[at],count);else std::memcpy(output+written,input.data+at,count);}
        at+=repeat?1:count;written+=count;
    }
    return true;
}
unsigned IndexCount(uint16_t holes)
{
    unsigned count=0;
    for (unsigned bit=0;bit<16;++bit) if (!(holes&(1u<<bit))) count+=48;
    return count;
}
}
std::size_t AdtTile::Bytes() const
{
    std::size_t n=sizeof(*this)+cells.capacity()*sizeof(TerrainCell)+placements.capacity()*sizeof(AdtModelPlacement)+
                  wmoInstances.capacity()*sizeof(AdtWmoPlacement)+
                  (textures.capacity()+models.capacity()+wmoModels.capacity())*sizeof(std::string);
    for (const auto* names : {&textures,&models,&wmoModels}) for (const auto& name : *names) n+=name.capacity()+1;
    for (const auto& cell : cells) {
        n+=cell.shadow.capacity()+cell.vertices.capacity()*sizeof(TerrainVertex)+cell.indices.capacity()*sizeof(uint16_t)+cell.layers.capacity()*sizeof(TerrainLayer);
        for (const auto& layer : cell.layers) n+=layer.alpha.capacity();
        n+=cell.colours.capacity()*sizeof(uint32_t);
    }
    return n;
}

bool DecodeLegacyLiquid(Bytes cell, std::vector<LegacyLiquid>& output)
{
    constexpr std::size_t kLayer=804,kHeights=8,kTiles=656;
    output.clear();
    const auto at=U32(cell,96),size=U32(cell,100);
    if (!at || size<=8) return true;
    if (at<136 || (size-8)%kLayer || !cell.Contains(at-8,size) || std::memcmp(cell.data+at-8,"QLCM",4)) return false;
    const auto flags=U32(cell,0);
    const auto layers=(size-8)/kLayer;
    uint16_t type=0;
    for (std::size_t layer=0;layer<layers;++layer) {
        // A cell's flags name its liquids, one bit each, in the order they are stored.
        do ++type; while (type<=4 && !(flags&(1u<<(type+1))));
        LegacyLiquid liquid;
        liquid.type=type<=4?type:1;
        const auto base=at+layer*kLayer;
        for (unsigned v=0;v<81;++v) std::memcpy(&liquid.heights[v],cell.data+base+kHeights+v*8+4,4);
        std::memcpy(liquid.tiles.data(),cell.data+base+kTiles,64);
        output.push_back(liquid);
    }
    return true;
}

AdtError DecodeAdt(world::Bytes input, AdtAlphaFormat alpha, std::size_t limit, AdtTile& output)
{
    if (alpha!=AdtAlphaFormat::Packed4 && alpha!=AdtAlphaFormat::Byte8) return AdtError::Unsupported;
    View v;
    if (!ReadView(input,v)) return AdtError::Layout;
    if (v.version.size!=4 || U32(v.version,0)!=18) return AdtError::Header;
    if (!v.textures.size || !v.names.data || !v.ids.data || !v.placements.data || v.ids.size%4 ||
        v.placements.size%36 || v.wmo.size%64 || v.wmoIds.size%4 ||
        (v.wmo.size && (!v.wmoNames.data || !v.wmoIds.size))) return AdtError::Layout;
    const std::size_t modelCount=v.ids.size/4, placementCount=v.placements.size/36;
    if (modelCount>65536 || placementCount>65536 || v.wmo.size/64>65536) return AdtError::Budget;
    std::size_t left=limit;
    const auto charge=[&](std::size_t count,std::size_t size) {
        if (count>left/size) return false;
        left-=count*size; return true;
    };
    if (!charge(1,sizeof(AdtTile)) || !charge(256,sizeof(TerrainCell)) || !charge(256*145,sizeof(TerrainVertex)) ||
        !charge(placementCount,sizeof(AdtModelPlacement)) || !charge(modelCount,sizeof(std::string)) ||
        !charge(v.wmo.size/64,sizeof(AdtWmoPlacement)) || !charge(v.wmoIds.size/4,sizeof(std::string))) return AdtError::Budget;
    std::size_t textureCount=0;
    for (std::size_t at=0;at<v.textures.size;) {
        std::string_view name;
        if (!NameAt(v.textures,at,name) || name.size()<4) return AdtError::Material;
        if (!Extension(name,".blp")) return AdtError::Material;
        if (++textureCount>4096 || !charge(1,sizeof(std::string)) ||
            !charge(std::max(name.size()+1,std::string().capacity()+1),1)) return AdtError::Budget;
        at+=name.size()+1;
    }
    for (std::size_t i=0;i<modelCount;++i) {
        std::string_view name;
        if (!NameAt(v.names,U32(v.ids,i*4),name)) return AdtError::Placement;
        // MDDF names may retain the older .mdx suffix, but the resolved source is M2.
        if (!Extension(name,".m2") && !Extension(name,".mdx")) return AdtError::Placement;
        if (!charge(std::max(name.size()+1,std::string().capacity()+1),1)) return AdtError::Budget;
    }
    if(v.wmoIds.size/4>65536)return AdtError::Budget;
    for(std::size_t i=0;i<v.wmoIds.size/4;++i) {
        std::string_view name;
        if(!NameAt(v.wmoNames,U32(v.wmoIds,i*4),name)||!Extension(name,".wmo"))return AdtError::Placement;
        if(!charge(std::max(name.size()+1,std::string().capacity()+1),1))return AdtError::Budget;
    }
    const unsigned encoded=alpha==AdtAlphaFormat::Packed4 ? 2048 : 4096;
    // Validate every referenced payload and admit the entire tile before heap allocation.
    for (const auto raw : v.cells) {
        const auto layers=U32(raw,12), flags=U32(raw,0), holes=U32(raw,60);
        if (layers>4 || holes>0xffff) return AdtError::Layout;
        Bytes heights,normals,layerData,alphaData,shadow,colours;
        if (((flags&0x40) || U32(raw,116)) && !Subchunk(raw,116,"VCCM",580,colours)) return AdtError::Layout;
        // A cell with no layers (Northrend has some, out of sight) is drawn with nothing, so has no triangles here.
        if (!Subchunk(raw,20,"TVCM",580,heights) || !Subchunk(raw,24,"RNCM",435,normals) ||
            (layers && !Subchunk(raw,28,"YLCM",layers*16,layerData))) return AdtError::Layout;
        bool compressed=false;
        for (unsigned i=1;i<layers;++i) compressed=compressed||(U32(layerData,i*16+4)&0x200);
        if (compressed && alpha!=AdtAlphaFormat::Byte8) return AdtError::Unsupported;
        if (layers>1 && alpha==AdtAlphaFormat::Byte8) {
            // The chunk's own size is not what the cell's says in the client's files (512 against 2596); the cell's holds the data.
            const auto at=U32(raw,36),span=U32(raw,40);
            if (span<8 || at<136 || !raw.Contains(at-8,span) || std::memcmp(raw.data+at-8,"LACM",4)) return AdtError::Layout;
            alphaData={raw.data+at,span-8};
            if (!compressed && alphaData.size!=(layers-1)*encoded) return AdtError::Layout;
        }
        else if (layers>1 && (!Subchunk(raw,36,"LACM",(layers-1)*encoded,alphaData) ||
                         U32(raw,40)!=alphaData.size+8)) return AdtError::Layout;
        if ((flags&1) && (!Subchunk(raw,44,"HSCM",512,shadow) || U32(raw,48)!=512)) return AdtError::Layout;
        if (!charge(IndexCount(static_cast<uint16_t>(holes)),sizeof(uint16_t)) || !charge(layers,sizeof(TerrainLayer)) ||
            !charge(layers?layers-1:0,4096) || !charge((flags&1)?512:0,1) || !charge(colours.size,1)) return AdtError::Budget;
        for (unsigned i=0;i<layers;++i) {
            const auto at=i*16;
            if (U32(layerData,at)>=textureCount) return AdtError::Material;
            const auto layerFlags=U32(layerData,at+4), offset=U32(layerData,at+8);
            // 0x400 asks for the game's cube-map reflection on the layer, which this picture does without.
            const auto kind=layerFlags&~0x4ffu;
            if (i ? (kind!=0x100u && !(compressed && kind==0x300u)) : kind!=0) return AdtError::Unsupported;
            if (!compressed && offset!=(i ? (i-1)*encoded : 0)) return AdtError::Layout;
            if (compressed && i) {
                if (kind==0x300u ? !ExpandAlpha(alphaData,offset,nullptr) : !alphaData.Contains(offset,encoded)) return AdtError::Layout;
            }
        }
    }
    AdtTile candidate;
    candidate.textures.reserve(textureCount); candidate.models.reserve(modelCount);
    candidate.cells.resize(256); candidate.placements.resize(placementCount);
    candidate.wmoPlacements=static_cast<uint32_t>(v.wmo.size/64);
    candidate.wmoModels.reserve(v.wmoIds.size/4);candidate.wmoInstances.resize(v.wmo.size/64);
    for(std::size_t i=0;i<v.wmoIds.size/4;++i) {
        std::string_view name;NameAt(v.wmoNames,U32(v.wmoIds,i*4),name);std::string canonical;
        CanonicalAssetName(name,canonical);candidate.wmoModels.push_back(std::move(canonical));
    }
    for(std::size_t i=0;i<candidate.wmoInstances.size();++i) {
        const auto at=i*64;auto& placement=candidate.wmoInstances[i];
        placement.model=U32(v.wmo,at);placement.uid=U32(v.wmo,at+4);
        if(placement.model>=candidate.wmoModels.size())return AdtError::Placement;
        float lowBounds[3],highBounds[3];
        for(unsigned axis=0;axis<3;++axis) {
            if(!Float(v.wmo,at+8+axis*4,placement.pose.position[axis])||
               !Float(v.wmo,at+20+axis*4,placement.pose.rotation[axis])||
               !Float(v.wmo,at+32+axis*4,lowBounds[axis])||!Float(v.wmo,at+44+axis*4,highBounds[axis])||lowBounds[axis]>highBounds[axis])return AdtError::Placement;
        }
        constexpr float origin=17066.666666666668f;
        placement.minimum={origin-highBounds[2],origin-highBounds[0],lowBounds[1]};
        placement.maximum={origin-lowBounds[2],origin-lowBounds[0],highBounds[1]};
        placement.flags=U16(v.wmo,at+56);placement.doodadSet=U16(v.wmo,at+58);placement.nameSet=U16(v.wmo,at+60);
        placement.pose.scale=(placement.flags&4)?U16(v.wmo,at+62):1024;
        if(!placement.pose.scale)return AdtError::Placement;
    }
    for (std::size_t at=0;at<v.textures.size;) {
        std::string_view name; NameAt(v.textures,at,name); std::string canonical;
        CanonicalAssetName(name,canonical); candidate.textures.push_back(std::move(canonical)); at+=name.size()+1;
    }
    for (std::size_t i=0;i<modelCount;++i) {
        std::string_view name; NameAt(v.names,U32(v.ids,i*4),name); std::string canonical;
        CanonicalAssetName(name,canonical);
        if (canonical.size()>=4 && canonical.substr(canonical.size()-4)==".mdx") canonical.replace(canonical.size()-4,4,".m2");
        candidate.models.push_back(std::move(canonical));
    }
    for (std::size_t i=0;i<placementCount;++i) {
        const auto at=i*36; auto& placement=candidate.placements[i];
        placement.model=U32(v.placements,at); placement.uid=U32(v.placements,at+4);
        if (placement.model>=modelCount) return AdtError::Placement;
        for (unsigned axis=0;axis<3;++axis)
            if (!Float(v.placements,at+8+axis*4,placement.pose.position[axis]) ||
                !Float(v.placements,at+20+axis*4,placement.pose.rotation[axis])) return AdtError::Placement;
        placement.pose.scale=U16(v.placements,at+32); placement.flags=U16(v.placements,at+34);
        if (!placement.pose.scale) return AdtError::Placement;
    }
    constexpr float spacing=1600.f/3/128;
    for (unsigned i=0;i<256;++i) {
        const auto raw=v.cells[i]; auto& cell=candidate.cells[i];
        cell.x=U32(raw,4); cell.y=U32(raw,8); cell.flags=U32(raw,0); cell.holes=static_cast<uint16_t>(U32(raw,60));
        float origin[3];
        for (unsigned axis=0;axis<3;++axis) if (!Float(raw,104+axis*4,origin[axis])) return AdtError::Geometry;
        const auto heights=U32(raw,20), normals=U32(raw,24);
        unsigned index=0;
        for (unsigned row=0;row<17;++row) for (unsigned col=0;col<(row&1 ? 8u : 9u);++col,++index) {
            auto& vertex=cell.vertices[index]; float height;
            if (!Float(raw,heights+index*4,height)) return AdtError::Geometry;
            const float r=row*0.5f, c=col+(row&1 ? 0.5f : 0.f);
            vertex.position={origin[0]-r*spacing,origin[1]-c*spacing,origin[2]+height};
            vertex.uv={c/8,r/8}; double length2=0;
            for (unsigned axis=0;axis<3;++axis) {
                const auto b=raw.data[normals+index*3+axis];
                const int value=b<128 ? b : int(b)-256;
                vertex.normal[axis]=value/127.f; length2+=double(vertex.normal[axis])*vertex.normal[axis];
                if (!std::isfinite(vertex.position[axis])) return AdtError::Geometry;
                if (!index) cell.minimum[axis]=cell.maximum[axis]=vertex.position[axis];
                else { cell.minimum[axis]=std::min(cell.minimum[axis],vertex.position[axis]);
                       cell.maximum[axis]=std::max(cell.maximum[axis],vertex.position[axis]); }
            }
            if (length2<0.25 || length2>2.25) return AdtError::Geometry;
        }
        if (U32(raw,12)) cell.indices.reserve(IndexCount(cell.holes));
        if (U32(raw,12)) for (unsigned row=0;row<8;++row) for (unsigned col=0;col<8;++col) {
            if (cell.holes&(1u<<((row/2)*4+col/2))) continue;
            const uint16_t tl=static_cast<uint16_t>(row*17+col), centre=tl+9, tr=tl+1, bl=tl+17, br=tl+18;
            const uint16_t indices[]={tl,centre,tr,tr,centre,br,br,centre,bl,bl,centre,tl};
            cell.indices.insert(cell.indices.end(),std::begin(indices),std::end(indices));
        }
        if (cell.flags&1) {cell.shadow.resize(512);std::memcpy(cell.shadow.data(),raw.data+U32(raw,44),512);}
        if ((cell.flags&0x40) || U32(raw,116)) {cell.colours.resize(145);std::memcpy(cell.colours.data(),raw.data+U32(raw,116),580);}
        const auto layers=U32(raw,12), layerData=U32(raw,28), alphaAt=U32(raw,36);
        cell.layers.resize(layers);
        for (unsigned k=0;k<layers;++k) {
            auto& layer=cell.layers[k]; layer.texture=U32(raw,layerData+k*16); layer.flags=U32(raw,layerData+k*16+4);
            layer.effect=U32(raw,layerData+k*16+12);
            if (!k) continue;
            layer.alpha.resize(4096);
            const auto offset=U32(raw,layerData+k*16+8);
            if (layer.flags&0x200) {
                const auto span=U32(raw,40);
                if (!ExpandAlpha({raw.data+alphaAt,span-8},offset,layer.alpha.data())) return AdtError::Layout;
                continue;
            }
            const auto* source=raw.data+alphaAt+offset;
            for (unsigned p=0;p<4096;++p)
                layer.alpha[p]=alpha==AdtAlphaFormat::Byte8 ? source[p] : static_cast<uint8_t>(((source[p/2]>>((p&1)*4))&15)*17);
        }
        if(alpha==AdtAlphaFormat::Packed4)for(unsigned p=0;p<4096;++p){
            uint8_t coverage[3]={};
            for(unsigned k=1;k<layers;++k)coverage[k-1]=cell.layers[k].alpha[p];
            ResolveTerrainCoverage(coverage,layers-1);
            for(unsigned k=1;k<layers;++k)cell.layers[k].alpha[p]=coverage[k-1];
        }
    }
    if (candidate.Bytes()>limit) return AdtError::Budget;
    output=std::move(candidate); return AdtError::None;
}
}
