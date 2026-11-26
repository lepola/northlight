#include "world/world_wmo.h"
#include "world/world_asset_name.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace water::world
{
namespace
{
uint32_t U32(Bytes b,std::size_t at) {uint32_t v=0;b.U32(at,v);return v;}
uint16_t U16(Bytes b,std::size_t at) {uint16_t v=0;b.U16(at,v);return v;}
bool Float(Bytes b,std::size_t at,float& v) {return b.F32(at,v)&&std::isfinite(v);}
struct Parts {Bytes version,header,textures,materials,group,vertices,normals,uv,secondaryUv,indices,batches,polygons,doodadNames,doodads,doodadSets,doodadRefs,liquid;};
bool PartsOf(Bytes b,Parts& p)
{
    for(std::size_t at=0;at<b.size;) {
        if(!b.Contains(at,8)) return false;
        const auto count=U32(b,at+4);if(!b.Contains(at+8,count)) return false;
        const auto* tag=b.data+at;Bytes* target=nullptr;
        if(!std::memcmp(tag,"REVM",4)) target=&p.version;
        else if(!std::memcmp(tag,"DHOM",4)) target=&p.header;
        else if(!std::memcmp(tag,"XTOM",4)) target=&p.textures;
        else if(!std::memcmp(tag,"TMOM",4)) target=&p.materials;
        else if(!std::memcmp(tag,"PGOM",4)) target=&p.group;
        else if(!std::memcmp(tag,"TVOM",4)) target=&p.vertices;
        else if(!std::memcmp(tag,"RNOM",4)) target=&p.normals;
        // Some authored groups carry a second UV set (Icecrown citadel).
        // Keep the base set first; readers of liquid or attachments must not
        // reject the whole group because of this independent geometry stream.
        else if(!std::memcmp(tag,"VTOM",4)) target=p.uv.data?&p.secondaryUv:&p.uv;
        else if(!std::memcmp(tag,"IVOM",4)) target=&p.indices;
        else if(!std::memcmp(tag,"ABOM",4)) target=&p.batches;
        else if(!std::memcmp(tag,"YPOM",4)) target=&p.polygons;
        else if(!std::memcmp(tag,"NDOM",4)) target=&p.doodadNames;
        else if(!std::memcmp(tag,"DDOM",4)) target=&p.doodads;
        else if(!std::memcmp(tag,"SDOM",4)) target=&p.doodadSets;
        else if(!std::memcmp(tag,"RDOM",4)) target=&p.doodadRefs;
        else if(!std::memcmp(tag,"QILM",4)) target=&p.liquid;
        if(target) {if(target->data) return false;*target={b.data+at+8,count};}
        at+=8+std::size_t(count);
    }
    return true;
}
bool AssetName(Bytes b,std::size_t at,std::string_view& name)
{
    if(at>=b.size||(at&&b.data[at-1])) return false;
    const auto* end=static_cast<const uint8_t*>(std::memchr(b.data+at,0,b.size-at));
    if(!end) return false;
    name={reinterpret_cast<const char*>(b.data+at),std::size_t(end-(b.data+at))};
    if(!ValidAssetName(name)) return false;
    return true;
}
bool Name(Bytes b,std::size_t at,std::string_view& name)
{
    if(!AssetName(b,at,name)||name.size()<5)return false;
    const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;};
    return name[name.size()-4]=='.'&&lower(name[name.size()-3])=='b'&&lower(name[name.size()-2])=='l'&&lower(name.back())=='p';
}
bool ModelName(std::string_view name)
{
    if(name.size()<4)return false;
    const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;};
    const auto n=name.size();
    return (name[n-3]=='.'&&lower(name[n-2])=='m'&&name[n-1]=='2')||
        (n>=5&&name[n-4]=='.'&&lower(name[n-3])=='m'&&lower(name[n-2])=='d'&&(lower(name[n-1])=='x'||lower(name[n-1])=='l'));
}
bool Charge(std::size_t& left,std::size_t count,std::size_t size)
{if(count>left/size) return false;left-=count*size;return true;}
}
std::size_t WmoRoot::Bytes() const
{
    std::size_t n=sizeof(*this)+materials.capacity()*sizeof(WmoMaterial);
    for(const auto& m:materials)n+=m.texture.name.capacity()+1;
    return n;
}
WmoError DecodeWmoRoot(Bytes input,std::size_t limit,WmoRoot& output)
{
    Parts p;if(!PartsOf(input,p)) return WmoError::Layout;
    if(p.version.size!=4||U32(p.version,0)!=17||p.header.size!=64) return WmoError::Header;
    const auto materials=U32(p.header,0),groups=U32(p.header,4);
    if(!materials||materials>256||!groups||groups>512||p.materials.size!=materials*64||!p.textures.size) return WmoError::Layout;
    std::size_t left=limit;
    if(!Charge(left,1,sizeof(WmoRoot))||!Charge(left,materials,sizeof(WmoMaterial))) return WmoError::Budget;
    for(unsigned i=0;i<materials;++i) {
        const auto at=i*64;std::string_view name;
        if(!Name(p.textures,U32(p.materials,at+12),name))return WmoError::Material;
        // Any shader is drawn from its base texture alone; blended materials' batches are left out of the group.
        if(!Charge(left,std::max(name.size()+1,std::string().capacity()+1),1))return WmoError::Budget;
    }
    WmoRoot candidate;candidate.groups=groups;candidate.doodads=static_cast<uint32_t>(p.doodads.size/40);candidate.flags=U32(p.header,60);
    candidate.materials.resize(materials);
    for(unsigned i=0;i<materials;++i) {
        auto& m=candidate.materials[i];const auto at=i*64;std::string_view name;
        if(!Name(p.textures,U32(p.materials,at+12),name)||!CanonicalAssetName(name,m.texture.name))return WmoError::Material;
        m.flags=U32(p.materials,at);m.shader=U32(p.materials,at+4);m.blend=U32(p.materials,at+8);
        m.texture.flags=((m.flags&0x40)?0:1)|((m.flags&0x80)?0:2);
        m.nightEmission=(m.flags&0x10)?U32(p.materials,at+16)&0xffffff:0;
    }
    if(candidate.Bytes()>limit)return WmoError::Budget;
    output=std::move(candidate);return WmoError::None;
}
std::size_t WmoAttachments::Bytes() const
{
    std::size_t n=sizeof(*this)+models.capacity()*sizeof(WmoDoodad);
    for(const auto& a:models)n+=a.name.capacity()+1;
    return n;
}
WmoError DecodeWmoAttachments(Bytes input,std::size_t limit,WmoAttachments& output)
{
    Parts p;if(!PartsOf(input,p))return WmoError::Layout;
    if(p.version.size!=4||U32(p.version,0)!=17||p.header.size!=64)return WmoError::Header;
    // Native chunk spans own the tables. Patched WMOs can retain stale MOHD
    // summary counts (Booty Bay declares 1123 doodads but stores 1059).
    std::size_t left=limit;
    const auto doodads=p.doodads.size/40,sets=p.doodadSets.size/32;
    if(doodads>65536||sets>1024||p.doodads.size%40||p.doodadSets.size%32)
        return WmoError::Layout;
    if(!Charge(left,1,sizeof(WmoAttachments))||!Charge(left,doodads,sizeof(WmoDoodad)))return WmoError::Budget;
    for(unsigned i=0;i<sets;++i){const auto first=U32(p.doodadSets,i*32+20),count=U32(p.doodadSets,i*32+24);
        if(first>doodads||count>doodads-first)return WmoError::Layout;}
    for(unsigned i=0;i<doodads;++i){
        std::string_view name;
        if(!AssetName(p.doodadNames,U32(p.doodads,i*40)&0xffffff,name))return WmoError::Layout;
        if(!ModelName(name))return WmoError::Unsupported;
        if(!Charge(left,std::max(name.size()+1,std::string().capacity()+1),1))return WmoError::Budget;
        float value;double norm=0;
        for(unsigned j=0;j<8;++j){if(!Float(p.doodads,i*40+4+j*4,value))return WmoError::Geometry;
            if(j>=3&&j<7)norm+=double(value)*value;}
        if(norm<1e-12||norm>1e12||value<=0||value>100)return WmoError::Geometry;
    }
    WmoAttachments candidate;
    candidate.models.resize(doodads);
    for(unsigned i=0;i<doodads;++i){
        const auto at=i*40;auto& a=candidate.models[i];std::string_view name;
        AssetName(p.doodadNames,U32(p.doodads,at)&0xffffff,name);CanonicalAssetName(name,a.name);
        const auto extension=a.name.substr(a.name.size()-4);
        if(extension==".mdx"||extension==".mdl")a.name.replace(a.name.size()-4,4,".m2");
        a.flags=uint8_t(U32(p.doodads,at)>>24);a.colour=U32(p.doodads,at+36);
        for(unsigned j=0;j<sets;++j){const auto first=U32(p.doodadSets,j*32+20),count=U32(p.doodadSets,j*32+24);
            if(i>=first&&i-first<count){a.set=uint16_t(j);break;}}
        float q[4],scale;for(unsigned j=0;j<4;++j)Float(p.doodads,at+16+j*4,q[j]);Float(p.doodads,at+32,scale);
        const double length=std::sqrt(double(q[0])*q[0]+double(q[1])*q[1]+double(q[2])*q[2]+double(q[3])*q[3]);
        const double x=q[0]/length,y=q[1]/length,z=q[2]/length,w=q[3]/length;
        const double rotation[]={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)a.transform[r*4+c]=float(rotation[r*3+c]*scale);
        for(unsigned j=0;j<3;++j)Float(p.doodads,at+4+j*4,a.transform[12+j]);a.transform[15]=1;
    }
    if(candidate.Bytes()>limit)return WmoError::Budget;
    output=std::move(candidate);return WmoError::None;
}
WmoError DecodeWmoReferences(Bytes input,uint32_t doodadCount,std::size_t limit,std::vector<uint16_t>& output)
{
    Parts outer,p;if(!PartsOf(input,outer))return WmoError::Layout;
    if(outer.version.size!=4||U32(outer.version,0)!=17||!outer.group.Contains(0,68))return WmoError::Header;
    if(!PartsOf({outer.group.data+68,outer.group.size-68},p)||p.doodadRefs.size%2)return WmoError::Layout;
    if(p.doodadRefs.size>limit)return WmoError::Budget;
    for(std::size_t i=0;i<p.doodadRefs.size;i+=2)if(U16(p.doodadRefs,i)>=doodadCount)return WmoError::Layout;
    std::vector<uint16_t> candidate(p.doodadRefs.size/2);
    for(std::size_t i=0;i<candidate.size();++i)candidate[i]=U16(p.doodadRefs,i*2);
    if(candidate.capacity()*sizeof(uint16_t)>limit)return WmoError::Budget;
    output=std::move(candidate);return WmoError::None;
}
bool WmoLiquid::Quad(unsigned x,unsigned y,std::array<std::array<double,3>,4>& points) const
{
    if(x>=width||y>=height||(tiles.data[y*width+x]&15)==15)return false;
    constexpr double step=1600.0/3/128;
    for(unsigned i=0;i<4;++i){
        const unsigned cx=x+(i&1),cy=y+(i>>1);float z;
        if(!Float(vertices,(cy*(width+1)+cx)*8+4,z))return false;
        points[i]={origin[0]+cx*step,origin[1]+cy*step,z};
    }
    return true;
}
// Use only wet cells of this group. An attachment above a dry hole in a large
// liquid grid need not load merely because the group's bounding box contains it.
bool WmoLiquid::NearSurface(const float* point,double reach) const
{
    constexpr double step=1600.0/3/128;
    if(!width || !height)return false;
    const auto cell=[&](double coordinate,double start,unsigned limit){
        return unsigned(std::clamp(std::floor((coordinate-start)/step),0.0,double(limit-1)));
    };
    for(unsigned y=cell(point[1]-reach,origin[1],height);y<=cell(point[1]+reach,origin[1],height);++y)
        for(unsigned x=cell(point[0]-reach,origin[0],width);x<=cell(point[0]+reach,origin[0],width);++x){
            std::array<std::array<double,3>,4> corners;
            if(!Quad(x,y,corners))continue;
            double low=corners[0][2],high=low;
            for(const auto& c:corners){low=std::min(low,c[2]);high=std::max(high,c[2]);}
            const double dx=point[0]-std::clamp(double(point[0]),corners[0][0],corners[3][0]);
            const double dy=point[1]-std::clamp(double(point[1]),corners[0][1],corners[3][1]);
            const double dz=point[2]-std::clamp(double(point[2]),low,high);
            if(dx*dx+dy*dy+dz*dz<=reach*reach)return true;
        }
    return false;
}
WmoError DecodeWmoLiquid(Bytes input,WmoLiquid& output)
{
    Parts outer,p;
    if(!PartsOf(input,outer))return WmoError::Layout;
    if(outer.version.size!=4||U32(outer.version,0)!=17||!outer.group.Contains(0,68))return WmoError::Header;
    if(!PartsOf({outer.group.data+68,outer.group.size-68},p))return WmoError::Layout;
    WmoLiquid candidate;
    if(p.liquid.data){
        const auto b=p.liquid;
        if(!b.Contains(0,30))return WmoError::Layout;
        const unsigned vx=U32(b,0),vy=U32(b,4),w=U32(b,8),h=U32(b,12);
        if(!w||!h||w>255||h>255||vx!=w+1||vy!=h+1||b.size!=30+std::size_t(vx)*vy*8+std::size_t(w)*h)
            return WmoError::Layout;
        for(unsigned c=0;c<3;++c)if(!Float(b,16+c*4,candidate.origin[c]))return WmoError::Geometry;
        const auto type=U32(outer.group,52);
        if(type>65535)return WmoError::Layout;
        candidate.width=w;candidate.height=h;candidate.type=static_cast<uint16_t>(type);
        candidate.vertices={b.data+30,std::size_t(vx)*vy*8};
        candidate.tiles={b.data+30+std::size_t(vx)*vy*8,std::size_t(w)*h};
        // Unused grid vertices may be uninitialized. Only drawn cells must have
        // finite heights; no dry cell becomes a receiving water surface.
        std::array<std::array<double,3>,4> points;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
            if((candidate.tiles.data[y*w+x]&15)!=15&&!candidate.Quad(x,y,points))return WmoError::Geometry;
    }
    output=candidate;return WmoError::None;
}
bool WmoGroupOnlyIndoors(Bytes input)
{
    Parts outer;if(!PartsOf(input,outer)||outer.group.size<68)return false;
    const auto flags=U32(outer.group,8);
    return (flags&0x2000)&&!(flags&0x8);
}
WmoError DecodeWmoGroup(Bytes input,const WmoRoot& root,std::size_t limit,StaticModel& output)
{
    Parts outer,p;if(!PartsOf(input,outer))return WmoError::Layout;
    if(outer.version.size!=4||U32(outer.version,0)!=17||!outer.group.Contains(0,68))return WmoError::Header;
    if(!PartsOf({outer.group.data+68,outer.group.size-68},p))return WmoError::Layout;
    const auto vertices=p.vertices.size/12,indices=p.indices.size/2,batches=p.batches.size/24;
    if(!vertices||vertices>65536||p.vertices.size%12||p.normals.size!=p.vertices.size||p.uv.size!=vertices*8||(p.secondaryUv.data&&p.secondaryUv.size!=vertices*8)||
       !indices||p.indices.size%6||p.polygons.size!=indices/3*2||p.batches.size%24||!batches||batches>65536||root.materials.empty()||root.materials.size()>256)return WmoError::Layout;
    std::size_t left=limit;
    if(!Charge(left,1,sizeof(StaticModel))||!Charge(left,vertices,sizeof(ModelVertex))||!Charge(left,indices,2)||
       !Charge(left,batches,sizeof(ModelDraw))||!Charge(left,root.materials.size(),sizeof(ModelTexture)))return WmoError::Budget;
    for(const auto& material:root.materials)if(!Charge(left,std::max(material.texture.name.size()+1,std::string().capacity()+1),1))return WmoError::Budget;
    // Validate the actual MOBA geometry before allocating output. Extra MOVI
    // collision geometry is not promoted into a render batch.
    for(std::size_t i=0;i<vertices;++i) {
        float value;double n2=0;
        for(unsigned a=0;a<3;++a){if(!Float(p.vertices,i*12+a*4,value)||!Float(p.normals,i*12+a*4,value))return WmoError::Geometry;n2+=double(value)*value;}
        for(unsigned a=0;a<2;++a)if(!Float(p.uv,i*8+a*4,value))return WmoError::Geometry;
        if(n2<.25||n2>2.25)return WmoError::Geometry;
    }
    for(std::size_t i=0;i<indices;++i)if(U16(p.indices,i*2)>=vertices)return WmoError::Geometry;
    for(std::size_t i=0;i<batches;++i) {
        const auto at=i*24;const auto first=U32(p.batches,at+12);const auto count=U16(p.batches,at+16),lo=U16(p.batches,at+18),hi=U16(p.batches,at+20);
        if(p.batches.data[at+22]||p.batches.data[at+23]>=root.materials.size())return WmoError::Unsupported;
        if(!count||count%3||first%3||first>indices||count>indices-first||lo>hi||hi>=vertices)return WmoError::Geometry;
        for(std::size_t j=first;j<first+count;++j)if(U16(p.indices,j*2)<lo||U16(p.indices,j*2)>hi)return WmoError::Geometry;
    }
    StaticModel candidate;candidate.vertices.resize(vertices);candidate.indices.resize(indices);candidate.draws.resize(batches);candidate.textures.reserve(root.materials.size());
    for(const auto& m:root.materials)candidate.textures.push_back(m.texture);
    for(std::size_t i=0;i<vertices;++i) {
        auto& v=candidate.vertices[i];
        for(unsigned a=0;a<3;++a){Float(p.vertices,i*12+a*4,v.position[a]);Float(p.normals,i*12+a*4,v.normal[a]);
            if(!i)candidate.minimum[a]=candidate.maximum[a]=v.position[a];else {candidate.minimum[a]=std::min(candidate.minimum[a],v.position[a]);candidate.maximum[a]=std::max(candidate.maximum[a],v.position[a]);}}
        for(unsigned a=0;a<2;++a)Float(p.uv,i*8+a*4,v.uv[a]);
    }
    for(std::size_t i=0;i<indices;++i)candidate.indices[i]=U16(p.indices,i*2);
    for(std::size_t i=0;i<batches;++i) {
        const auto at=i*24;auto& d=candidate.draws[i];d.firstIndex=U32(p.batches,at+12);d.indexCount=U16(p.batches,at+16);d.texture=p.batches.data[at+23];
        const auto& material=root.materials[d.texture];d.blend=static_cast<uint16_t>(material.blend);d.materialFlags=static_cast<uint16_t>(material.flags&7);d.nightEmission=material.nightEmission;
    }
    candidate.draws.erase(std::remove_if(candidate.draws.begin(),candidate.draws.end(),[](const ModelDraw& d){return d.blend>1;}),candidate.draws.end());
    if(candidate.Bytes()>limit)return WmoError::Budget;
    output=std::move(candidate);return WmoError::None;
}
}
