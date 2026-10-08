#pragma once
// Portable asset-derived regional fog. All cache I/O and geometry rasterization
// belong on the worker. No game pointers, weather guesses, or camera-floor ray.
#include "world_gi.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
namespace NorthlightRegionalFog {
constexpr unsigned N=64;
constexpr float Spacing=8.f;
constexpr double WorldZero=17066.666666666668,TileSize=533.3333333333334,ChunkSize=TileSize/16,Unit=ChunkSize/8;
struct Water {float x=0,y=0,height=0;uint64_t mask=0;};
struct Indoor {float low[3]={},high[3]={};};
struct Tile {int x=0,y=0;std::array<uint32_t,256> zones{};std::vector<Water> water;std::vector<Indoor> indoors;};
struct Region {std::vector<Tile> tiles;unsigned missing=0,invalid=0;};
struct Texel {float ground=0,day=0,nightExtra=0,height=0;};
static_assert(sizeof(Texel)==16,"Fog texture float4");
struct Field {float originX=0,originY=0;std::array<Texel,N*N> texels{};std::array<uint8_t,N*N> lush{}; /* 0.3.199 (fog clouds): 1 = outdoor forest/grass zone (lushZone), CPU only */unsigned groundCells=0,fogCells=0,airCells=0,indoorCells=0,citySurfaceCells=0;};
inline float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
// Artist policy transitions, applied to the VERIFIED game's normalized render
// time (minute-of-day / 1440, including fixed-time map override). Unknown clock
// adds no night enhancement. Never use the host clock or an invented orbit.
inline float nightFactor(float gameDayFraction){
    if(!std::isfinite(gameDayFraction)||gameDayFraction<0||gameDayFraction>=1)return 0;
    float hour=gameDayFraction*24;
    if(hour<6)return 1;if(hour<8)return 1-smooth((hour-6)*.5f);
    if(hour<18)return 0;if(hour<20)return smooth((hour-18)*.5f);return 1;
}
inline bool loadTile(const std::string& path,int x,int y,Tile& out){
    FILE* file=std::fopen(path.c_str(),"rb");if(!file)return false;
    struct Close {FILE* f;~Close(){std::fclose(f);}} close{file};
    if(std::fseek(file,0,SEEK_END))return false;long bytes=std::ftell(file);
    if(bytes<1048||bytes>2*1024*1024||std::fseek(file,0,SEEK_SET))return false;
    uint32_t h[6]={};if(std::fread(h,4,6,file)!=6||h[0]!=0x31465246u||h[1]!=1||h[2]!=unsigned(x)||h[3]!=unsigned(y)||h[4]>4096||h[5]>8192||uint64_t(bytes)!=1048ull+20ull*h[4]+24ull*h[5])return false;
    Tile t;t.x=x;t.y=y;if(std::fread(t.zones.data(),4,256,file)!=256)return false;
    t.water.resize(h[4]);t.indoors.resize(h[5]);
    for(auto& w:t.water){if(std::fread(&w.x,4,3,file)!=3||std::fread(&w.mask,8,1,file)!=1)return false;
        if(!std::isfinite(w.x)||!std::isfinite(w.y)||!std::isfinite(w.height)||std::fabs(w.x)>100000||std::fabs(w.y)>100000||std::fabs(w.height)>100000)return false;}
    for(auto& b:t.indoors){if(std::fread(b.low,4,3,file)!=3||std::fread(b.high,4,3,file)!=3)return false;
        for(unsigned k=0;k<3;++k)if(!std::isfinite(b.low[k])||!std::isfinite(b.high[k])||b.low[k]>b.high[k]||std::fabs(b.low[k])>100000||std::fabs(b.high[k])>100000)return false;}
    out=std::move(t);return true;
}
// cacheDirectory is CLIENT/world-cache/fog. Unknown maps return an empty region
// and therefore exactly zero added fog. Only a bounded 3x3 neighborhood is read.
inline Region loadRegion(const std::string& cacheDirectory,const std::string& map,float x,float y){
    Region r;if((map!="Azeroth"&&map!="Kalimdor"&&map!="Expansion01"&&map!="Northrend")||!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>100000||std::fabs(y)>100000)return r;
    int tx=int(std::floor((WorldZero-y)/TileSize)),ty=int(std::floor((WorldZero-x)/TileSize));
    for(int yy=ty-1;yy<=ty+1;++yy)for(int xx=tx-1;xx<=tx+1;++xx){
        std::string path=cacheDirectory+"/"+map+"/"+std::to_string(xx)+"_"+std::to_string(yy)+".frf";Tile tile;
        if(loadTile(path,xx,yy,tile))r.tiles.push_back(std::move(tile));else ++r.missing;
    }
    return r;
}
inline uint32_t zoneAt(const Region& region,float x,float y){
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>100000||std::fabs(y)>100000)return 0;
    int gx=int(std::floor((WorldZero-y)/ChunkSize)),gy=int(std::floor((WorldZero-x)/ChunkSize));
    int tx=int(std::floor(double(gx)/16)),ty=int(std::floor(double(gy)/16));
    unsigned ix=unsigned(gx-tx*16),iy=unsigned(gy-ty*16);
    for(const auto& tile:region.tiles)if(tile.x==tx&&tile.y==ty)return tile.zones[iy*16+ix];return 0;
}
inline bool indoorColumn(const Region& region,float x,float y,float ground){
    // Inflate each authored indoor GROUP footprint by one sample spacing. This
    // makes all four bilinear nodes zero inside that group; roots/foliage never
    // exclude fog. An underground group ending below the outdoor ground must
    // not erase the atmosphere above it. Keep two units of vertical margin;
    // overhead roofs and nearby exterior strips remain conservative exclusions.
    // This is NOT exact portal containment.
    for(const auto& tile:region.tiles)for(const auto& b:tile.indoors)
        if(ground<=b.high[2]+2.f&&x>=b.low[0]-Spacing&&x<=b.high[0]+Spacing&&y>=b.low[1]-Spacing&&y<=b.high[1]+Spacing)return true;
    return false;
}
inline float wetness(const Region& region,float x,float y,float ground,float& surface){
    float best=24.f*24.f;surface=ground;
    for(const auto& tile:region.tiles)for(const auto& w:tile.water){
        if(std::fabs(ground-w.height)>8)continue;
        float dx=std::max({float(w.x-ChunkSize)-x,0.f,x-w.x}),dy=std::max({float(w.y-ChunkSize)-y,0.f,y-w.y});
        if(dx*dx+dy*dy>=best)continue;
        for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col)if(w.mask&(uint64_t(1)<<(row*8+col))){
            float highX=float(w.x-row*Unit),lowX=float(w.x-(row+1)*Unit),highY=float(w.y-col*Unit),lowY=float(w.y-(col+1)*Unit);
            float a=std::max({lowX-x,0.f,x-highX}),b=std::max({lowY-y,0.f,y-highY});float distance=a*a+b*b;
            if(distance<best){best=distance;if(distance==0)surface=std::max(ground,w.height);}
        }
    }
    return 1-smooth(std::sqrt(best)/24.f);
}
inline bool forestZone(uint32_t zone){
    switch(zone){
#define FOREST_REGION(id,name) case id:
#include "forest_regions.inc"
#undef FOREST_REGION
        return true;
        default:return false;
    }
}
// 0.3.199 (fog clouds): grass zones (root AreaTable ids, checked against the 3.3.5a client's AreaTable). With the forests, Duskwood, STV,
// Mulgore and Stormwind they are the lush zones that get fog banks without rain; deserts, barrens, badlands and the like get them only in rain.
// Runtime only: no world-cache input, no regional fog density change.
inline bool grassZone(uint32_t zone){
    switch(zone){
        case 40:   /* Westfall */
        case 44:   /* Redridge Mountains */
        case 45:   /* Arathi Highlands */
        case 267:  /* Hillsbrad Foothills */
        case 38:   /* Loch Modan */
        case 28:   /* Western Plaguelands */
        case 139:  /* Eastern Plaguelands */
        case 36:   /* Alterac Mountains */
        case 3518: /* Nagrand */
        case 3537: /* Borean Tundra */
            return true;
        default:return false;
    }
}
inline bool lushZone(uint32_t zone){return zone==10||zone==33||zone==215||zone==1519||forestZone(zone)||grassZone(zone);}
inline Texel policy(uint32_t zone,float ground,float wet,float basin,bool indoors){
    Texel out;out.ground=ground;
    if(indoors||!std::isfinite(ground))return out;
    wet=std::clamp(wet,0.f,1.f);basin=std::clamp(basin,0.f,1.f);
    // Explicit artistic extinction policy; authored area/terrain/water select
    // where it exists. It is not claimed to reproduce Blizzard weather density.
    if(zone==10){out.day=.0007f+.0003f*basin+.0004f*wet;out.nightExtra=.003f+.015f*wet+.008f*basin;out.height=5.f;}
    else if(zone==33){out.day=.00055f*wet+.00008f*basin;out.nightExtra=.0045f+.025f*wet+.012f*basin;out.height=2.5f;}
    // Mulgore: prairie haze, 60% of the forest air increment. Reuse the
    // existing continuous .625..1.25 atmosphere ramp: sigma .0038 by day,
    // .00566 at night, before shared height scaling above local terrain. No opaque
    // ground blanket, extra ray samples, or changes to neighbouring zones.
    else if(zone==215){out.height=1.f;}
    // Stormwind and Orgrimmar outdoor air: 85% of the forest increment, plus shared haze.
    // Day sigma .004675 vs Elwynn .0052 (~90%). Follow the existing night
    // air transition, but do not add the forest's wet/basin ground blanket.
    else if(zone==1519||zone==1637){out.height=1.15625f;}
    else if(forestZone(zone)){out.nightExtra=.0036f+.024f*wet+.0144f*basin;out.height=1.25f;}
    // General outdoors: half the forest air increment. Day .00345 / night
    // .005, about 91% / 88% of Mulgore. No added ground blanket.
    else if(zone!=0){out.height=.9375f;}
    // 0.3.60: modest night-only increase across Duskwood, STV and all forests.
    out.nightExtra*=1.2f;
    // Positive height also identifies the supported forest atmosphere profile.
    // Daytime ground density remains zero on dry STV uplands; a separate, sparse
    // canopy air layer is evaluated by WorldFog. Generic forests use tag1.25
    // and have no added daytime fog; the shader expands their shallow layer
    // smoothly to six units at night. Indoors return height zero.
    // Other known zones use tag .9375 for air; missing metadata stays zero.
    return out;
}
inline bool cityZone(uint32_t zone){return zone==1519||zone==1637;}
// Match the geometry sampler's bilinear alpha test. A cutout in a city roof
// must not raise the fog floor into empty space. Runtime FGS3 defaults wrap.
inline bool cityOpaqueAt(const NorthlightGI::WorldMaterial& m,float u,float v){
    if(m.rgba.empty()||m.alphaCutoff<=0)return true;
    if(!m.width||!m.height||m.rgba.size()!=size_t(m.width)*m.height*4||!std::isfinite(u)||!std::isfinite(v))return false;
    auto coord=[](float p,uint32_t mode){if(mode==3)return std::clamp(p,0.f,1.f);if(mode==2){float q=p-2*std::floor(p*.5f);return q<=1?q:2-q;}return p-std::floor(p);};
    auto texel=[](int i,unsigned size,uint32_t mode){return mode==1?unsigned((i+int(size))%int(size)):unsigned(std::clamp(i,0,int(size)-1));};
    float x=coord(u,m.addressU)*m.width-.5f,y=coord(v,m.addressV)*m.height-.5f;
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-std::floor(x),fy=y-std::floor(y),alpha=0;
    for(int j=0;j<2;++j)for(int i=0;i<2;++i)alpha+=m.rgba[(size_t(texel(iy+j,m.height,m.addressV))*m.width+texel(ix+i,m.width,m.addressU))*4+3]/255.f*(i?fx:1-fx)*(j?fy:1-fy);
    return alpha>=m.alphaCutoff;
}
// originX/Y are the WORLD POSITION OF TEXEL(0,0)'S CENTER. Shader UV is
// (worldXY-originXY)/(N*Spacing)+0.5/N. Two outer sample rows fade to zero.
inline Field buildField(const NorthlightGI::WorldScene& scene,const Region& region,float centerX,float centerY){
    Field out;if(!std::isfinite(centerX)||!std::isfinite(centerY)||std::fabs(centerX)>100000||std::fabs(centerY)>100000)return out;
    out.originX=std::floor(centerX/Spacing)*Spacing-float(N/2)*Spacing;
    out.originY=std::floor(centerY/Spacing)*Spacing-float(N/2)*Spacing;
    std::array<bool,N*N> city{};bool hasCity=false,hasWmo=false;
    for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){city[y*N+x]=cityZone(zoneAt(region,out.originX+x*Spacing,out.originY+y*Spacing));hasCity|=city[y*N+x];}
    if(hasCity)for(const auto& material:scene.materials)hasWmo|=material.wmo;
    std::array<float,N*N> ground;ground.fill(-std::numeric_limits<float>::infinity());
    for(const auto& tri:scene.triangles){
        if(tri.material>=scene.materials.size()||tri.v0>=scene.vertices.size()||tri.v1>=scene.vertices.size()||tri.v2>=scene.vertices.size())continue;
        const auto& material=scene.materials[tri.material];
        const bool cityWmo=hasCity&&material.wmo&&!material.terrain;
        if(!material.terrain&&!cityWmo)continue;
        const auto a=scene.vertices[tri.v0].position,b=scene.vertices[tri.v1].position,c=scene.vertices[tri.v2].position;
        double denominator=double(b.y-c.y)*(a.x-c.x)+double(c.x-b.x)*(a.y-c.y);
        if(!std::isfinite(denominator)||std::fabs(denominator)<1e-10)continue;
        float loX=std::min({a.x,b.x,c.x}),hiX=std::max({a.x,b.x,c.x}),loY=std::min({a.y,b.y,c.y}),hiY=std::max({a.y,b.y,c.y});
        if(hiX<out.originX||hiY<out.originY||loX>out.originX+(N-1)*Spacing||loY>out.originY+(N-1)*Spacing)continue;
        int x0=int(std::max(0.,std::ceil((double(loX)-out.originX)/Spacing))),x1=int(std::min(double(N-1),std::floor((double(hiX)-out.originX)/Spacing)));
        int y0=int(std::max(0.,std::ceil((double(loY)-out.originY)/Spacing))),y1=int(std::min(double(N-1),std::floor((double(hiY)-out.originY)/Spacing)));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            if(cityWmo&&!city[y*N+x])continue;
            double px=out.originX+x*Spacing,py=out.originY+y*Spacing;
            double u=((b.y-c.y)*(px-c.x)+(c.x-b.x)*(py-c.y))/denominator;
            double v=((c.y-a.y)*(px-c.x)+(a.x-c.x)*(py-c.y))/denominator,w=1-u-v;
            if(u<-.00001||v<-.00001||w<-.00001)continue;float z=float(u*a.z+v*b.z+w*c.z);
            if(cityWmo){
                const auto& va=scene.vertices[tri.v0];const auto& vb=scene.vertices[tri.v1];const auto& vc=scene.vertices[tri.v2];
                if(!cityOpaqueAt(material,float(u*va.u+v*vb.u+w*vc.u),float(u*va.v+v*vb.v+w*vc.v)))continue;
            }
            if(std::isfinite(z))ground[y*N+x]=std::max(ground[y*N+x],z);
        }
    }
    for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        unsigned i=y*N+x;if(!std::isfinite(ground[i]))continue;++out.groundCells;
        // City WMOs enclose whole open streets in their indoor AABBs. The top
        // opaque terrain/WMO envelope instead starts air at the street or roof.
        // Below a roof the shader's altitude test is negative, so rooms stay dry.
        // M2 trees/doodads never define this envelope. No camera-height test.
        bool citySurface=city[i]&&hasWmo;if(citySurface)++out.citySurfaceCells;
        float px=out.originX+x*Spacing,py=out.originY+y*Spacing;bool indoors=!citySurface&&indoorColumn(region,px,py,ground[i]);if(indoors)++out.indoorCells;
        float surrounding=0;unsigned count=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int xx=int(x)+dx,yy=int(y)+dy;if((dx||dy)&&xx>=0&&xx<int(N)&&yy>=0&&yy<int(N)&&std::isfinite(ground[yy*N+xx])){surrounding+=ground[yy*N+xx];++count;}}
        float basin=count?smooth((surrounding/count-ground[i]-.25f)/2.f):0;
        float surface=ground[i],wet=wetness(region,px,py,ground[i],surface);
        const uint32_t zone=zoneAt(region,px,py);
        auto cell=policy(zone,surface,wet,basin,indoors);out.lush[i]=!indoors&&lushZone(zone);
        float edge=smooth(float(std::min({x,y,N-1-x,N-1-y}))/2.f);cell.day*=edge;cell.nightExtra*=edge;
        out.texels[i]=cell;if(cell.day>0||cell.nightExtra>0)++out.fogCells;
        if(cell.height>0&&edge>0)++out.airCells;
    }
    return out;
}
} // namespace NorthlightRegionalFog
