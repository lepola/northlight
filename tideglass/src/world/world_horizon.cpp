#include "world/world_horizon.h"
#include <algorithm>
#include <cstring>

namespace water::world
{
HorizonError DecodeHorizon(Bytes bytes,const OwnedWorldRegion& region,const std::atomic<bool>& cancelled,
                           std::size_t limit,StaticModel& output,unsigned& tiles)
{
    if(!region.Valid())return HorizonError::Unsupported;
    if(bytes.size>HorizonInputLimit)return HorizonError::Budget;
    Bytes offsets;bool version=false;
    // Validate all chunk boundaries before following the absolute MAOF offsets.
    std::vector<uint32_t> areas;
    areas.reserve(4096);
    for(std::size_t at=0;at<bytes.size;){
        if(cancelled.load())return HorizonError::Cancelled;
        uint32_t size=0;
        if(!bytes.Contains(at,8)||!bytes.U32(at+4,size)||!bytes.Contains(at+8,size))return HorizonError::Layout;
        const auto* tag=bytes.data+at;
        if(!std::memcmp(tag,"REVM",4)){
            uint32_t v=0;if(version||size!=4||!bytes.U32(at+8,v)||v!=18)return HorizonError::Layout;
            version=true;
        }else if(!std::memcmp(tag,"FOAM",4)){
            if(offsets.data||size!=4096*4)return HorizonError::Layout;
            offsets={tag+8,size};
        }else if(!std::memcmp(tag,"ERAM",4)){
            if(size!=1090||areas.size()==4096)return HorizonError::Layout;
            areas.push_back(uint32_t(at));
        }else if(!std::memcmp(tag,"OHAM",4)&&size!=32)return HorizonError::Layout;
        at+=8+size;
    }
    if(!version||!offsets.data)return HorizonError::Layout;
    struct Tile {unsigned x,y;uint32_t at;std::array<uint16_t,16> holes;};
    std::vector<Tile> selected;selected.reserve(256);
    constexpr double unit=1600.0/3/16,origin=17066.666666666668;
    const auto admitted=[&](unsigned tx,unsigned ty,unsigned x,unsigned y){
        const double px=origin-(ty*16+y+1)*unit,py=origin-(tx*16+x+1)*unit;
        const double dx=px-region.Centre(0),dy=py-region.Centre(1);
        // The detailed region too: where its terrain lies wholly past the fog it is left out of the reflection, and this
        // coarse surface stands in for it (drawn only where wholly fogged, so never over the clear detailed terrain).
        return dx*dx+dy*dy<=double(HorizonReach)*HorizonReach;
    };
    std::size_t indices=0;
    for(unsigned index=0;index<4096;++index){
        uint32_t at=0;offsets.U32(index*4,at);if(!at)continue;
        if(!std::binary_search(areas.begin(),areas.end(),at))return HorizonError::Layout;
        Tile tile{index%64,index/64,at,{}};
        const auto after=std::size_t(at)+8+1090;
        if(bytes.Contains(after,8)&&!std::memcmp(bytes.data+after,"OHAM",4))
            for(unsigned y=0;y<16;++y)bytes.U16(after+8+y*2,tile.holes[y]);
        std::size_t count=0;
        for(unsigned y=0;y<16;y+=2)for(unsigned x=0;x<16;x+=2)
            if(admitted(tile.x,tile.y,x,y) && !(((tile.holes[y]|tile.holes[y+1])>>x)&3))count+=12;
        if(!count)continue;
        if(selected.size()==256)return HorizonError::Budget;
        selected.push_back(tile);indices+=count;
    }
    // 9x9 outer samples and 8x8 centres from the original 17x17 outer lattice.
    const auto vertices=selected.size()*145;
    const auto required=sizeof(StaticModel)+vertices*sizeof(ModelVertex)+indices*sizeof(uint16_t);
    if(vertices>65536||required>std::min(limit,HorizonMeshLimit))return HorizonError::Budget;
    StaticModel mesh;mesh.vertices.reserve(vertices);mesh.indices.reserve(indices);
    for(const auto& tile:selected){
        if(cancelled.load())return HorizonError::Cancelled;
        const auto base=unsigned(mesh.vertices.size());
        const auto vertex=[&](unsigned x,unsigned y){
            uint16_t bits=0;bytes.U16(tile.at+8+(y*17+x)*2,bits);
            const int height=bits<32768?int(bits):int(bits)-65536;
            ModelVertex v;v.position={float(origin-(tile.y*16+y)*unit),float(origin-(tile.x*16+x)*unit),float(height)};
            v.normal={0,0,1};mesh.vertices.push_back(v);
            for(unsigned axis=0;axis<3;++axis){
                if(mesh.vertices.size()==1)mesh.minimum[axis]=mesh.maximum[axis]=v.position[axis];
                else {mesh.minimum[axis]=std::min(mesh.minimum[axis],v.position[axis]);mesh.maximum[axis]=std::max(mesh.maximum[axis],v.position[axis]);}
            }
        };
        for(unsigned y=0;y<=16;y+=2)for(unsigned x=0;x<=16;x+=2)vertex(x,y);
        for(unsigned y=1;y<16;y+=2)for(unsigned x=1;x<16;x+=2)vertex(x,y);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            if(!admitted(tile.x,tile.y,x*2,y*2)||(((tile.holes[y*2]|tile.holes[y*2+1])>>(x*2))&3))continue;
            const unsigned corners[]={base+y*9+x,base+(y+1)*9+x,base+(y+1)*9+x+1,base+y*9+x+1};
            const auto centre=base+81+y*8+x;
            for(unsigned side=0;side<4;++side){mesh.indices.push_back(uint16_t(corners[side]));mesh.indices.push_back(uint16_t(corners[(side+1)%4]));mesh.indices.push_back(uint16_t(centre));}
        }
    }
    if(mesh.Bytes()>limit)return HorizonError::Budget;
    output=std::move(mesh);tiles=unsigned(selected.size());return HorizonError::None;
}
}
