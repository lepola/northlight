#include "utf8_path.h"
#include "world_gi.h"
#include "material_byte_ops.h"
#include "probe_moment_pack.h"
#include "gi_trig.h"
#include "mesh_page_key.h"
#include "backend_loader.h" // shared SHA-256 implementation for FGL source identity
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <chrono>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightGI {
static constexpr float PI = 3.14159265358979323846f;
// 0.3.138 tracer switches. Each one keeps every RayHit and Probe bit-identical
// to the 0.3.137 tracer (same traversal order, arithmetic and tie precedence).
static constexpr bool TraceCarryEntry=true;   /* reuse the child slab entry instead of re-testing popped boxes */
static constexpr bool TraceDeferShading=true; /* texture color and normals only for the final closest hit */
static constexpr bool TraceOpaqueSkip=true;   /* no alpha fetch where no bilinear sample can fall below the cutoff */
static constexpr bool TracePairNodes=true;    /* both child boxes in one record, two-box SSE2 slab test */
// Faster tree (BVH::build fast=true, northlight-quality GIFastBVH=1), NOT
// bit-identical: binned SAH split, reciprocal-direction slab
// test on boxes inflated by 2^-20 relative (+2^-20 absolute), and the closest
// hit among equal distances is the lowest triangle index. The result is then
// independent of tree shape and traversal order; it can differ from 0.3.137
// only where two triangles have exactly equal hit distance (0.3.137 kept the
// first one visited) or where 0.3.137's exact box test culled a grazing hit.
static constexpr bool TraceSah=true;          /* allow the fast tree; false forces the exact median tree everywhere */
static constexpr float TraceSahPairCost=2.f,TraceSahPairsPerTriangle=.32f;
static constexpr uint32_t TraceSahAllAxesBelow=1024;
static_assert(!TraceSah||TracePairNodes,"the SAH tree is stored as pair nodes");
/* Conservative box inflation, far above slab/intersection rounding (~2^-22). */
static void inflate(Vec3& low,Vec3& high){
    auto pad=[](float x){return (std::fabs(x)+1.f)*(1.f/1048576);};
    low=Vec3(low.x-pad(low.x),low.y-pad(low.y),low.z-pad(low.z));high=Vec3(high.x+pad(high.x),high.y+pad(high.y),high.z+pad(high.z));
}
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 operator-(Vec3 a) { return {-a.x,-a.y,-a.z}; }
Vec3 operator*(Vec3 a, float b) { return {a.x*b,a.y*b,a.z*b}; }
Vec3 operator*(float b, Vec3 a) { return a*b; }
Vec3 operator*(Vec3 a, Vec3 b) { return {a.x*b.x,a.y*b.y,a.z*b.z}; }
Vec3 operator/(Vec3 a, float b) { return a*(1.f/b); }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 normalized(Vec3 a) { float q=dot(a,a); return q>1e-20f && std::isfinite(q) ? a/std::sqrt(q) : Vec3(); }
static bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
static float component(Vec3 a,unsigned i) { return i==0?a.x:i==1?a.y:a.z; }
static Vec3 minimum(Vec3 a,Vec3 b) {return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
static Vec3 maximum(Vec3 a,Vec3 b) {return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
static float clamp01(float a) {return std::max(0.f,std::min(1.f,a));}
static Vec3 reflectance(Vec3 a) {return {clamp01(a.x),clamp01(a.y),clamp01(a.z)};}
static Vec3 nonnegative(Vec3 a) {return {std::max(0.f,a.x),std::max(0.f,a.y),std::max(0.f,a.z)};}

namespace {
struct Reader {
    FILE* file=nullptr;
    uint64_t remaining=0;
    ~Reader(){if(file)std::fclose(file);}
    bool bytes(void* p,size_t n) {
        if(n>remaining || (n && std::fread(p,1,n,file)!=n)) return false;
        remaining-=n; return true;
    }
    bool u32(uint32_t& v) {
        uint8_t b[4]; if(!bytes(b,4))return false;
        v=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;
    }
    bool u64(uint64_t& v) {uint32_t a,b;if(!u32(a)||!u32(b))return false;v=uint64_t(a)|(uint64_t(b)<<32);return true;}
    bool f32(float& v) {uint32_t u; if(!u32(u))return false; std::memcpy(&v,&u,4);return std::isfinite(v);}
    bool vec(Vec3& v){return f32(v.x)&&f32(v.y)&&f32(v.z);}
};
}

static bool readScene(const char* path,WorldScene& output,std::string& error,const AllocationAdmission& admit,bool materialsOnly,bool immutableTexels=false) {
    error.clear(); Reader r; r.file=NorthlightFiles::open(path,"rb");
    if(!r.file){error="Cannot open FGS2 scene";return false;}
    if(std::fseek(r.file,0,SEEK_END)!=0){error="Cannot seek FGS2 scene";return false;}
    long fileSize=std::ftell(r.file);
    if(fileSize<20 || fileSize>512L*1024L*1024L || std::fseek(r.file,0,SEEK_SET)!=0){error="Invalid FGS2 file size (limit 512 MiB)";return false;}
    r.remaining=uint64_t(fileSize);
    char magic[4];uint32_t version=0,nv=0,nt=0,nm=0;
    if(!r.bytes(magic,4)||std::memcmp(magic,"FGS2",4)||!r.u32(version)||version!=2||!r.u32(nv)||!r.u32(nt)||!r.u32(nm)){
        error="Invalid FGS2 header/version";return false;
    }
    if(nv>8000000||nt>12000000||nm>65536||uint64_t(nv)*32+uint64_t(nt)*16+uint64_t(nm)*28>r.remaining){
        error="FGS2 count limit or truncated data";return false;
    }
    WorldScene s;
    try {
        // Pressure decoding must not leave a source-sized CRT arena behind.
        if(immutableTexels){s.vertices.chunks(&admit);s.triangles.chunks(&admit);}
        auto allowed=[&](uint64_t bytes){if(!admit||admit(bytes))return true;error="Geometry allocation deferred";return false;};
        if(!materialsOnly){
            auto resizeGeometry=[&](auto& values,size_t count){
                using T=typename std::decay_t<decltype(values)>::value_type;
                if(!allowed(uint64_t(count)*sizeof(T))){values.chunks(&admit);error.clear();}
                try{values.resize(count);}catch(const std::bad_alloc&){if(values.chunked())throw;values.chunks(&admit);values.resize(count);}
            };
            resizeGeometry(s.vertices,nv);resizeGeometry(s.triangles,nt);
        }
        if(!allowed(uint64_t(nm)*sizeof(WorldMaterial)))return false;s.materials.resize(nm);
    }
    catch(...) {error="Cannot allocate FGS2 scene";return false;}
    if(materialsOnly){
        const uint64_t geometryBytes=uint64_t(nv)*32+uint64_t(nt)*16;
        if(geometryBytes>r.remaining||std::fseek(r.file,long(geometryBytes),SEEK_CUR)!=0){error="Cannot seek cached FGS2 materials";return false;}
        r.remaining-=geometryBytes;
    }
    const uint16_t endian=1;
    if(*reinterpret_cast<const uint8_t*>(&endian)==1&&sizeof(WorldVertex)==32&&sizeof(WorldTriangle)==16){
        // FGS2 matches these plain little-endian records. Read once, then retain
        // every validation performed by the scalar portable decoder below.
        static_assert(std::is_trivially_copyable<WorldVertex>::value&&std::is_trivially_copyable<WorldTriangle>::value,"FGS2 bulk records must be plain values");
        auto readGeometry=[&](auto& values){using Buffer=std::decay_t<decltype(values)>;using T=typename Buffer::value_type;
            for(size_t offset=0;offset<values.size();){const size_t count=std::min(values.size()-offset,values.chunked()?Buffer::blockSize():values.size());
                if(!r.bytes(&values[offset],count*sizeof(T)))return false;offset+=count;}return true;};
        if(!readGeometry(s.vertices)||!readGeometry(s.triangles)){error="Invalid/truncated FGS2 geometry";return false;}
        for(const auto& v:s.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)){error="Invalid/truncated FGS2 vertex";return false;}
        for(const auto& t:s.triangles)if(t.v0>=nv||t.v1>=nv||t.v2>=nv||t.material>=nm){error="Invalid/truncated FGS2 triangle";return false;}
    }else{
    for(auto& v:s.vertices)if(!r.vec(v.position)||!r.vec(v.normal)||!r.f32(v.u)||!r.f32(v.v)){
        error="Invalid/truncated FGS2 vertex";return false;
    }
    for(auto& t:s.triangles)if(!r.u32(t.v0)||!r.u32(t.v1)||!r.u32(t.v2)||!r.u32(t.material)||t.v0>=nv||t.v1>=nv||t.v2>=nv||t.material>=nm){
        error="Invalid/truncated FGS2 triangle";return false;
    }
    }
    for(auto& m:s.materials){
        uint32_t bytes=0;
        if(!r.vec(m.albedo)||!r.u32(m.width)||!r.u32(m.height)||!r.f32(m.alphaCutoff)||!r.u32(bytes)||
           m.width>4096||m.height>4096||uint64_t(m.width)*m.height*4!=bytes||bytes>r.remaining||
           ((m.width==0)!=(m.height==0))){
            error="Invalid/truncated FGS2 material";return false;
        }
        if(m.alphaCutoff<=-1.f&&m.alphaCutoff>=-8388607.f){
            const uint32_t tag=uint32_t(-m.alphaCutoff);
            if(float(tag)==-m.alphaCutoff){m.lampLocalId=tag;m.alphaCutoff=0.f;}
        }
        uint32_t cutoffBits;std::memcpy(&cutoffBits,&m.alphaCutoff,4);
        if(((cutoffBits&0x80000000u)&&(cutoffBits&0x7fffffffu))||m.alphaCutoff>1){error="Invalid/truncated FGS2 material";return false;}
        if(admit&&!admit(bytes)){error="Geometry allocation deferred";return false;}
        try {
            if(immutableTexels&&bytes){auto pixels=geometryBlock<uint8_t>(bytes);
                if(!r.bytes(pixels.get(),bytes)){error="Truncated FGS2 texture";return false;}
                m.sharedTexels=std::shared_ptr<const uint8_t>(pixels.release(),GeometryBlockDelete<uint8_t>{});m.sharedTexelBytes=bytes;
            }else{m.rgba.resize(bytes);if(!r.bytes(m.rgba.data(),bytes)){error="Truncated FGS2 texture";return false;}}
        }catch(...){error="Cannot allocate FGS2 texture";return false;}
        m.albedo=reflectance(m.albedo);
    }
    if(r.remaining){error="Unexpected trailing FGS2 data";return false;}
    s.vertices.detachAdmission();s.triangles.detachAdmission();output=std::move(s);return true;
}

bool loadScene(const char* path,WorldScene& output,std::string& error,const AllocationAdmission& admit){return readScene(path,output,error,admit,false);}

bool cropScene(WorldScene& s,Vec3 low,Vec3 high,std::string& error){
    error.clear();
    if(!finite(low)||!finite(high)||low.x>high.x||low.y>high.y||low.z>high.z){error="Invalid crop bounds";return false;}
    WorldScene result;
    std::vector<uint32_t> vertices,materials,materialSources;
    try{
        vertices.resize(s.vertices.size(),UINT32_MAX);materials.resize(s.materials.size(),UINT32_MAX);
        for(const auto& t:s.triangles){
            if(t.v0>=s.vertices.size()||t.v1>=s.vertices.size()||t.v2>=s.vertices.size()||t.material>=s.materials.size()){
                error="Invalid triangle in crop input";return false;
            }
            Vec3 a=s.vertices[t.v0].position,b=s.vertices[t.v1].position,c=s.vertices[t.v2].position;
            Vec3 tl=minimum(a,minimum(b,c)),th=maximum(a,maximum(b,c));
            if(th.x<low.x||th.y<low.y||th.z<low.z||tl.x>high.x||tl.y>high.y||tl.z>high.z)continue;
            WorldTriangle copy=t;uint32_t* ids[3]={&copy.v0,&copy.v1,&copy.v2};
            for(auto p:ids){uint32_t old=*p;if(vertices[old]==UINT32_MAX){vertices[old]=uint32_t(result.vertices.size());result.vertices.push_back(s.vertices[old]);}*p=vertices[old];}
            if(materials[t.material]==UINT32_MAX){materials[t.material]=uint32_t(materialSources.size());materialSources.push_back(t.material);}
            copy.material=materials[t.material];result.triangles.push_back(copy);
        }
        result.materials.resize(materialSources.size());
    }catch(...){error="Cannot allocate cropped geometry";return false;}
    // No allocations remain: mutation starts only after all validation succeeds.
    for(size_t i=0;i<materialSources.size();++i)result.materials[i]=std::move(s.materials[materialSources[i]]);
    s=std::move(result);return true;
}

namespace {
struct Instance {uint64_t uid=0;uint32_t category=0;Vec3 low,high;float matrix[9];Vec3 translation;bool terrain=false,wmo=false;};
uint64_t lampIdentity(const std::string& key,const Instance& instance,uint32_t localId){
    if(key.size()!=64||!localId||instance.category!=2)return 0;
    unsigned char bytes[48]={};auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(unsigned i=0;i<32;++i){const int a=digit(key[2*i]),b=digit(key[2*i+1]);if(a<0||b<0)return 0;bytes[i]=static_cast<unsigned char>((a<<4)|b);}
    const uint32_t index=0x90000000u+(localId-1);
    for(unsigned i=0;i<8;++i)bytes[32+i]=static_cast<unsigned char>(instance.uid>>(8*i));
    for(unsigned i=0;i<4;++i){bytes[40+i]=static_cast<unsigned char>(instance.category>>(8*i));bytes[44+i]=static_cast<unsigned char>(index>>(8*i));}
    NorthlightBackendLoader::Sha256 hash;hash.update(bytes,sizeof bytes);const std::string digest=hash.hex();uint64_t value=0;
    for(unsigned i=0;i<8;++i)value|=uint64_t((digit(digest[2*i])<<4)|digit(digest[2*i+1]))<<(8*i);
    return value;
}
bool boundsOverlap(Vec3 al,Vec3 ah,Vec3 bl,Vec3 bh){return ah.x>=bl.x&&ah.y>=bl.y&&ah.z>=bl.z&&al.x<=bh.x&&al.y<=bh.y&&al.z<=bh.z;}
Vec3 transform(const float* m,Vec3 v){return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,m[6]*v.x+m[7]*v.y+m[8]*v.z};}
bool normalMatrix(const float* m,float* n){
    n[0]=m[4]*m[8]-m[5]*m[7];n[1]=m[5]*m[6]-m[3]*m[8];n[2]=m[3]*m[7]-m[4]*m[6];
    n[3]=m[2]*m[7]-m[1]*m[8];n[4]=m[0]*m[8]-m[2]*m[6];n[5]=m[1]*m[6]-m[0]*m[7];
    n[6]=m[1]*m[5]-m[2]*m[4];n[7]=m[2]*m[3]-m[0]*m[5];n[8]=m[0]*m[4]-m[1]*m[3];
    float determinant=m[0]*n[0]+m[1]*n[1]+m[2]*n[2];
    float scale=0;for(unsigned i=0;i<9;++i)scale=std::max(scale,std::fabs(m[i]));
    if(!std::isfinite(determinant)||scale<1e-12f||std::fabs(determinant)<1e-10f*scale*scale*scale)return false;
    for(unsigned i=0;i<9;++i)n[i]/=determinant;
    return true;
}
// True when the two boxes overlap with IoU >= 1/2 (a previous build's counts are a usable size hint).
bool regionsOverlapHalf(Vec3 al,Vec3 ah,Vec3 bl,Vec3 bh){
    double inter=1,va=1,vb=1;const double e[3][4]={{al.x,ah.x,bl.x,bh.x},{al.y,ah.y,bl.y,bh.y},{al.z,ah.z,bl.z,bh.z}};
    for(const auto& a:e){inter*=std::max(0.0,std::min(a[1],a[3])-std::max(a[0],a[2]));va*=a[1]-a[0];vb*=a[3]-a[2];}
    const double uni=va+vb-inter;return uni>0&&inter*2>=uni;
}
// Same conservative MCNK contract as shadow_terrain.h: each vertex must lie
// in one world-grid chunk (including the existing .03u exporter allowance).
std::pair<int,int> terrainChunk(Vec3 a,Vec3 b,Vec3 c){
    constexpr double origin=17066.666666666666,size=100.0/3.0;
    const double x=std::floor((origin-(double(a.y)+b.y+c.y)/3)/size),y=std::floor((origin-(double(a.x)+b.x+c.x)/3)/size);
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=1024||y>=1024)return {-1,-1};
    const double lx=origin-(y+1)*size,hx=origin-y*size,ly=origin-(x+1)*size,hy=origin-x*size;
    for(auto p:{a,b,c})if(p.x<lx-.03||p.x>hx+.03||p.y<ly-.03||p.y>hy+.03)return {-1,-1};
    return {int(x),int(y)};
}
struct TerrainRun {int x=-1,y=-1;uint32_t first=0,count=0;uint64_t membership[2]={};bool complete=false;NorthlightMeshPageKey::Hasher hash;};
// hintVertices/hintTriangles (0 = not hinted): result was pre-reserved to these counts; only the first overflow past that reserve grows +1/4, then x2.
bool appendInstance(const WorldScene& model,const Instance& instance,Vec3 low,Vec3 high,WorldScene& result,
                    std::vector<uint32_t>& materialRemap,uint64_t& textureBytes,std::string& error,const AllocationAdmission& admit,const std::function<bool(Vec3,Vec3)>& terrainChunkFilter = {},size_t hintVertices = 0,size_t hintTriangles = 0,bool copyTextures=true,uint64_t* membership=nullptr,std::vector<TerrainRun>* terrainRuns=nullptr,bool completeTerrainRuns=true,const std::string* modelKey=nullptr){
    float normal[9];if(!normalMatrix(instance.matrix,normal)){error="FGS3 singular instance transform";return false;}
    AdaptiveGeometryArray<uint32_t> remap;
    if(model.vertices.chunked()||result.vertices.chunked()||(admit&&!admit(model.vertices.size()*sizeof(uint32_t))))remap.chunks(&admit);
    remap.assign(model.vertices.size(),UINT32_MAX);
    auto reserve=[&](auto& values,size_t count,size_t hint=0){
        if(!admit||values.capacity()>=count)return true;
        using Buffer=std::decay_t<decltype(values)>;using T=typename Buffer::value_type;
        if constexpr(std::is_same_v<Buffer,AdaptiveGeometryArray<WorldVertex>>||std::is_same_v<Buffer,AdaptiveGeometryArray<WorldTriangle>>)if(values.chunked())return true;
        const size_t capacity=std::max(count,std::max<size_t>(64,hint&&values.capacity()<=hint?values.capacity()+values.capacity()/4:values.capacity()*2));
        if(!admit(uint64_t(capacity)*sizeof(T))){
            if constexpr(std::is_same_v<Buffer,AdaptiveGeometryArray<WorldVertex>>||std::is_same_v<Buffer,AdaptiveGeometryArray<WorldTriangle>>){values.chunks(&admit);error.clear();return true;}
            error="Geometry allocation deferred";return false;
        }
        try{values.reserve(capacity);}catch(const std::bad_alloc&){
            if constexpr(std::is_same_v<Buffer,AdaptiveGeometryArray<WorldVertex>>||std::is_same_v<Buffer,AdaptiveGeometryArray<WorldTriangle>>){values.chunks(&admit);error.clear();return true;}
            throw;
        }return true;
    };
    NorthlightMeshPageKey::Hasher accepted;uint32_t ordinal=0,acceptedCount=0;std::map<std::pair<int,int>,uint32_t> chunkTotals;const auto triangleBase=result.triangles.size();
    const auto disableRuns=[&](){if(terrainRuns){std::vector<TerrainRun>().swap(*terrainRuns);terrainRuns=nullptr;}chunkTotals.clear();};
    for(const auto& t:model.triangles){
        const auto sourceOrdinal=ordinal++;
        const uint32_t source[3]={t.v0,t.v1,t.v2};Vec3 positions[3];
        // An accepted remap already owns this exact transformed position.
        for(unsigned j=0;j<3;++j){
            const auto mapped=remap[source[j]];
            positions[j]=mapped==UINT32_MAX?transform(instance.matrix,model.vertices[source[j]].position)+instance.translation:result.vertices[mapped].position;
        }
        auto chunk=terrainRuns&&completeTerrainRuns?terrainChunk(positions[0],positions[1],positions[2]):std::pair<int,int>{-1,-1};
        if(terrainRuns&&completeTerrainRuns&&chunk.first>=0){try{auto found=chunkTotals.find(chunk);if(found==chunkTotals.end()){if(chunkTotals.size()>=4096||(admit&&!admit(sizeof(std::pair<const std::pair<int,int>,uint32_t>)+64)))disableRuns();else chunkTotals.emplace(chunk,1);}else ++found->second;}catch(const std::bad_alloc&){disableRuns();}}
        Vec3 tl=minimum(positions[0],minimum(positions[1],positions[2])),th=maximum(positions[0],maximum(positions[1],positions[2]));
        if(!boundsOverlap(tl,th,low,high))continue;
        if(instance.terrain&&terrainChunkFilter&&!terrainChunkFilter(tl,th))continue;
        if(!finite(positions[0])||!finite(positions[1])||!finite(positions[2])){error="FGS3 transformed position overflow";return false;}
        if(terrainRuns&&!completeTerrainRuns){
            // Far preparation needs exact membership, not a complete-MCNK claim.
            // Classify accepted triangles only; most remain inside the last 4x4 cell.
            constexpr double origin=17066.666666666666,size=400.0/3.0,margin=.001;
            const auto previous=terrainRuns->empty()?std::pair<int,int>{-1,-1}:std::pair<int,int>{terrainRuns->back().x,terrainRuns->back().y};
            if(previous.first>=0&&tl.x>origin-(previous.second+1)*size+margin&&th.x<origin-previous.second*size-margin&&tl.y>origin-(previous.first+1)*size+margin&&th.y<origin-previous.first*size-margin)chunk=previous;
            else{const double x=std::floor((origin-(double(positions[0].y)+positions[1].y+positions[2].y)/3)/size),y=std::floor((origin-(double(positions[0].x)+positions[1].x+positions[2].x)/3)/size);if(x>=0&&y>=0&&x<256&&y<256)chunk={int(x),int(y)};}
        }
        if(result.vertices.size()>4000000-3||result.triangles.size()>=6000000){error="FGS3 local region geometry limit exceeded";return false;}
        if(!reserve(result.vertices,result.vertices.size()+3,hintVertices)||!reserve(result.triangles,result.triangles.size()+1,hintTriangles))return false;
        WorldTriangle out;uint32_t* target[3]={&out.v0,&out.v1,&out.v2};
        for(unsigned j=0;j<3;++j){
            if(remap[source[j]]==UINT32_MAX){
                WorldVertex v=model.vertices[source[j]];v.position=positions[j];v.normal=normalized(transform(normal,v.normal));
                remap[source[j]]=uint32_t(result.vertices.size());result.vertices.push_back(v);
            }
            *target[j]=remap[source[j]];
        }
        if(materialRemap[t.material]==UINT32_MAX){
            const auto& m=model.materials[t.material];
            if(result.materials.size()>=65536||textureBytes+(copyTextures?m.texels().size():0)>256ull*1024*1024){error="FGS3 local region material limit exceeded";return false;}
            if(!reserve(result.materials,result.materials.size()+1)||(admit&&!admit(copyTextures?m.texels().size():0))){error="Geometry allocation deferred";return false;}
            materialRemap[t.material]=uint32_t(result.materials.size());
            if(copyTextures)result.materials.push_back(m);
            else{WorldMaterial metadata;metadata.albedo=m.albedo;metadata.alphaCutoff=m.alphaCutoff;metadata.addressU=m.addressU;metadata.addressV=m.addressV;result.materials.push_back(std::move(metadata));}
            result.materials.back().terrain=instance.terrain;result.materials.back().wmo=instance.wmo;textureBytes+=copyTextures?m.texels().size():0;
            auto& ownerMaterial=result.materials.back();ownerMaterial.pointOnly=m.pointOnly;ownerMaterial.lampLocalId=m.lampLocalId;ownerMaterial.lampSourceId=m.lampSourceId;
            if(modelKey&&m.lampLocalId)ownerMaterial.lampSourceId=lampIdentity(*modelKey,instance,m.lampLocalId);
        }
        if(terrainRuns){try{if(terrainRuns->empty()||terrainRuns->back().x!=chunk.first||terrainRuns->back().y!=chunk.second){if(terrainRuns->size()==terrainRuns->capacity()){const auto capacity=std::min<size_t>(4096,std::max<size_t>(16,terrainRuns->capacity()*2));if(terrainRuns->size()>=4096||(admit&&!admit(capacity*sizeof(TerrainRun))))disableRuns();else terrainRuns->reserve(capacity);}if(terrainRuns){TerrainRun run;run.x=chunk.first;run.y=chunk.second;run.first=uint32_t(result.triangles.size()-triangleBase);terrainRuns->push_back(std::move(run));}}if(terrainRuns){auto& run=terrainRuns->back();++run.count;run.hash.word(sourceOrdinal);}}catch(const std::bad_alloc&){disableRuns();}}
        out.material=materialRemap[t.material];result.triangles.push_back(out);
        if(membership){accepted.word(sourceOrdinal);++acceptedCount;}
    }
    if(terrainRuns)for(auto& run:*terrainRuns){const auto key=run.hash.finish(uint32_t(model.triangles.size()),run.count);run.membership[0]=key.lo;run.membership[1]=key.hi;run.complete=completeTerrainRuns&&run.x>=0&&chunkTotals[{run.x,run.y}]==run.count;}
    if(membership){const auto key=accepted.finish(uint32_t(model.triangles.size()),acceptedCount);membership[0]=key.lo;membership[1]=key.hi;}
    return true;
}
}

namespace {
using InstanceGroups=std::map<std::string,std::vector<Instance>>;
bool selectInstances(const std::vector<std::string>& files,Vec3 low,Vec3 high,uint32_t categoryMask,InstanceGroups& groups,std::string& error,const std::function<bool(Vec3,Vec3)>& instanceFilter = {}){
    std::set<std::pair<uint32_t,uint64_t>> seen;size_t selected=0;
        for(const auto& path:files){
            Reader r;r.file=NorthlightFiles::open(path.c_str(),"rb");if(!r.file){error="Cannot open FGS3 tile: "+path;return false;}
            if(std::fseek(r.file,0,SEEK_END)!=0){error="Cannot seek FGS3 tile";return false;}
            long length=std::ftell(r.file);
            if(length<12||length>128L*1024L*1024L||std::fseek(r.file,0,SEEK_SET)!=0){error="Invalid FGS3 file size";return false;}
            r.remaining=uint64_t(length);char magic[4];uint32_t version=0,count=0;
            if(!r.bytes(magic,4)||std::memcmp(magic,"FGS3",4)||!r.u32(version)||version!=3||!r.u32(count)||count>1000000||uint64_t(count)*116!=r.remaining){
                error="Invalid FGS3 header/count";return false;
            }
            for(uint32_t i=0;i<count;++i){
                uint8_t hash[32];uint64_t uid;uint32_t category;Instance instance;
                if(!r.bytes(hash,32)||!r.u64(uid)||!r.u32(category)||category>3||!r.vec(instance.low)||!r.vec(instance.high)){
                    error="Invalid FGS3 instance header";return false;
                }
                instance.uid=uid;instance.category=category;
                instance.terrain=category==0;
                instance.wmo=category==2;
                for(auto& v:instance.matrix)if(!r.f32(v)){error="Invalid FGS3 transform";return false;}
                if(!r.vec(instance.translation)||instance.low.x>instance.high.x||instance.low.y>instance.high.y||instance.low.z>instance.high.z){error="Invalid FGS3 instance bounds";return false;}
                if(!(categoryMask&(1u<<category))||!boundsOverlap(instance.low,instance.high,low,high))continue;
                if(instanceFilter&&!instanceFilter(instance.low,instance.high))continue;
                if(!seen.emplace(category,uid).second)continue;
                if(++selected>100000){error="FGS3 local instance count limit exceeded";return false;}
                char key[65];const char hex[]="0123456789abcdef";
                for(unsigned j=0;j<32;++j){key[2*j]=hex[hash[j]>>4];key[2*j+1]=hex[hash[j]&15];}key[64]=0;
                groups[std::string(key)].push_back(instance);
            }
        }
    return true;
}
}

bool loadInstancedScenes(const std::vector<std::string>& files,const std::string& modelDirectory,
                         Vec3 low,Vec3 high,WorldScene& output,std::string& error,uint32_t categoryMask,const AllocationAdmission& admit,const std::function<bool(Vec3,Vec3)>& instanceFilter,const std::function<bool(Vec3,Vec3)>& terrainChunkFilter,SceneSizeHint* sizeHint,const SceneSourceProvider& sourceProvider,bool terrainChunkProvenance,bool chunkedTerrain,bool spooledTerrain){
    error.clear();if(sizeHint)sizeHint->hinted=false;
    if(files.size()>((categoryMask==1&&instanceFilter)?256u:64u)||!finite(low)||!finite(high)||low.x>high.x||low.y>high.y||low.z>high.z){error="Invalid FGS3 region or tile count";return false;}
    std::map<std::string,std::vector<Instance>> groups;
    WorldScene result;uint64_t textureBytes=0;bool hinted=false;size_t hintV=0,hintT=0;
    try{
        if(spooledTerrain){result.vertices.spool(&admit);result.triangles.spool(&admit);}else if(chunkedTerrain){result.vertices.chunks(&admit);result.triangles.chunks(&admit);}
        if(!selectInstances(files,low,high,categoryMask,groups,error,instanceFilter))return false;
        if(sizeHint&&sizeHint->sized&&sizeHint->sizedKey==sizeHint->key&&regionsOverlapHalf(low,high,sizeHint->low,sizeHint->high)){
            const uint64_t v=std::min<uint64_t>(4000000,sizeHint->vertices+sizeHint->vertices/16),t=std::min<uint64_t>(6000000,sizeHint->triangles+sizeHint->triangles/16);
            if(v&&t&&(!admit||(admit(v*sizeof(WorldVertex))&&admit(t*sizeof(WorldTriangle))))){result.vertices.reserve(v);result.triangles.reserve(t);hinted=true;hintV=size_t(v);hintT=size_t(t);}
        }
        for(const auto& group:groups){
            std::string path=modelDirectory;
            if(!path.empty()&&path.back()!='/'&&path.back()!='\\')path+='/';
            path+=group.first+".fgs";WorldScene loaded;std::shared_ptr<const WorldScene> shared;
            if(sourceProvider){shared=sourceProvider(path,error,admit);if(!shared&&!error.empty())return false;}
            if(!shared&&!readScene(path.c_str(),loaded,error,admit,false,spooledTerrain&&!sourceProvider)){error="FGS3 model "+group.first+": "+error;return false;}
            const WorldScene& model=shared?*shared:loaded;
            // A shared model hash can legally appear as terrain and as another
            // category. Keep terrain, WMO and other metadata distinct;
            // one descriptor must not relabel earlier materials.
            std::vector<uint32_t> materialRemap[3];
            for(auto& remap:materialRemap)remap.resize(model.materials.size(),UINT32_MAX);
            for(const auto& instance:group.second){
                const size_t before=result.triangles.size(),vertexBefore=result.vertices.size();uint64_t membership[2]={};
                std::vector<TerrainRun> terrainRuns;
                // One preparation entry per contiguous 4x4-cell run avoids per-MCNK metadata dominating the existing 8 MiB memo.
                const bool chunks=terrainChunkProvenance&&instance.terrain&&model.sourceIdentity;
                auto& materials=materialRemap[instance.terrain?1:instance.wmo?2:0];
                for(size_t m=0;m<model.materials.size();++m)if(model.materials[m].lampLocalId)materials[m]=UINT32_MAX;
                if(!appendInstance(model,instance,low,high,result,materialRemap[instance.terrain?1:instance.wmo?2:0],textureBytes,error,admit,terrainChunkFilter,hintV,hintT,true,model.sourceIdentity?membership:nullptr,chunks?&terrainRuns:nullptr,false,&group.first))return false;
                if(model.sourceIdentity&&result.triangles.size()>before){
                    // Provenance is optional: refusal must retain every accepted triangle.
                    const auto publish=[&](bool chunkRanges){
                        const auto count=chunkRanges?terrainRuns.size():size_t(1);
                        if(admit&&!admit(count*sizeof(SourcePieceRange)))return false;
                        std::vector<SourcePieceRange> ranges;ranges.reserve(count);
                        std::vector<uint8_t> used;
                        if(chunkRanges){if(admit&&!admit(result.materials.size()))return false;used.resize(result.materials.size());}
                        for(size_t i=0;i<count;++i){
                            SourcePieceRange range;range.sourceIdentity=model.sourceIdentity;
                            range.triangleBase=uint32_t(before)+(chunkRanges?terrainRuns[i].first:0);
                            range.triangleCount=chunkRanges?terrainRuns[i].count:uint32_t(result.triangles.size()-before);
                            range.complete=chunkRanges?terrainRuns[i].complete:range.triangleCount==model.triangles.size();
                            range.membership[0]=chunkRanges?terrainRuns[i].membership[0]:membership[0];range.membership[1]=chunkRanges?terrainRuns[i].membership[1]:membership[1];
                            if(chunkRanges){
                                uint32_t first=UINT32_MAX,last=0,materialCount=0;
                                for(size_t t=range.triangleBase;t<uint64_t(range.triangleBase)+range.triangleCount;++t){const auto& triangle=result.triangles[t];first=std::min({first,triangle.v0,triangle.v1,triangle.v2});last=std::max({last,triangle.v0,triangle.v1,triangle.v2});if(!used[triangle.material]){used[triangle.material]=1;++materialCount;}}
                                range.vertexBase=first;range.vertexCount=last-first+1;
                                if(admit&&!admit(uint64_t(materialCount)*sizeof(uint32_t)))return false;
                                range.materialMap.reserve(materialCount);
                                for(auto m:materials)if(m!=UINT32_MAX&&used[m]){range.materialMap.push_back(m);used[m]=0;}
                            }else{
                                range.vertexBase=uint32_t(vertexBefore);range.vertexCount=uint32_t(result.vertices.size()-vertexBefore);
                                if(admit&&!admit(uint64_t(materials.size())*sizeof(uint32_t)))return false;range.materialMap=materials;
                            }
                            if(admit&&!admit(group.first.size()+1))return false;
                            auto& owner=range.placement;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;
                            ranges.push_back(std::move(range));
                        }
                        if(result.sourcePieces.capacity()<result.sourcePieces.size()+count){const auto capacity=std::max(result.sourcePieces.size()+count,result.sourcePieces.capacity()*2);if(admit&&!admit(capacity*sizeof(SourcePieceRange)))return false;result.sourcePieces.reserve(capacity);}
                        for(auto& range:ranges)result.sourcePieces.push_back(std::move(range));
                        return true;
                    };
                    bool published=false;
                    if(chunks&&!terrainRuns.empty())try{published=publish(true);}catch(const std::bad_alloc&){}
                    if(!published)try{publish(false);}catch(const std::bad_alloc&){}
                }
                // Count accepted triangles rather than trust descriptor bounds:
                // partial crops must never suppress the complete shadow model.
                if(!instance.terrain&&!model.triangles.empty()&&result.triangles.size()-before==model.triangles.size()){
                    WorldPlacementCoverage owner;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;
                    std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;
                    result.completePlacements.push_back(std::move(owner));
                }
            }
        }
    }catch(const TemporaryGeometryError& failure){error=failure.what();return false;}catch(...){error="Cannot allocate FGS3 local world region";return false;}
    // Cold terrain assembly can leave both geometry vectors oversized while
    // later page preparation holds them. Compaction is optional under pressure.
    const uint64_t slack=uint64_t(result.vertices.capacity()-result.vertices.size())*sizeof(WorldVertex)+uint64_t(result.triangles.capacity()-result.triangles.size())*sizeof(WorldTriangle);
    if(slack>=4u*1024u*1024u){
        auto compact=[&](auto& values){using Buffer=std::decay_t<decltype(values)>;using T=typename Buffer::value_type;const uint64_t bytes=uint64_t(values.size())*sizeof(T);try{if(values.chunked()||values.capacity()==values.size()||(admit&&!admit(bytes)))return;Buffer exact(values.begin(),values.end());values.swap(exact);}catch(...) {}};
        compact(result.triangles);compact(result.vertices);
    }
    if(sizeHint){sizeHint->hinted=hinted;sizeHint->sized=true;sizeHint->sizedKey=sizeHint->key;sizeHint->vertices=result.vertices.size();sizeHint->triangles=result.triangles.size();sizeHint->low=low;sizeHint->high=high;}
    result.vertices.detachAdmission();result.triangles.detachAdmission();output=std::move(result);return true;
}

// Bilinear alpha is sum(texel/255*weight) over four texels whose float weights
// sum to 1 within a few ulps, so it is >= minTexel/255*(1-8*2^-24). The 1e-5
// margin keeps the skip strictly inside that bound; NaN alpha never culls.
static bool alphaNeverCut(const WorldMaterial& m){
    if(m.texels().empty())return true; /* materialSample returns alpha 1 >= cutoff (validated <= 1) */
    if(m.alphaCutoff==0.f)return true; // Every texel alpha is nonnegative.
    const auto texels=m.texels();const uint8_t low=NorthlightMaterialBytes::minimumAlpha(texels.data(),texels.size());
    return double(low)/255.0*(1.0-1e-5)>=double(m.alphaCutoff);
}
static bool validateBVHScene(WorldScene& s,std::string& error){
    error.clear();
    if(s.vertices.spooled()||s.triangles.spooled()){error="Temporary geometry cannot be published in a BVH";return false;}
    if(s.triangles.size()>=(1u<<28)||s.vertices.size()>UINT32_MAX){error="Scene exceeds 32-bit index capacity";return false;}
    for(const auto& v:s.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)){
        error="Scene contains nonfinite vertex data";return false;
    }
    for(auto& m:s.materials){
        if(!finite(m.albedo)||!std::isfinite(m.alphaCutoff)||m.alphaCutoff<0||m.alphaCutoff>1||m.width>4096||m.height>4096||m.addressU<1||m.addressU>3||m.addressV<1||m.addressV>3||
           uint64_t(m.width)*m.height*4!=m.texels().size()||((m.width==0)!=(m.height==0))){error="Scene contains invalid material";return false;}
        m.albedo=reflectance(m.albedo);
    }
    for(const auto& t:s.triangles)if(t.v0>=s.vertices.size()||t.v1>=s.vertices.size()||t.v2>=s.vertices.size()||t.material>=s.materials.size()){
        error="Scene contains invalid triangle index";return false;
    }
    return true;
}
bool BVH::build(WorldScene&& s,std::string& error,bool fast,const AllocationAdmission& admit) {return buildImpl(std::move(s),error,fast,{},{},admit);}
bool BVH::buildImpl(WorldScene&& s,std::string& error,bool fast,std::shared_ptr<const MaterialBank> bank,std::vector<uint32_t> sources,const AllocationAdmission& admit) {
    if(!validateBVHScene(s,error))return false;
    // Build into a temporary so even allocation failure preserves the old scene.
    if(bank){
        if(sources.size()!=s.materials.size()){error="Invalid shared material map";return false;}
        for(auto i:sources)if(i>=bank->values.size()){error="Shared material index out of range";return false;}
    }
    BVH next;next.scene_=std::move(s);next.sah_=TraceSah&&fast;next.materialBank_=std::move(bank);next.materialSources_=std::move(sources);
    try {
        if(next.scene_.vertices.chunked()||next.scene_.triangles.chunked()){next.order_.chunks(&admit);next.pairs_.chunks(&admit);next.nodes_.chunks(&admit);next.centers_.chunks(&admit);}
        next.order_.reserve(next.scene_.triangles.size());
        for(uint32_t i=0;i<next.scene_.triangles.size();++i){
            const auto& t=next.scene_.triangles[i];
            Vec3 a=next.scene_.vertices[t.v0].position,b=next.scene_.vertices[t.v1].position,c=next.scene_.vertices[t.v2].position;
            Vec3 n=cross(b-a,c-a);
            if(dot(n,n)>1e-16f) next.order_.push_back(i);
        }
        if constexpr(TracePairNodes){
            if(next.sah_){
                next.buildSahTree();
            }else{
                // Exact interior count of the median tree, so the array never regrows.
                struct Count {static size_t interior(uint32_t n){return n<=6?0:1+interior(n/2)+interior(n-n/2);}};
                next.pairs_.reserve(Count::interior(uint32_t(next.order_.size())));
                if(!next.order_.empty())next.root_=next.makePair(0,uint32_t(next.order_.size()),next.rootLow_,next.rootHigh_);
            }
        }else{
            next.nodes_.reserve(next.order_.size()/2+1);
            if(!next.order_.empty())next.makeNode(0,uint32_t(next.order_.size()));
        }
        next.opaque_.resize(next.scene_.materials.size());
        for(size_t i=0;i<next.opaque_.size();++i)next.opaque_[i]=alphaNeverCut(next.material(i));
    } catch(...) {error="Cannot allocate geometry BVH";return false;}
    next.scene_.vertices.detachAdmission();next.scene_.triangles.detachAdmission();next.order_.detachAdmission();next.pairs_.detachAdmission();next.nodes_.detachAdmission();next.centers_.detachAdmission();*this=std::move(next);return true;
}

void BVH::buildSahTree(){
    if(order_.empty())return;
    centers_.resize(scene_.triangles.size());
    for(uint32_t i:order_){const auto&t=scene_.triangles[i];centers_[i]=(scene_.vertices[t.v0].position+scene_.vertices[t.v1].position+scene_.vertices[t.v2].position)/3.f;}
    pairs_.reserve(size_t(double(order_.size())*TraceSahPairsPerTriangle));
    if(!order_.empty()){BuildBox box=bounds(0,uint32_t(order_.size()));root_=makeSah(0,uint32_t(order_.size()),box);rootLow_=box.low;rootHigh_=box.high;inflate(rootLow_,rootHigh_);}
    centers_.free();
    if(pairs_.capacity()>pairs_.size()+pairs_.size()/8)pairs_.shrink_to_fit();
}

// Bounds, then the median split (0 for a leaf). Shared by both node layouts
// so they produce the identical tree and triangle order.
uint32_t BVH::partition(uint32_t start,uint32_t count,Vec3& low,Vec3& high) {
    const float inf=std::numeric_limits<float>::infinity();
    low={inf,inf,inf};high={-inf,-inf,-inf};
    Vec3 cl=low,ch=high;
    auto center=[this](uint32_t index){const auto&t=scene_.triangles[index];return (scene_.vertices[t.v0].position+scene_.vertices[t.v1].position+scene_.vertices[t.v2].position)/3.f;};
    for(uint32_t i=start;i<start+count;++i){
        const auto&t=scene_.triangles[order_[i]];
        Vec3 a=scene_.vertices[t.v0].position,b=scene_.vertices[t.v1].position,c=scene_.vertices[t.v2].position;
        low=minimum(low,minimum(a,minimum(b,c)));high=maximum(high,maximum(a,maximum(b,c)));
        Vec3 q=(a+b+c)/3.f;cl=minimum(cl,q);ch=maximum(ch,q);
    }
    if(count<=6)return 0;
    Vec3 extent=ch-cl;unsigned axis=extent.y>extent.x?1:0;if(extent.z>component(extent,axis))axis=2;
    uint32_t mid=start+count/2;
    std::nth_element(order_.begin()+start,order_.begin()+mid,order_.begin()+start+count,[&](uint32_t a,uint32_t b){return component(center(a),axis)<component(center(b),axis);});
    return mid;
}
// Triangle and centroid bounds of order_[start,start+count).
BVH::BuildBox BVH::bounds(uint32_t start,uint32_t count) const {
    const float inf=std::numeric_limits<float>::infinity();
    BuildBox box{{inf,inf,inf},{-inf,-inf,-inf},{inf,inf,inf},{-inf,-inf,-inf}};
    for(uint32_t i=start;i<start+count;++i){
        const auto&t=scene_.triangles[order_[i]];
        Vec3 a=scene_.vertices[t.v0].position,b=scene_.vertices[t.v1].position,c=scene_.vertices[t.v2].position;
        box.low=minimum(box.low,minimum(a,minimum(b,c)));box.high=maximum(box.high,maximum(a,maximum(b,c)));
        box.cl=minimum(box.cl,centers_[order_[i]]);box.ch=maximum(box.ch,centers_[order_[i]]);
    }
    return box;
}
// Binned SAH split (0 for a leaf) over precomputed centroids. Child bounds come
// from the bins, so each level makes one binning and one partition pass. Depth
// is capped; below the cap a median split guarantees progress (depth < 80).
uint32_t BVH::sahSplit(uint32_t start,uint32_t count,const BuildBox& box,BuildBox (&child)[2]) {
    if(count<=2)return 0;
    const float inf=std::numeric_limits<float>::infinity();
    constexpr unsigned B=16;
    const Vec3 extent=box.ch-box.cl;float scale[3];
    for(unsigned ax=0;ax<3;++ax)scale[ax]=component(extent,ax)>0?float(B)/component(extent,ax):0.f;
    auto bin=[&](uint32_t index,unsigned ax){return std::min(B-1,unsigned((component(centers_[index],ax)-component(box.cl,ax))*scale[ax]));};
    auto area=[](Vec3 lo,Vec3 hi){Vec3 e=hi-lo;return e.x*e.y+e.y*e.z+e.z*e.x;};
    const BuildBox empty{{inf,inf,inf},{-inf,-inf,-inf},{inf,inf,inf},{-inf,-inf,-inf}};
    // Large nodes bin only their widest centroid axis (3x cheaper build, ~same tree quality).
    unsigned widest=extent.y>extent.x?1:0;if(extent.z>component(extent,widest))widest=2;
    const bool allAxes=count<=TraceSahAllAxesBelow;
    float bestCost=inf;unsigned bestAxis=0,bestSplit=0;
    struct Bin {Vec3 low,high;unsigned count;};Bin bins[3][B];
    if(depth_<48&&(scale[0]>0||scale[1]>0||scale[2]>0)){
        // Only axes considered below can be read by binning or cost evaluation.
        for(unsigned ax=0;ax<3;++ax)if(scale[ax]>0&&(allAxes||ax==widest))for(unsigned k=0;k<B;++k)bins[ax][k]={{inf,inf,inf},{-inf,-inf,-inf},0};
        for(uint32_t i=start;i<start+count;++i){
            const uint32_t index=order_[i];const auto&t=scene_.triangles[index];
            Vec3 a=scene_.vertices[t.v0].position,b=scene_.vertices[t.v1].position,c=scene_.vertices[t.v2].position;
            const Vec3 lo=minimum(a,minimum(b,c)),hi=maximum(a,maximum(b,c));
            for(unsigned ax=0;ax<3;++ax)if(scale[ax]>0&&(allAxes||ax==widest)){Bin& e=bins[ax][bin(index,ax)];e.low=minimum(e.low,lo);e.high=maximum(e.high,hi);++e.count;}
        }
        // Cost in triangle tests: one two-box pair test plus area-weighted children.
        const float parent=std::max(area(box.low,box.high),1e-20f);
        for(unsigned ax=0;ax<3;++ax)if(scale[ax]>0&&(allAxes||ax==widest)){
            float rightArea[B];unsigned rightCount[B];Vec3 lo={inf,inf,inf},hi={-inf,-inf,-inf};unsigned n=0;
            for(unsigned k=B-1;k>0;--k){lo=minimum(lo,bins[ax][k].low);hi=maximum(hi,bins[ax][k].high);n+=bins[ax][k].count;rightCount[k]=n;rightArea[k]=n?area(lo,hi):0;}
            lo={inf,inf,inf};hi={-inf,-inf,-inf};n=0;
            for(unsigned k=1;k<B;++k){
                lo=minimum(lo,bins[ax][k-1].low);hi=maximum(hi,bins[ax][k-1].high);n+=bins[ax][k-1].count;
                if(!n||!rightCount[k])continue;
                float cost=TraceSahPairCost+(area(lo,hi)*float(n)+rightArea[k]*float(rightCount[k]))/parent;
                if(cost<bestCost){bestCost=cost;bestAxis=ax;bestSplit=k;}
            }
        }
    }
    if(count<=6&&!(bestCost<float(count)))return 0;
    if(bestSplit){
        // Partition in place; child triangle bounds come from the bins and
        // centroid bounds are gathered while moving.
        child[0]=child[1]=empty;for(unsigned k=0;k<B;++k)if(bins[bestAxis][k].count){auto& c=child[k<bestSplit?0:1];c.low=minimum(c.low,bins[bestAxis][k].low);c.high=maximum(c.high,bins[bestAxis][k].high);}
        uint32_t first=start,last=start+count;
        while(first<last){
            if(bin(order_[first],bestAxis)<bestSplit){child[0].cl=minimum(child[0].cl,centers_[order_[first]]);child[0].ch=maximum(child[0].ch,centers_[order_[first]]);++first;}
            else{--last;std::swap(order_[first],order_[last]);child[1].cl=minimum(child[1].cl,centers_[order_[last]]);child[1].ch=maximum(child[1].ch,centers_[order_[last]]);}
        }
        return first;
    }
    if(count<=6)return 0;
    unsigned axis=extent.y>extent.x?1:0;if(extent.z>component(extent,axis))axis=2;
    const uint32_t mid=start+count/2;
    std::nth_element(order_.begin()+start,order_.begin()+mid,order_.begin()+start+count,[&](uint32_t a,uint32_t b){return component(centers_[a],axis)<component(centers_[b],axis);});
    child[0]=bounds(start,mid-start);child[1]=bounds(mid,start+count-mid);
    return mid;
}
uint32_t BVH::makeSah(uint32_t start,uint32_t count,const BuildBox& box) {
    BuildBox child[2];const uint32_t mid=sahSplit(start,count,box,child);
    if(!mid)return LeafBit|start<<3|count;
    const uint32_t result=uint32_t(pairs_.size());pairs_.emplace_back();
    ++depth_;const uint32_t refs[2]={makeSah(start,mid-start,child[0]),makeSah(mid,start+count-mid,child[1])};--depth_;
    Pair& p=pairs_[result];
    for(unsigned side=0;side<2;++side){
        Vec3 l=child[side].low,h=child[side].high;inflate(l,h);p.child[side]=refs[side];
        for(unsigned i=0;i<3;++i){p.axis[i][side]=component(l,i);p.axis[i][2+side]=component(h,i);}
    }
    p.pad[0]=p.pad[1]=0;return result;
}
uint32_t BVH::makeNode(uint32_t start,uint32_t count) {
    Node node;uint32_t mid=partition(start,count,node.low,node.high);
    uint32_t result=uint32_t(nodes_.size());nodes_.push_back(node);
    if(!mid){nodes_[result].start=start;nodes_[result].count=count;return result;}
    uint32_t left=makeNode(start,mid-start),right=makeNode(mid,start+count-mid);
    nodes_[result].left=left;nodes_[result].right=right;return result;
}
uint32_t BVH::makePair(uint32_t start,uint32_t count,Vec3& low,Vec3& high) {
    uint32_t mid=partition(start,count,low,high);
    if(!mid)return LeafBit|start<<3|count;
    uint32_t result=uint32_t(pairs_.size());pairs_.emplace_back();
    Vec3 l[2],h[2];++depth_;uint32_t child[2]={makePair(start,mid-start,l[0],h[0]),makePair(mid,start+count-mid,l[1],h[1])};--depth_;
    Pair& p=pairs_[result];
    for(unsigned side=0;side<2;++side){p.child[side]=child[side];for(unsigned i=0;i<3;++i){p.axis[i][side]=component(l[side],i);p.axis[i][2+side]=component(h[side],i);}}
    p.pad[0]=p.pad[1]=0;return result;
}

namespace {
bool intersectsBox(Vec3 origin,Vec3 direction,Vec3 low,Vec3 high,float minT,float maxT,float& entry){
    for(unsigned i=0;i<3;++i){
        float o=component(origin,i),d=component(direction,i),a=component(low,i),b=component(high,i);
        if(std::fabs(d)<1e-12f){if(o<a||o>b)return false;continue;}
        float p=(a-o)/d,q=(b-o)/d;if(p>q)std::swap(p,q);
        minT=std::max(minT,p);maxT=std::min(maxT,q);if(minT>maxT)return false;
    }
    entry=minT;return true;
}
// Two-box slab test. Without Sah it has intersectsBox's exact accept/
// reject and entry comparisons: per lane p=(bound-o)/d is the same IEEE
// division, near/far are min/max of p,q (never NaN for |d|>=1e-12 and finite
// bounds), and the original per-axis early-out equals the final entry<=exit
// (monotone bounds). Only the sign of a zero entry can differ, which no
// comparison observes. Sah multiplies by 1/d; its inflated boxes absorb that.
// Slab division/reciprocal rounding can put a box entry a few ulps past
// an equal-distance triangle hit. Widen traversal only; primitive distances
// retain the caller's exact bounds and the SAH lowest-triangle tie policy.
static float traversalUpper(float distance){return std::nextafter(distance+std::fabs(distance)*(8*std::numeric_limits<float>::epsilon()),std::numeric_limits<float>::infinity());}
} // namespace
namespace detail {
template<bool Sah> struct PairRay {
    bool skip[3];float o[3];
#if defined(__SSE2__)
    __m128 vo[3],vd[3];
    PairRay(Vec3 origin,Vec3 direction){for(unsigned i=0;i<3;++i){o[i]=component(origin,i);float d=component(direction,i);skip[i]=std::fabs(d)<1e-12f;vo[i]=_mm_set1_ps(o[i]);vd[i]=_mm_set1_ps(Sah&&!skip[i]?1.f/d:d);}}
    unsigned test(const float (&axis)[3][4],float minT,float maxT,float& le,float& re)const{
        __m128 entry=_mm_set1_ps(minT),exit=_mm_set1_ps(maxT);unsigned valid=3;
        for(unsigned i=0;i<3;++i){
            const __m128 bound=_mm_loadu_ps(axis[i]);
            if(skip[i]){valid&=~unsigned((_mm_movemask_ps(_mm_cmplt_ps(vo[i],bound))&3)|((_mm_movemask_ps(_mm_cmpgt_ps(vo[i],bound))>>2)&3));continue;}
            const __m128 delta=_mm_sub_ps(bound,vo[i]),t=Sah?_mm_mul_ps(delta,vd[i]):_mm_div_ps(delta,vd[i]),swapped=_mm_shuffle_ps(t,t,_MM_SHUFFLE(1,0,3,2));
            entry=_mm_max_ps(entry,_mm_min_ps(t,swapped));exit=_mm_min_ps(exit,_mm_max_ps(t,swapped));
        }
        float e[4];_mm_storeu_ps(e,entry);le=e[0];re=e[1];
        return unsigned(_mm_movemask_ps(_mm_cmple_ps(entry,exit)))&valid;
    }
#else
    float d[3];
    PairRay(Vec3 origin,Vec3 direction){for(unsigned i=0;i<3;++i){o[i]=component(origin,i);d[i]=component(direction,i);skip[i]=std::fabs(d[i])<1e-12f;if(Sah&&!skip[i])d[i]=1.f/d[i];}}
    unsigned test(const float (&axis)[3][4],float minT,float maxT,float& le,float& re)const{
        unsigned mask=0;
        for(unsigned side=0;side<2;++side){
            float entry=minT,exit=maxT;bool ok=true;
            for(unsigned i=0;i<3&&ok;++i){
                float a=axis[i][side],b=axis[i][2+side];
                if(skip[i]){ok=!(o[i]<a||o[i]>b);continue;}
                float p=Sah?(a-o[i])*d[i]:(a-o[i])/d[i],q=Sah?(b-o[i])*d[i]:(b-o[i])/d[i];if(p>q)std::swap(p,q);
                entry=std::max(entry,p);exit=std::min(exit,q);ok=entry<=exit;
            }
            (side?re:le)=entry;if(ok)mask|=1u<<side;
        }
        return mask;
    }
#endif
};
} // namespace detail
namespace {
using detail::PairRay;
struct Sample {Vec3 color;float alpha;};
float srgb(uint8_t v){float x=float(v)/255.f;return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
const std::array<float,256>& srgbTable(){static const std::array<float,256> table=[](){std::array<float,256> a{};for(unsigned i=0;i<256;++i)a[i]=srgb(uint8_t(i));return a;}();return table;}
float addressCoordinate(float value,uint32_t mode){
    if(mode==3)return std::max(0.f,std::min(1.f,value));
    if(mode==2){float phase=value-2.f*std::floor(value*.5f);return phase<=1.f?phase:2.f-phase;}
    return value-std::floor(value);
}
unsigned addressTexel(int index,uint32_t size,uint32_t mode){
    if(mode==1)return unsigned((index+int(size))%int(size));
    return unsigned(std::max(0,std::min(int(size)-1,index)));
}
template<bool Color=true> Sample materialSample(const WorldMaterial& m,float u,float v){
    if(m.texels().empty())return {m.albedo,1.f};
    // Address normalized coordinates before integer conversion. Mirror/clamp
    // repeat the edge texel at bilinear footprints; wrap crosses the seam.
    float x=addressCoordinate(u,m.addressU)*m.width-.5f,y=addressCoordinate(v,m.addressV)*m.height-.5f;
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-std::floor(x),fy=y-std::floor(y);
    Sample result{{0,0,0},0};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){
        unsigned tx=addressTexel(ix+i,m.width,m.addressU),ty=addressTexel(iy+j,m.height,m.addressV);
        const uint8_t* p=&m.texels()[(size_t(ty)*m.width+tx)*4];float w=(i?fx:1-fx)*(j?fy:1-fy);
        if constexpr(Color){const auto& lut=srgbTable();result.color=result.color+Vec3(lut[p[0]],lut[p[1]],lut[p[2]])*w;}
        result.alpha+=float(p[3])/255.f*w;
    }
    result.color=result.color*m.albedo;return result;
}
}

void BVH::shadeHit(RayHit& hit,Vec3 origin,Vec3 direction,uint32_t ti,float u,float v,float distance)const{
        const auto&t=scene_.triangles[ti];
        const auto&a=scene_.vertices[t.v0];const auto&b=scene_.vertices[t.v1];const auto&c=scene_.vertices[t.v2];
        Vec3 ab=b.position-a.position,ac=c.position-a.position;
        float w=1-u-v;const auto&m=material(t.material);
        Sample sample=materialSample<true>(m,w*a.u+u*b.u+v*c.u,w*a.v+u*b.v+v*c.v);
        Vec3 gn=normalized(cross(ab,ac));
        Vec3 normal=normalized(a.normal*w+b.normal*u+c.normal*v);
        Vec3 outward=dot(normal,normal)>.5f?normal:gn;
        bool backFace=dot(outward,direction)>0;
        if(dot(gn,direction)>0)gn=-gn;
        if(dot(normal,normal)<.5f)normal=gn;
        if(dot(normal,gn)<0)normal=-normal;
        // Smooth normals that face the incoming ray can produce negative
        // hemispheres. Use the valid geometric normal at such silhouettes.
        if(dot(normal,direction)>-.001f)normal=gn;
        hit.backFace=backFace;hit.position=origin+direction*distance;
        hit.normal=normal;hit.geometricNormal=gn;hit.albedo=sample.color;hit.triangle=ti;
    }

template<bool AnyHit,bool Sah>
RayHit BVH::traceImpl(Vec3 origin,Vec3 direction,float minT,float maxT,bool normalizedRay,float checkedEntry,float* barycentricOut,const detail::PairRay<Sah>* preparedRay) const {
    RayHit hit;hit.distance=maxT;
    // An empty tree cannot hit, so it needs no ray normalization.
    if(TracePairNodes?order_.empty():nodes_.empty())return hit;
    if(!normalizedRay)direction=normalized(direction);
    if((TracePairNodes?order_.empty():nodes_.empty())||!finite(origin)||dot(direction,direction)<.5f||!std::isfinite(minT)||!std::isfinite(maxT)||minT<0||maxT<=minT)return hit;
    float boxUpper=0;if constexpr(Sah)boxUpper=traversalUpper(maxT);
    uint32_t best=UINT32_MAX;float bestU=0,bestV=0;

    // Returns true when an any-hit query is resolved.
    auto leaf=[&](uint32_t start,uint32_t count){
        for(uint32_t i=start;i<start+count;++i){
            uint32_t ti=order_[i];const auto&t=scene_.triangles[ti];
            const auto&a=scene_.vertices[t.v0];const auto&b=scene_.vertices[t.v1];const auto&c=scene_.vertices[t.v2];
            Vec3 ab=b.position-a.position,ac=c.position-a.position,h=cross(direction,ac);
            float determinant=dot(ab,h);if(std::fabs(determinant)<1e-10f)continue;
            float inv=1.f/determinant;Vec3 s=origin-a.position;float u=dot(s,h)*inv;if(u<0||u>1)continue;
            Vec3 q=cross(s,ab);float v=dot(direction,q)*inv;if(v<0||u+v>1)continue;
            float distance=dot(ac,q)*inv;if(distance<minT||distance>hit.distance)continue;
            if(distance==hit.distance&&(!Sah||AnyHit||!hit.hit||ti>=best))continue;
            // Bilinear alpha is a convex texel blend; opaque_ marks materials
            // whose sample can never fall below the cutoff (see build()).
            if(!TraceOpaqueSkip||!opaque_[t.material]){
                float w=1-u-v;const auto&m=material(t.material);
                if(materialSample<false>(m,w*a.u+u*b.u+v*c.u,w*a.v+u*b.v+v*c.v).alpha<m.alphaCutoff)continue;
            }
            hit.hit=true;if constexpr(AnyHit)return true;
            // Shading depends only on the accepted triangle and barycentrics;
            // evaluate it once for the final closest candidate below.
            if constexpr(Sah)if(distance!=hit.distance)boxUpper=traversalUpper(distance);hit.distance=distance;
            best=ti;if constexpr(TraceDeferShading){bestU=u;bestV=v;}else shadeHit(hit,origin,direction,ti,u,v,distance);
        }
        return false;
    };
    // Median partition bounds tree depth below 33 for the allowed index range,
    // the SAH build below 80 (depth cap 48, then median). Stack <= depth+1.
    // A child's slab entry is final when it is pushed; only the closest-hit
    // bound shrinks afterwards, so entry<=distance repeats the original re-test.
    struct Item {uint32_t node;float entry;};
    Item stack[Sah?128:64];unsigned size=0;
    if constexpr(TracePairNodes){
        float entry=checkedEntry;if(entry<0&&!intersectsBox(origin,direction,rootLow_,rootHigh_,minT,Sah?boxUpper:maxT,entry))return hit;
        if(root_&LeafBit){if(leaf((root_&~LeafBit)>>3,root_&7))return hit;}
        else{stack[size++]={root_,entry};const auto traverse=[&](const PairRay<Sah>& ray){
        while(size){
            const Item item=stack[--size];
            if(item.entry>(Sah?boxUpper:hit.distance))continue;
            if(item.node&LeafBit){if(leaf((item.node&~LeafBit)>>3,item.node&7))return true;continue;}
            const Pair& p=pairs_[item.node];float le,re;
            const unsigned mask=ray.test(p.axis,minT,Sah?boxUpper:hit.distance,le,re);
            if(mask==3){if(le<re){stack[size++]={p.child[1],re};stack[size++]={p.child[0],le};}else{stack[size++]={p.child[0],le};stack[size++]={p.child[1],re};}}
            else if(mask==1)stack[size++]={p.child[0],le};else if(mask==2)stack[size++]={p.child[1],re};
        }
            return false;};
            if(preparedRay){if(traverse(*preparedRay))return hit;}
            else{const PairRay<Sah> ray(origin,direction);if(traverse(ray))return hit;}
        }
    }else{
        {float entry;if(!intersectsBox(origin,direction,nodes_[0].low,nodes_[0].high,minT,maxT,entry))return hit;stack[size++]={0,entry};}
        while(size){
            const Item item=stack[--size];
            if constexpr(TraceCarryEntry){if(item.entry>hit.distance)continue;}
            const Node& node=nodes_[item.node];
            if constexpr(!TraceCarryEntry){float entry;if(!intersectsBox(origin,direction,node.low,node.high,minT,hit.distance,entry))continue;}
            if(!node.count){
                float le=0,re=0;const Node& l=nodes_[node.left];const Node& r=nodes_[node.right];
                bool lh=intersectsBox(origin,direction,l.low,l.high,minT,hit.distance,le),rh=intersectsBox(origin,direction,r.low,r.high,minT,hit.distance,re);
                if(lh&&rh){if(le<re){stack[size++]={node.right,re};stack[size++]={node.left,le};}else{stack[size++]={node.left,le};stack[size++]={node.right,re};}}
                else if(lh)stack[size++]={node.left,le};else if(rh)stack[size++]={node.right,re};
                continue;
            }
            if(leaf(node.start,node.count))return hit;
        }
    }
    if constexpr(!AnyHit)if(TraceDeferShading&&best!=UINT32_MAX){if(barycentricOut){barycentricOut[0]=bestU;barycentricOut[1]=bestV;hit.triangle=best;}else shadeHit(hit,origin,direction,best,bestU,bestV,hit.distance);}
    return hit;
}
const WorldMaterial& BVH::material(size_t index)const{return materialBank_?materialBank_->values[materialSources_[index]]:scene_.materials[index];}
uint64_t BVH::buildScratchBytes(size_t triangles){return uint64_t(triangles)*sizeof(Vec3);}
size_t BVH::ownBytes()const{
    size_t bytes=sizeof(*this)+scene_.retainedBytes()+nodes_.capacity()*sizeof(Node)+pairs_.capacity()*sizeof(Pair)+order_.capacity()*sizeof(uint32_t)+opaque_.capacity()+
        materialSources_.capacity()*sizeof(uint32_t)+regionOrder_.capacity()*sizeof(uint32_t)+regionPairs_.capacity()*sizeof(Pair)+pieces_.capacity()*sizeof(PieceReference)+nodes_.overhead()+pairs_.overhead()+order_.overhead()+regionPairs_.overhead();
    for(const auto& p:pieces_)bytes+=p.materialMap.capacity()*sizeof(uint32_t)+p.placement.modelKey.capacity();
    if(retainedCharge_)bytes+=sizeof(SceneMemoryCharge)+32;
    return bytes;
}
void BVH::shareMaterialTexels(){
    if(materialBank_||region_)return;
    // ponytail: local material counts are small; compare hashes then exact bytes
    // instead of maintaining another retained texture cache.
    try{
        std::vector<NorthlightMeshPageKey::Key> keys;keys.reserve(scene_.materials.size());
        for(size_t i=0;i<scene_.materials.size();++i){auto& m=scene_.materials[i];const auto& bytes=m.texels();
            keys.push_back(NorthlightMeshPageKey::keyOf(bytes.data(),bytes.size(),nullptr,0));
            if(bytes.empty())continue;
            for(size_t j=0;j<i;++j){auto& previous=scene_.materials[j];
                if(keys[i]!=keys[j]||m.texels()!=previous.texels())continue;
                if(!previous.sharedTexels){auto pixels=geometryBlock<uint8_t>(previous.rgba.size());std::memcpy(pixels.get(),previous.rgba.data(),previous.rgba.size());
                    previous.sharedTexels=std::shared_ptr<const uint8_t>(pixels.release(),GeometryBlockDelete<uint8_t>{});previous.sharedTexelBytes=previous.rgba.size();std::vector<uint8_t>().swap(previous.rgba);}
                m.sharedTexels=previous.sharedTexels;m.sharedTexelBytes=previous.sharedTexelBytes;std::vector<uint8_t>().swap(m.rgba);break;
            }
        }
    }catch(const std::bad_alloc&){/* Optional compaction: every texel remains readable. */}
}
void BVH::accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
    using namespace NorthlightMemoryFootprint;
    if(!memory.visit(this))return;
    memory.add(Metadata,sizeof(*this));scene_.accountMemory(memory);
    nodes_.accountMemory(memory,Acceleration);pairs_.accountMemory(memory,Acceleration);order_.accountMemory(memory,Acceleration);
    memory.vector(Acceleration,regionOrder_);regionPairs_.accountMemory(memory,Acceleration);centers_.accountMemory(memory,Acceleration);
    memory.vector(Metadata,opaque_);memory.vector(Metadata,materialSources_);memory.vector(Metadata,pieces_);
    if(materialBank_&&memory.visit(materialBank_.get())){
        memory.add(Metadata,sizeof(MaterialBank));memory.vector(Metadata,materialBank_->values);
        for(const auto& material:materialBank_->values){if(material.sharedTexels){if(memory.visit(material.sharedTexels.get())){memory.add(Texels,material.texelCapacity());memory.add(Metadata,64);}}else memory.vector(Texels,material.rgba);}
    }
    for(const auto& piece:pieces_){memory.vector(Metadata,piece.materialMap);memory.string(piece.placement.modelKey);piece.geometry->accountMemory(memory);}
}
size_t BVH::retainedBytes()const{
    std::unordered_set<const BVH*> geometry;std::unordered_set<const MaterialBank*> materials;
    const auto bankBytes=[](const MaterialBank& bank){size_t bytes=sizeof(bank)+sizeof(SceneMemoryCharge)+64+bank.values.capacity()*sizeof(WorldMaterial);for(const auto& m:bank.values)bytes+=m.texelCapacity();return bytes;};
    try{
        const auto visit=[&](auto&& self,const BVH& b)->size_t{
            if(!geometry.insert(&b).second)return 0;size_t bytes=b.ownBytes();
            if(b.materialBank_&&materials.insert(b.materialBank_.get()).second)bytes+=bankBytes(*b.materialBank_);
            for(const auto& p:b.pieces_)bytes+=self(self,*p.geometry);return bytes;
        };return visit(visit,*this);
    }catch(...){
        // Diagnostic/retirement accounting must never fail or undercount when
        // optional dedup bookkeeping cannot allocate. Overcount shared owners.
        size_t bytes=ownBytes();if(materialBank_)bytes+=bankBytes(*materialBank_);
        for(const auto& p:pieces_)bytes+=p.geometry->retainedBytes();return bytes;
    }
}
uint32_t BVH::makeRegionNode(uint32_t start,uint32_t count,uint32_t depth){
    const float inf=std::numeric_limits<float>::infinity();Node node;node.low={inf,inf,inf};node.high={-inf,-inf,-inf};Vec3 cl=node.low,ch=node.high;
    for(uint32_t i=start;i<start+count;++i){const auto& p=*pieces_[regionOrder_[i]].geometry;node.low=minimum(node.low,p.rootLow_);node.high=maximum(node.high,p.rootHigh_);const Vec3 center=(p.rootLow_+p.rootHigh_)*.5f;cl=minimum(cl,center);ch=maximum(ch,center);}
    const uint32_t index=uint32_t(nodes_.size());nodes_.push_back(node);
    if(count<=1){nodes_[index].start=start;nodes_[index].count=count;return index;}
    const Vec3 extent=ch-cl;unsigned axis=extent.y>extent.x?1:0;if(extent.z>component(extent,axis))axis=2;
    constexpr unsigned B=16;unsigned bestAxis=0,bestSplit=0;float cost=std::numeric_limits<float>::infinity();
    const auto area=[](Vec3 lo,Vec3 hi){const auto e=hi-lo;return e.x*e.y+e.x*e.z+e.y*e.z;};
    const auto bin=[&](uint32_t index,unsigned ax){const auto& p=*pieces_[index].geometry;return std::min(B-1,unsigned((component((p.rootLow_+p.rootHigh_)*.5f,ax)-component(cl,ax))*(float(B)/component(extent,ax))));};
    if(depth<48&&count>2)for(unsigned ax=0;ax<3;++ax){if(component(extent,ax)<=1e-10f)continue;
        struct Bin{Vec3 lo,hi;unsigned count=0;};std::array<Bin,B> bins;for(auto& b:bins){b.lo={inf,inf,inf};b.hi={-inf,-inf,-inf};}
        for(uint32_t i=start;i<start+count;++i){const auto index=regionOrder_[i];const auto& p=*pieces_[index].geometry;auto& b=bins[bin(index,ax)];++b.count;b.lo=minimum(b.lo,p.rootLow_);b.hi=maximum(b.hi,p.rootHigh_);}
        std::array<float,B> right;Vec3 lo={inf,inf,inf},hi={-inf,-inf,-inf};unsigned n=0;for(unsigned i=B;i-->0;){n+=bins[i].count;lo=minimum(lo,bins[i].lo);hi=maximum(hi,bins[i].hi);right[i]=n?area(lo,hi)*n:0;}
        lo={inf,inf,inf};hi={-inf,-inf,-inf};n=0;for(unsigned i=0;i<B-1;++i){n+=bins[i].count;lo=minimum(lo,bins[i].lo);hi=maximum(hi,bins[i].hi);if(!n||n==count)continue;const float candidate=area(lo,hi)*n+right[i+1];if(candidate<cost){cost=candidate;bestAxis=ax;bestSplit=i+1;}}
    }
    uint32_t mid=start;
    if(std::isfinite(cost)){auto split=std::partition(regionOrder_.begin()+start,regionOrder_.begin()+start+count,[&](uint32_t i){return bin(i,bestAxis)<bestSplit;});mid=uint32_t(split-regionOrder_.begin());}
    if(mid==start||mid==start+count){mid=start+count/2;std::nth_element(regionOrder_.begin()+start,regionOrder_.begin()+mid,regionOrder_.begin()+start+count,[&](uint32_t a,uint32_t b){const auto& x=*pieces_[a].geometry;const auto& y=*pieces_[b].geometry;return component((x.rootLow_+x.rootHigh_)*.5f,axis)<component((y.rootLow_+y.rootHigh_)*.5f,axis);});}
    const auto left=makeRegionNode(start,mid-start,depth+1),right=makeRegionNode(mid,start+count-mid,depth+1);nodes_[index].left=left;nodes_[index].right=right;return index;
}
bool BVH::buildRegion(WorldScene&& scene,std::vector<PieceReference>&& pieces,std::string& error,const AllocationAdmission& admit,const std::shared_ptr<SceneMemoryBudget>&){
    if(!validateBVHScene(scene,error))return false;
    const uint64_t metadata=uint64_t(pieces.size())*(2*sizeof(Node)+sizeof(Pair)+sizeof(uint32_t));
    if(admit&&!admit(metadata)){error="Shared region allocation deferred";return false;}
    BVH next;next.scene_=std::move(scene);next.pieces_=std::move(pieces);next.region_=next.sah_=true;
    try{
        if(next.scene_.vertices.chunked()||next.scene_.triangles.chunked()){next.order_.chunks(&admit);next.pairs_.chunks(&admit);next.nodes_.chunks(&admit);next.regionPairs_.chunks(&admit);next.centers_.chunks(&admit);}
        next.regionOrder_.reserve(next.pieces_.size());next.nodes_.reserve(next.pieces_.size()*2);
        for(uint32_t i=0;i<next.pieces_.size();++i){const auto& p=next.pieces_[i];
            if(!p.geometry||!p.geometry->sah_||p.geometry->region_||uint64_t(p.triangleBase)+p.geometry->scene().triangles.size()>next.scene_.triangles.size()){
                error="Invalid shared region piece";return false;
            }
            next.regionTriangles_+=p.geometry->triangleCount();if(p.geometry->triangleCount())next.regionOrder_.push_back(i);
        }
        if(!next.regionOrder_.empty()){
            next.makeRegionNode(0,uint32_t(next.regionOrder_.size()));next.regionLow_=next.nodes_[0].low;next.regionHigh_=next.nodes_[0].high;
            next.regionPairs_.reserve(next.nodes_.size()/2);
            const auto pack=[&](auto&& self,uint32_t index)->uint32_t{const auto& n=next.nodes_[index];if(n.count)return LeafBit|(n.start<<3)|n.count;
                const auto pair=uint32_t(next.regionPairs_.size());next.regionPairs_.push_back(Pair{});const auto l=self(self,n.left),r=self(self,n.right);auto& p=next.regionPairs_[pair];p.child[0]=l;p.child[1]=r;
                const auto& a=next.nodes_[n.left];const auto& b=next.nodes_[n.right];for(unsigned axis=0;axis<3;++axis){p.axis[axis][0]=component(a.low,axis);p.axis[axis][1]=component(b.low,axis);p.axis[axis][2]=component(a.high,axis);p.axis[axis][3]=component(b.high,axis);}return pair;};
            next.regionRoot_=pack(pack,0);next.nodes_.free();
        }
        // The compatibility scene already owns every triangle. Unadmitted
        // ranges use its original global IDs and one mandatory pair tree;
        // neither geometry nor textures are copied into another fallback scene.
        uint64_t fallbackCount=next.scene_.triangles.size();for(const auto& p:next.pieces_)fallbackCount-=p.geometry->scene().triangles.size();uint32_t first=0;if(!next.order_.chunked()&&admit&&!admit(fallbackCount*sizeof(uint32_t))){error="Hybrid index allocation deferred";return false;}next.order_.reserve(fallbackCount);
        const auto append=[&](uint32_t end){for(;first<end;++first){const auto& t=next.scene_.triangles[first];const Vec3 n=cross(next.scene_.vertices[t.v1].position-next.scene_.vertices[t.v0].position,next.scene_.vertices[t.v2].position-next.scene_.vertices[t.v0].position);if(dot(n,n)>1e-16f)next.order_.push_back(first);}};
        for(const auto& p:next.pieces_){if(p.triangleBase<first){error="Unordered shared region pieces";return false;}append(p.triangleBase);first=p.triangleBase+uint32_t(p.geometry->scene().triangles.size());}
        append(uint32_t(next.scene_.triangles.size()));next.regionTriangles_+=next.order_.size();
        if(!next.order_.empty()){
            const uint64_t tree=next.order_.capacity()*sizeof(uint32_t)+uint64_t(double(next.order_.size())*TraceSahPairsPerTriangle)*sizeof(Pair);
            if(!next.order_.chunked()&&admit&&(!admit(tree)||!admit(buildScratchBytes(next.scene_.triangles.size())))){error="Hybrid fallback allocation deferred";return false;}
            next.buildSahTree();next.opaque_.resize(next.scene_.materials.size());for(size_t i=0;i<next.opaque_.size();++i)next.opaque_[i]=alphaNeverCut(next.material(i));
        }else next.order_.free();
        // Region acceleration replaces the mandatory full-scene tree. Its
        // actual storage is admitted and retirement-accounted, not retained in
        // the optional piece cache; a full cache cannot force a region restart.
    }catch(...){error="Cannot allocate shared region TLAS";return false;}
    next.scene_.vertices.detachAdmission();next.scene_.triangles.detachAdmission();next.order_.detachAdmission();next.pairs_.detachAdmission();next.nodes_.detachAdmission();next.regionPairs_.detachAdmission();next.centers_.detachAdmission();*this=std::move(next);return true;
}
template<bool AnyHit> RayHit BVH::traceRegion(Vec3 origin,Vec3 direction,float minT,float maxT)const{
    RayHit best;best.distance=maxT;direction=normalized(direction);
    if((regionOrder_.empty()&&order_.empty())||!finite(origin)||dot(direction,direction)<.5f||!std::isfinite(minT)||!std::isfinite(maxT)||minT<0||maxT<=minT)return best;
    if(regionOrder_.empty())return traceImpl<AnyHit,true>(origin,direction,minT,maxT,true);
    const PairRay<true> ray(origin,direction);
    float bestBarycentric[2]={};const auto finish=[&](){if constexpr(!AnyHit)if(best.hit)shadeHit(best,origin,direction,best.triangle,bestBarycentric[0],bestBarycentric[1],best.distance);return best;};
    if(!order_.empty())best=traceImpl<AnyHit,true>(origin,direction,minT,maxT,true,-1,bestBarycentric,&ray);
    if constexpr(AnyHit){if(best.hit)return best;}
    float boxUpper=traversalUpper(best.distance);
    struct Item{uint32_t node;float entry;};Item stack[128];unsigned size=0;float entry;
    if(!intersectsBox(origin,direction,regionLow_,regionHigh_,minT,boxUpper,entry))return finish();stack[size++]={regionRoot_,entry};
    while(size){const auto item=stack[--size];if(item.entry>boxUpper)continue;
        if(item.node&LeafBit){const auto start=(item.node&~LeafBit)>>3,count=item.node&7;
            for(uint32_t i=start;i<start+count;++i){const auto& p=pieces_[regionOrder_[i]];
                // Normalize once; one ulp keeps exact-distance global-ID ties.
                const float bound=best.hit?std::min(maxT,std::nextafter(best.distance,std::numeric_limits<float>::infinity())):maxT;
                float barycentric[2];auto hit=p.geometry->traceImpl<AnyHit,true>(origin,direction,minT,bound,true,item.entry,barycentric,&ray);if(!hit.hit)continue;hit.triangle+=p.triangleBase;
                if constexpr(AnyHit)return hit;
                if(!best.hit||hit.distance<best.distance||(hit.distance==best.distance&&hit.triangle<best.triangle)){if(hit.distance!=best.distance)boxUpper=traversalUpper(hit.distance);best=hit;bestBarycentric[0]=barycentric[0];bestBarycentric[1]=barycentric[1];}
            }
        }else{const auto& p=regionPairs_[item.node];float left,right;const auto mask=ray.test(p.axis,minT,boxUpper,left,right);
            if(mask==3){if(left<right){stack[size++]={p.child[1],right};stack[size++]={p.child[0],left};}else{stack[size++]={p.child[0],left};stack[size++]={p.child[1],right};}}
            else if(mask==1)stack[size++]={p.child[0],left};else if(mask==2)stack[size++]={p.child[1],right};
        }
    }return finish();
}
// Both queries share one kernel per tree, so occluded(o,d,t,m)==trace(o,d,m,t).hit.
RayHit BVH::trace(Vec3 origin,Vec3 direction,float minT,float maxT)const {if(region_)return traceRegion<false>(origin,direction,minT,maxT);return sah_?traceImpl<false,true>(origin,direction,minT,maxT):traceImpl<false,false>(origin,direction,minT,maxT);}
bool BVH::occluded(Vec3 origin,Vec3 direction,float maxT,float minT) const {if(region_)return traceRegion<true>(origin,direction,minT,maxT).hit;return (sah_?traceImpl<true,true>(origin,direction,minT,maxT):traceImpl<true,false>(origin,direction,minT,maxT)).hit;}

namespace {

bool containsBounds(Vec3 lo,Vec3 hi,Vec3 a,Vec3 b){return lo.x<=a.x&&lo.y<=a.y&&lo.z<=a.z&&hi.x>=b.x&&hi.y>=b.y&&hi.z>=b.z;}
bool sameVector(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
std::string pieceKey(const std::string& model,const Instance& instance){
    std::string key=model;key.append(reinterpret_cast<const char*>(&instance.category),sizeof(instance.category));key.append(reinterpret_cast<const char*>(&instance.uid),sizeof(instance.uid));key.append(reinterpret_cast<const char*>(instance.matrix),sizeof(instance.matrix));key.append(reinterpret_cast<const char*>(&instance.translation),sizeof(instance.translation));return key;
}
uint64_t sceneBytes(const WorldScene& s){return s.retainedBytes()-sizeof(s); }
using CacheClock=std::chrono::steady_clock;
double milliseconds(CacheClock::time_point a){return std::chrono::duration<double,std::milli>(CacheClock::now()-a).count();}
}

struct LocalSceneCache::Impl {
    struct Piece {
        WorldScene scene;
        std::shared_ptr<const BVH> geometry;uint64_t sourceIdentity=0,membership[2]={};
        const WorldScene& transport()const{return geometry?geometry->scene():scene;}
        std::vector<uint32_t> materialSources;
        std::vector<TerrainRun> terrainRuns;std::string parentKey;
        Vec3 cropLow,cropHigh,low,high;
        bool complete=false;
    };
    Limits limits;Stats stats;std::string map,directory;bool accelerated=false;
    std::shared_ptr<SceneMemoryBudget> budget;
    std::map<std::string,std::shared_ptr<Piece>> pieces,chunks;
    // Size of the last assembled flat scene and its region: consecutive regions overlap heavily, so it
    // sizes the next vertex/triangle block once instead of a doubling chain (old+new coexisting).
    bool sized=false;uint64_t sizedVertices=0,sizedTriangles=0;Vec3 sizedLow,sizedHigh;
    explicit Impl(Limits l,std::shared_ptr<SceneMemoryBudget> shared={}):limits(l),budget(shared?std::move(shared):std::make_shared<SceneMemoryBudget>(l.retainedBytes)){}
};
LocalSceneCache::LocalSceneCache():LocalSceneCache(Limits{}){}
LocalSceneCache::LocalSceneCache(Limits limits):impl_(new Impl(limits)){}
LocalSceneCache::~LocalSceneCache()=default;
void LocalSceneCache::reset(){auto limits=impl_->limits;auto budget=impl_->budget;impl_.reset(new Impl(limits,std::move(budget)));}
const LocalSceneCache::Stats& LocalSceneCache::stats()const{return impl_->stats;}
void LocalSceneCache::accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
    using namespace NorthlightMemoryFootprint;
    if(!memory.visit(impl_.get()))return;
    const auto& cache=*impl_;memory.add(Metadata,sizeof(Impl));memory.string(cache.map);memory.string(cache.directory);
    const auto account=[&](const auto& entries){for(const auto& entry:entries){
        memory.allocation(Metadata,&entry,sizeof(entry));memory.string(entry.first);
        if(!memory.visit(entry.second.get()))continue;
        const auto& piece=*entry.second;memory.add(Metadata,sizeof(piece));piece.scene.accountMemory(memory);
        memory.vector(Metadata,piece.materialSources);memory.vector(Metadata,piece.terrainRuns);memory.string(piece.parentKey);
        if(piece.geometry)piece.geometry->accountMemory(memory);
    }};
    account(cache.pieces);account(cache.chunks);
}

bool LocalSceneCache::build(const std::vector<std::string>& files,const std::string& directory,const std::string& map,Vec3 low,Vec3 high,BVH& output,std::string& error,const AllocationAdmission& admit,bool fast,const SceneSourceProvider& sourceProvider,const SceneSourceRevision& sourceRevision,bool chunked){
    const auto begin=CacheClock::now();error.clear();
    if(files.size()>64||!finite(low)||!finite(high)||low.x>high.x||low.y>high.y||low.z>high.z){error="Invalid incremental FGS3 region or tile count";return false;}
    try{
    const bool accelerate=TraceSah&&fast&&bool(sourceProvider);
    if(impl_->map!=map||impl_->directory!=directory||impl_->accelerated!=accelerate){reset();impl_->map=map;impl_->directory=directory;impl_->accelerated=accelerate;}
    auto& cache=*impl_;cache.stats=Stats{};auto& stats=cache.stats;
    auto allowed=[&](uint64_t bytes){if(!admit||admit(bytes))return true;error="Geometry allocation deferred";return false;};
    auto pieceBytes=[](const Impl::Piece& p){return uint64_t(sizeof(Impl::Piece)+(p.geometry?0:sceneBytes(p.scene))+p.materialSources.capacity()*4+p.terrainRuns.capacity()*sizeof(TerrainRun)+p.parentKey.capacity());};
    auto cacheBytes=[&](){uint64_t bytes=cache.budget->used.load(std::memory_order_relaxed);if(accelerate)return bytes;for(const auto& entry:cache.pieces)bytes+=pieceBytes(*entry.second)+entry.first.capacity()+96;return bytes;};
    auto trimCache=[&](){if(accelerate)return;for(auto it=cache.pieces.begin();it!=cache.pieces.end()&&stats.retainedBytes>cache.limits.retainedBytes;){stats.retainedBytes-=pieceBytes(*it->second)+it->first.capacity()+96;it=cache.pieces.erase(it);}};

    struct AccelerationUnavailable {};
    try {
        InstanceGroups groups;if(!selectInstances(files,low,high,15,groups,error))return false;
        std::set<std::string> wanted;for(const auto& group:groups)for(const auto& instance:group.second)wanted.insert(pieceKey(group.first,instance));
        // Keep only useful pieces. Published flat snapshots independently own
        // their geometry; cache eviction cannot invalidate a GI solve.
        for(auto it=cache.pieces.begin();it!=cache.pieces.end();)if(!wanted.count(it->first))it=cache.pieces.erase(it);else++it;
        for(auto it=cache.chunks.begin();it!=cache.chunks.end();)if(!wanted.count(it->second->parentKey))it=cache.chunks.erase(it);else++it;
        stats.retainedBytes=cacheBytes();trimCache();
        WorldScene next;if(chunked){next.vertices.chunks(&admit);next.triangles.chunks(&admit);}std::vector<BVH::PieceReference> references;if(sourceProvider){if(!allowed(uint64_t(wanted.size())*sizeof(SourcePieceRange)))return false;next.sourcePieces.reserve(wanted.size());}if(accelerate){if(!allowed(uint64_t(wanted.size())*sizeof(BVH::PieceReference)))return false;references.reserve(wanted.size());}
        uint64_t textureBytes=0;
        // Previous build's counts (+1/16) as the one up-front block when the regions overlap (IoU >= 1/2);
        // beyond that, growth is +1/4 rather than +1/2. A denied hint falls back to normal growth, error unset.
        bool hinted=false;
        if(!chunked&&cache.sized){
            if(regionsOverlapHalf(low,high,cache.sizedLow,cache.sizedHigh)){
                const uint64_t v=std::min<uint64_t>(4000000,cache.sizedVertices+cache.sizedVertices/16),t=std::min<uint64_t>(6000000,cache.sizedTriangles+cache.sizedTriangles/16);
                if(v&&t&&(!admit||(admit(v*sizeof(WorldVertex))&&admit(t*sizeof(WorldTriangle))))){next.vertices.reserve(v);next.triangles.reserve(t);hinted=true;++stats.hintedBuilds;}
            }
        }
        auto reserve=[&](auto& values,size_t count){if(values.capacity()>=count)return true;using Buffer=std::decay_t<decltype(values)>;using T=typename Buffer::value_type;if constexpr(std::is_same_v<Buffer,AdaptiveGeometryArray<WorldVertex>>||std::is_same_v<Buffer,AdaptiveGeometryArray<WorldTriangle>>)if(values.chunked())return true;size_t capacity=std::max(count,std::max<size_t>(64,hinted?values.capacity()+values.capacity()/4:values.capacity()+values.capacity()/2));if(!allowed(uint64_t(capacity)*sizeof(T)))return false;values.reserve(capacity);return true;};
        for(const auto& group:groups){
            WorldScene loaded;std::shared_ptr<const WorldScene> sharedSource;auto stamp=CacheClock::now();std::string path=directory;if(!path.empty()&&path.back()!='/'&&path.back()!='\\')path+='/';path+=group.first+".fgs";
            bool geometryNeeded=false;
            for(const auto& instance:group.second){auto old=cache.pieces.find(pieceKey(group.first,instance));if(old==cache.pieces.end()){geometryNeeded=true;break;}const auto& p=*old->second;if(!((sameVector(p.cropLow,low)&&sameVector(p.cropHigh,high))||(p.complete&&containsBounds(low,high,p.low,p.high)))){geometryNeeded=true;break;}}
            // Reuse a proven source revision and immutable material bank even
            // after the decoded source LRU evicts its raw model geometry.
            std::shared_ptr<const BVH::MaterialBank> warmBank;uint64_t sourceIdentity=0;
            if(!geometryNeeded&&accelerate&&sourceRevision){sourceIdentity=sourceRevision(path);if(sourceIdentity){for(const auto& instance:group.second){const auto& p=*cache.pieces.find(pieceKey(group.first,instance))->second;if(p.sourceIdentity!=sourceIdentity||!p.geometry){warmBank.reset();break;}warmBank=p.geometry->materialBank_;}}}
            if(!warmBank){
                if(sourceProvider){sharedSource=sourceProvider(path,error,admit);if(!sharedSource&&!error.empty())return false;}
                if(!sharedSource&&!readScene(path.c_str(),loaded,error,admit,!geometryNeeded&&!sourceProvider,chunked&&!cache.limits.retainedBytes&&!sourceProvider)){error="Incremental FGS3 model "+group.first+": "+error;return false;}
                sourceIdentity=(sharedSource?*sharedSource:loaded).sourceIdentity;++stats.modelReads;
            }
            // Pressure candidates have no retained piece/source cache. Share
            // decoded texels before copying placement materials, so duplicates
            // never raise the allocator's address-space high-water mark.
            if(chunked&&!cache.limits.retainedBytes&&!sharedSource){
                for(size_t i=0;i<loaded.materials.size();++i){auto& m=loaded.materials[i];if(m.texels().empty())continue;
                    const WorldMaterial* identical=nullptr;
                    for(const auto& previous:next.materials)if(previous.sharedTexels&&m.texels()==previous.texels()){identical=&previous;break;}
                    if(!identical)for(size_t j=0;j<i;++j)if(loaded.materials[j].sharedTexels&&m.texels()==loaded.materials[j].texels()){identical=&loaded.materials[j];break;}
                    if(identical){m.sharedTexels=identical->sharedTexels;m.sharedTexelBytes=identical->sharedTexelBytes;std::vector<uint8_t>().swap(m.rgba);}
                }
            }
            const WorldScene& source=sharedSource?*sharedSource:loaded;const auto& materials=warmBank?warmBank->values:source.materials;
            stats.loadMs+=milliseconds(stamp);
            // Drop obsolete cache-only generations before reserving their
            // replacements. Published immutable owners keep their own leases.
            if(accelerate)for(const auto& instance:group.second){auto old=cache.pieces.find(pieceKey(group.first,instance));if(old==cache.pieces.end())continue;const auto& p=*old->second;if(p.sourceIdentity!=sourceIdentity||!((sameVector(p.cropLow,low)&&sameVector(p.cropHigh,high))||(p.complete&&containsBounds(low,high,p.low,p.high))))cache.pieces.erase(old);}
            std::shared_ptr<const BVH::MaterialBank> bank=warmBank;
            if(accelerate&&sourceIdentity&&!bank){
                for(auto it=cache.chunks.begin();it!=cache.chunks.end();){const auto& p=*it->second;if(p.parentKey.compare(0,group.first.size(),group.first)==0){if(p.sourceIdentity!=sourceIdentity){it=cache.chunks.erase(it);continue;}if(!bank)bank=p.geometry->materialBank_;}++it;}
                for(const auto& instance:group.second){auto old=cache.pieces.find(pieceKey(group.first,instance));if(old!=cache.pieces.end()&&old->second->sourceIdentity==sourceIdentity&&old->second->geometry){bank=old->second->geometry->materialBank_;break;}}
                if(!bank){uint64_t required=uint64_t(materials.size())*sizeof(WorldMaterial);for(const auto& material:materials)required+=material.texels().size();if(allowed(required)&&required<=cache.budget->limit-cache.budget->used.load(std::memory_order_relaxed)){
                    auto nextBank=std::make_shared<BVH::MaterialBank>();nextBank->values=materials;uint64_t bytes=sizeof(BVH::MaterialBank)+sizeof(SceneMemoryCharge)+64+nextBank->values.capacity()*sizeof(WorldMaterial);
                    for(auto& material:nextBank->values){material.albedo=reflectance(material.albedo);bytes+=material.texelCapacity();}
                    nextBank->charge=SceneMemoryCharge::acquire(cache.budget,bytes);if(nextBank->charge)bank=std::move(nextBank);
                    }else error.clear();
                }
            }
            std::vector<uint32_t> materialRemap[3];for(auto& remap:materialRemap)remap.assign(materials.size(),UINT32_MAX);
            uint64_t decodedBytes=sceneBytes(source),largestPiece=0;for(const auto& material:materials)decodedBytes+=sizeof(WorldMaterial)+material.texelCapacity();
            for(const auto& instance:group.second){
                auto key=pieceKey(group.first,instance);auto old=cache.pieces.find(key);std::shared_ptr<Impl::Piece> piece;
                if(old!=cache.pieces.end()){
                    const auto& p=*old->second;
                    if((!sourceProvider||p.sourceIdentity==sourceIdentity)&&((sameVector(p.cropLow,low)&&sameVector(p.cropHigh,high))||(p.complete&&containsBounds(low,high,p.low,p.high)))){piece=old->second;++stats.pieceReuses;stats.reusedTriangles+=piece->transport().triangles.size();if(accelerate){++stats.blasReuses;stats.blasTrianglesReused+=piece->geometry->triangleCount();}}
                }
                if(!piece){
                    stamp=CacheClock::now();piece=std::make_shared<Impl::Piece>();piece->cropLow=low;piece->cropHigh=high;piece->sourceIdentity=sourceIdentity;
                    WorldScene cropped;if(chunked&&source.vertices.size()*sizeof(WorldVertex)>4u*1024u*1024u)cropped.vertices.chunks(&admit);if(chunked&&source.triangles.size()*sizeof(WorldTriangle)>4u*1024u*1024u)cropped.triangles.chunks(&admit);std::vector<uint32_t> remap(source.materials.size(),UINT32_MAX);uint64_t bytes=0;
                    if(!appendInstance(source,instance,low,high,cropped,remap,bytes,error,admit,{},0,0,false,piece->membership,accelerate&&instance.terrain?&piece->terrainRuns:nullptr,true,&group.first))return false;
                    piece->complete=!source.triangles.empty()&&cropped.triangles.size()==source.triangles.size();
                    piece->materialSources.resize(cropped.materials.size());for(uint32_t i=0;i<remap.size();++i)if(remap[i]!=UINT32_MAX)piece->materialSources[remap[i]]=i;
                    const float inf=std::numeric_limits<float>::infinity();piece->low={inf,inf,inf};piece->high={-inf,-inf,-inf};
                    for(const auto& vertex:cropped.vertices){piece->low=minimum(piece->low,vertex.position);piece->high=maximum(piece->high,vertex.position);}
                    if(accelerate&&bank&&!instance.terrain){
                        const uint64_t triangles=cropped.triangles.size();
                        const uint64_t tree=triangles*sizeof(uint32_t)+uint64_t(double(triangles)*TraceSahPairsPerTriangle)*sizeof(BVH::Pair);
                        const uint64_t estimate=cropped.retainedBytes()+sizeof(BVH)+tree+sizeof(Impl::Piece)+piece->materialSources.capacity()*4+key.capacity()+192;
                        if(estimate<=cache.budget->limit-cache.budget->used.load(std::memory_order_relaxed)&&allowed(tree)&&allowed(BVH::buildScratchBytes(triangles))){
                            auto geometry=std::make_shared<BVH>();if(!geometry->buildImpl(std::move(cropped),error,true,bank,piece->materialSources,admit))return false;
                            const auto charge=geometry->ownBytes()+sizeof(SceneMemoryCharge)+32+sizeof(Impl::Piece)+piece->materialSources.capacity()*4+key.capacity()+96;
                            geometry->retainedCharge_=SceneMemoryCharge::acquire(cache.budget,charge);
                            if(geometry->retainedCharge_){piece->geometry=std::move(geometry);piece->sourceIdentity=sourceIdentity;++stats.blasBuilds;stats.blasTrianglesBuilt+=piece->geometry->triangleCount();}
                            else cropped=std::move(geometry->scene_);
                        }else error.clear();
                    }
                    if(!piece->geometry){cropped.vertices.detachAdmission();cropped.triangles.detachAdmission();piece->scene=std::move(cropped);}
                    ++stats.pieceBuilds;stats.builtTriangles+=piece->transport().triangles.size();stats.pieceMs+=milliseconds(stamp);
                    // Cropped boundary pieces change on the next movement; keep
                    // the complete interior pieces that can actually be reused.
                    if((piece->complete||accelerate)&&(!accelerate||piece->geometry)&&pieceBytes(*piece)<=cache.limits.retainedBytes){if(old!=cache.pieces.end())stats.retainedBytes-=pieceBytes(*old->second)+old->first.capacity()+96;cache.pieces[key]=piece;const auto& inserted=*cache.pieces.find(key);stats.retainedBytes+=pieceBytes(*piece)+inserted.first.capacity()+96;trimCache();}
                }
                stamp=CacheClock::now();const auto& p=*piece;const auto& scene=p.transport();
                auto& remap=materialRemap[instance.terrain?1:instance.wmo?2:0];uint32_t vertexBase=uint32_t(next.vertices.size()),triangleBase=uint32_t(next.triangles.size());
                for(size_t i=0;i<materials.size();++i)if(materials[i].lampLocalId)remap[i]=UINT32_MAX;
                if(uint64_t(vertexBase)+scene.vertices.size()>4000000||uint64_t(triangleBase)+scene.triangles.size()>6000000){error="FGS3 local region geometry limit exceeded";return false;}
                if(!reserve(next.vertices,vertexBase+scene.vertices.size())||!reserve(next.triangles,triangleBase+scene.triangles.size()))return false;
                next.vertices.insert(next.vertices.end(),scene.vertices.begin(),scene.vertices.end());
                for(const auto& triangle:scene.triangles){auto t=triangle;uint32_t original=p.materialSources[t.material];
                    if(remap[original]==UINT32_MAX){const auto& m=materials[original];if(next.materials.size()>=65536||textureBytes+m.texels().size()>256ull*1024*1024){error="FGS3 local region material limit exceeded";return false;}if(!reserve(next.materials,next.materials.size()+1)||!allowed(m.texels().size()))return false;remap[original]=uint32_t(next.materials.size());next.materials.push_back(m);auto& dest=next.materials.back();dest.terrain=instance.terrain;dest.wmo=instance.wmo;dest.lampSourceId=m.lampLocalId?lampIdentity(group.first,instance,m.lampLocalId):scene.materials[t.material].lampSourceId;textureBytes+=m.texels().size();}
                    t.v0+=vertexBase;t.v1+=vertexBase;t.v2+=vertexBase;t.material=remap[original];next.triangles.push_back(t);
                }
                if(p.sourceIdentity&&!scene.triangles.empty()){
                    SourcePieceRange range;range.sourceIdentity=p.sourceIdentity;range.membership[0]=p.membership[0];range.membership[1]=p.membership[1];range.vertexBase=vertexBase;range.vertexCount=uint32_t(scene.vertices.size());range.triangleBase=triangleBase;range.triangleCount=uint32_t(scene.triangles.size());range.complete=p.complete;
                    if(!allowed(uint64_t(remap.size())*sizeof(uint32_t)))return false;range.materialMap=remap;
                    auto& owner=range.placement;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;next.sourcePieces.push_back(std::move(range));
                }
                if(accelerate&&p.geometry&&!scene.triangles.empty()){
                    BVH::PieceReference reference;reference.geometry=p.geometry;reference.vertexBase=vertexBase;reference.triangleBase=triangleBase;reference.complete=p.complete;reference.materialMap.reserve(p.materialSources.size());for(auto original:p.materialSources)reference.materialMap.push_back(remap[original]);
                    auto& owner=reference.placement;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;references.push_back(std::move(reference));
                }
                if(accelerate&&instance.terrain&&bank&&!p.terrainRuns.empty())try{
                    const auto chunkKey=[&](const TerrainRun& run){std::string id=key;id.append(reinterpret_cast<const char*>(&sourceIdentity),sizeof(sourceIdentity));id.append(reinterpret_cast<const char*>(&run.x),sizeof(run.x));id.append(reinterpret_cast<const char*>(&run.y),sizeof(run.y));id.append(reinterpret_cast<const char*>(run.membership),sizeof(run.membership));return id;};
                    std::set<std::string> current;for(const auto& run:p.terrainRuns)if(run.complete)current.insert(chunkKey(run));
                    for(auto it=cache.chunks.begin();it!=cache.chunks.end();)if(it->second->parentKey==key&&!current.count(it->first))it=cache.chunks.erase(it);else++it;
                    size_t maxVertices=0;for(const auto& run:p.terrainRuns)if(run.complete)maxVertices=std::max(maxVertices,std::min<size_t>(scene.vertices.size(),size_t(run.count)*3));
                    if(!allowed(uint64_t(scene.vertices.size()+maxVertices)*sizeof(uint32_t)))throw std::bad_alloc();std::vector<uint32_t> vertexRemap(scene.vertices.size(),UINT32_MAX),touched;touched.reserve(maxVertices);
                    for(const auto& run:p.terrainRuns){if(!run.complete)continue;try{const auto id=chunkKey(run);std::shared_ptr<Impl::Piece> chunk;
                        auto found=cache.chunks.find(id);if(found!=cache.chunks.end()){chunk=found->second;++stats.chunkReuses;stats.chunkTrianglesReused+=chunk->geometry->triangleCount();}
                        if(!chunk){
                            const uint64_t temporary=uint64_t(run.count)*(3*sizeof(WorldVertex)+sizeof(WorldTriangle))+uint64_t(scene.materials.size())*sizeof(uint32_t)+uint64_t(std::min<size_t>(scene.materials.size(),run.count))*(sizeof(WorldMaterial)+sizeof(uint32_t));if(!allowed(temporary)){error.clear();continue;}
                            auto candidate=std::make_shared<Impl::Piece>();candidate->parentKey=key;candidate->sourceIdentity=sourceIdentity;candidate->complete=true;WorldScene transport;if(chunked&&uint64_t(run.count)*3*sizeof(WorldVertex)>4u*1024u*1024u)transport.vertices.chunks(&admit);if(chunked&&uint64_t(run.count)*sizeof(WorldTriangle)>4u*1024u*1024u)transport.triangles.chunks(&admit);transport.vertices.reserve(std::min<size_t>(scene.vertices.size(),size_t(run.count)*3));transport.triangles.reserve(run.count);std::vector<uint32_t> materialRemap(scene.materials.size(),UINT32_MAX);
                            for(uint32_t i=run.first;i<run.first+run.count;++i){auto t=scene.triangles[i];uint32_t* ids[]={&t.v0,&t.v1,&t.v2};for(auto index:ids){const auto original=*index;if(vertexRemap[original]==UINT32_MAX){vertexRemap[original]=uint32_t(transport.vertices.size());transport.vertices.push_back(scene.vertices[original]);touched.push_back(original);}*index=vertexRemap[original];}
                                if(materialRemap[t.material]==UINT32_MAX){materialRemap[t.material]=uint32_t(transport.materials.size());candidate->materialSources.push_back(p.materialSources[t.material]);transport.materials.push_back(scene.materials[t.material]);}t.material=materialRemap[t.material];transport.triangles.push_back(t);
                            }
                            for(auto index:touched)vertexRemap[index]=UINT32_MAX;touched.clear();
                            const auto tree=uint64_t(run.count)*sizeof(uint32_t)+uint64_t(double(run.count)*TraceSahPairsPerTriangle)*sizeof(BVH::Pair);
                            const auto estimate=transport.retainedBytes()+sizeof(BVH)+tree+pieceBytes(*candidate)+id.capacity()+192;
                            if(estimate>cache.budget->limit-cache.budget->used.load(std::memory_order_relaxed)||!allowed(tree)||!allowed(BVH::buildScratchBytes(run.count))){error.clear();continue;}
                            auto geometry=std::make_shared<BVH>();if(!geometry->buildImpl(std::move(transport),error,true,bank,candidate->materialSources,admit)){error.clear();continue;}
                            const auto charge=geometry->ownBytes()+sizeof(SceneMemoryCharge)+32+pieceBytes(*candidate)+id.capacity()+96;
                            geometry->retainedCharge_=SceneMemoryCharge::acquire(cache.budget,charge);if(!geometry->retainedCharge_)continue;candidate->geometry=std::move(geometry);chunk=std::move(candidate);cache.chunks[id]=chunk;++stats.chunkBuilds;stats.chunkTrianglesBuilt+=chunk->geometry->triangleCount();
                        }
                        const uint64_t referenceBytes=uint64_t(chunk->materialSources.size())*sizeof(uint32_t);
                        if(!allowed(referenceBytes)||!reserve(references,references.size()+1)){error.clear();continue;}
                        BVH::PieceReference reference;reference.geometry=chunk->geometry;reference.triangleBase=triangleBase+run.first;reference.vertexBase=vertexBase;reference.materialMap.reserve(chunk->materialSources.size());for(auto original:chunk->materialSources)reference.materialMap.push_back(remap[original]);
                        auto& owner=reference.placement;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;references.push_back(std::move(reference));
                    }catch(const std::bad_alloc&){for(auto index:touched)vertexRemap[index]=UINT32_MAX;touched.clear();error.clear();}}
                }catch(const std::bad_alloc&){error.clear();}
                if(p.complete&&!instance.terrain){WorldPlacementCoverage owner;owner.uid=instance.uid;owner.category=instance.category;owner.modelKey=group.first;std::copy(instance.matrix,instance.matrix+9,owner.matrix);owner.translation=instance.translation;owner.low=instance.low;owner.high=instance.high;next.completePlacements.push_back(std::move(owner));}
                stats.assembleMs+=milliseconds(stamp);
                largestPiece=std::max(largestPiece,pieceBytes(p));
            }
            // Working-set estimate excludes old published generations and
            // allocator/transient vector-growth overhead. Admission observes
            // actual process memory independently before every large reserve.
            stats.peakBytes=std::max(stats.peakBytes,stats.retainedBytes+sceneBytes(next)+decodedBytes+largestPiece);
        }
        cache.sized=true;cache.sizedVertices=next.vertices.size();cache.sizedTriangles=next.triangles.size();cache.sizedLow=low;cache.sizedHigh=high;
        // Cold city builds can leave ~38 MiB of vector growth slack in the
        // published GI scene. Compact only large waste, before tree/terrain
        // allocations overlap it. A denied optional copy keeps the old data.
        auto compact=[&](auto& values){
            using Buffer=std::decay_t<decltype(values)>;using T=typename Buffer::value_type;
            const uint64_t bytes=values.size()*sizeof(T),slack=(values.capacity()-values.size())*sizeof(T);
            if(values.chunked()||slack<4u*1024u*1024u||values.capacity()<=values.size()+values.size()/4||(admit&&!admit(bytes)))return;
            try{Buffer exact(values.begin(),values.end());values.swap(exact);}
            catch(const std::bad_alloc&){/* optional compaction: keep the complete scene */}
        };
        compact(next.vertices);compact(next.triangles);
        stats.flatBytes=sceneBytes(next);auto stamp=CacheClock::now();
        // Preflight the tree allocations: triangle order, nodes (the median
        // Node reserve can grow once) and SAH build-time centroids.
        const bool sah=TraceSah&&fast;
        const uint64_t triangles=next.triangles.size(),nodeBytes=!TracePairNodes?2*(triangles/2+1)*sizeof(BVH::Node):
            sah?uint64_t(double(triangles)*TraceSahPairsPerTriangle)*sizeof(BVH::Pair):(triangles/2+1)*sizeof(BVH::Pair);
        stats.bvhBytes=triangles*sizeof(uint32_t)+nodeBytes;
        if(!chunked&&!accelerate&&(!allowed(triangles*sizeof(uint32_t))||!allowed(nodeBytes)||(sah&&!allowed(triangles*sizeof(Vec3)))))return false;
        if(accelerate){stats.tlasPieces=references.size();
            if(!output.buildRegion(std::move(next),std::move(references),error,admit,cache.budget)){if(chunked)return false;throw AccelerationUnavailable{};}
            stats.fallbackTriangles=output.fallbackTriangleCount();stats.mandatoryTreeBytes=output.ownBytes()-sizeof(BVH)-output.scene().retainedBytes();stats.tlasBytes=stats.mandatoryTreeBytes-output.order_.capacity()*sizeof(uint32_t)-output.pairs_.capacity()*sizeof(BVH::Pair)-output.opaque_.capacity();stats.compatibilityBytes=output.scene().retainedBytes();stats.sharedLiveBytes=cache.budget->used.load(std::memory_order_relaxed);
        }else if(!output.build(std::move(next),error,fast,admit))return false;
        stats.retainedBytes=cache.budget->used.load(std::memory_order_relaxed);if(!accelerate)stats.retainedBytes=cacheBytes();
        stats.bvhMs=milliseconds(stamp);stats.peakBytes=std::max(stats.peakBytes,stats.retainedBytes+stats.flatBytes+stats.bvhBytes);stats.totalMs=milliseconds(begin);return true;
    }catch(const AccelerationUnavailable&){
        cache.pieces.clear();cache.chunks.clear();stats.retainedBytes=cache.budget->used.load(std::memory_order_relaxed);stats.sharedLiveBytes=stats.retainedBytes;++stats.legacyFallbacks;
        WorldScene legacy;error.clear();if(!loadInstancedScenes(files,directory,low,high,legacy,error,15,admit,{}, {},nullptr,sourceProvider))return false;
        const uint64_t triangles=legacy.triangles.size();const uint64_t tree=triangles*sizeof(uint32_t)+(fast?uint64_t(double(triangles)*TraceSahPairsPerTriangle)*sizeof(BVH::Pair):2*(triangles/2+1)*sizeof(BVH::Node));
        if(!allowed(tree)||(fast&&!allowed(BVH::buildScratchBytes(triangles))))return false;
        if(!output.build(std::move(legacy),error,fast))return false;stats.retainedBytes=stats.sharedLiveBytes=cache.budget->used.load(std::memory_order_relaxed);stats.compatibilityBytes=output.scene().retainedBytes();stats.totalMs=milliseconds(begin);return true;
    }catch(...){error="Cannot allocate incremental local world region";return false;}
    }catch(...){error="Cannot allocate incremental local world metadata";return false;}
}

namespace {
struct RNG {
    uint32_t state;
    explicit RNG(uint32_t seed):state(seed?seed:0x9e3779b9u){}
    uint32_t next(){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
    float uniform(){return float(next()>>8)*(1.f/16777216.f);}
};
Vec3 cosineDirection(Vec3 n,RNG& rng){
    float u=rng.uniform(),phi=2*PI*rng.uniform(),r=std::sqrt(u);
    Vec3 helper=std::fabs(n.z)<.999f?Vec3(0,0,1):Vec3(0,1,0);
    Vec3 tangent=normalized(cross(helper,n)),bitangent=cross(n,tangent);
    float sine,cosine;sincosAngle(phi,sine,cosine);
    return normalized(tangent*(r*cosine)+bitangent*(r*sine)+n*std::sqrt(std::max(0.f,1-u)));
}
// Record: static-only transport (movingGeometry null) that logs every moving
// query a moving solve would issue along this unchanged path, in order.
template<bool Record=false,bool Response=false>
Vec3 transport(const BVH& bvh,Vec3 origin,Vec3 direction,const Lighting& l,const BVH* movingGeometry,RNG& rng,float& firstDistance,bool& firstBackFace,
               std::vector<StaticPathRecord::Query>* record=nullptr,Vec3* response=nullptr){
    Vec3 throughput(1,1,1),radiance;const Vec3 sun=l.sunDirection,up=l.worldUp;
    firstDistance=l.maxDistance;firstBackFace=false;
    for(unsigned bounce=0;bounce<=l.maxBounces;++bounce){
        RayHit h=bvh.trace(origin,direction,.0005f,l.maxDistance);
        if constexpr(Record)record->push_back({origin,direction,h.hit?h.distance:l.maxDistance});
        if(movingGeometry){auto actor=movingGeometry->trace(origin,direction,.0005f,h.hit?h.distance:l.maxDistance);if(actor.hit)h=actor;}
        if(bounce==0){firstDistance=h.hit?h.distance:l.maxDistance;firstBackFace=h.hit&&h.backFace;}
        if(!h.hit){
            if(dot(direction,up)>0){radiance=radiance+throughput*l.skyRadiance;if constexpr(Response)response[1]=response[1]+throughput;}
            break;
        }
        if(bounce==l.maxBounces)break;
        Vec3 offset=h.position+h.geometricNormal*l.rayBias;
        auto illuminate=[&](Vec3 direction,Vec3 irradiance,float distance,unsigned source){
            float cosine=std::max(0.f,dot(h.normal,direction));
            if(cosine<=0||dot(h.geometricNormal,direction)<=0||dot(irradiance,irradiance)<1e-16f)return;
            if(bvh.occluded(offset,direction,distance,.0005f))return;
            if constexpr(Record)record->push_back({offset,direction,distance});
            if(movingGeometry&&movingGeometry->occluded(offset,direction,distance,.0005f))return;
            radiance=radiance+throughput*h.albedo*irradiance*(cosine/PI);
            if constexpr(Response){
                const Vec3 contribution=throughput*h.albedo*(cosine/PI);
                response[source]=response[source]+(source==3?throughput*h.albedo*irradiance*(cosine/PI):contribution);
            }
        };
        illuminate(sun,l.sunIrradiance,l.maxDistance,0);
        for(const auto& light:l.additionalDirections)
            illuminate(light.direction,light.irradiance,l.maxDistance,2);
        for(const auto& light:l.points){
            Vec3 delta=light.position-offset;float squared=dot(delta,delta);
            if(squared<1e-8f||squared>=light.attenuationEnd*light.attenuationEnd)continue;
            float distance=std::sqrt(squared);
            float attenuation=clamp01((light.attenuationEnd-distance)/std::max(light.attenuationEnd-light.attenuationStart,.0001f));
            illuminate(delta/distance,light.irradiance*attenuation,std::max(.0006f,distance-l.rayBias),3);
        }
        throughput=throughput*h.albedo;
        if(std::max(throughput.x,std::max(throughput.y,throughput.z))<1e-6f)break;
        direction=cosineDirection(h.normal,rng);
        // Reject transport through the geometric back face due to an extreme
        // interpolated shading normal. This loses energy rather than leaking.
        if(dot(direction,h.geometricNormal)<=0)break;
        origin=offset;
    }
    return nonnegative(radiance);
}
}

PreparedLighting prepareLighting(const Lighting& input){
    PreparedLighting prepared;
    if(!finite(input.sunDirection)||!finite(input.sunIrradiance)||!finite(input.skyRadiance)||
       !finite(input.worldUp)||!std::isfinite(input.maxDistance)||!std::isfinite(input.rayBias)||input.maxDistance<=0||
       input.rayBias<=0||input.maxDistance>1000000.f||dot(input.worldUp,input.worldUp)<.1f)return prepared;
    for(const auto& p:input.points)if(!finite(p.position)||!finite(p.irradiance)||!std::isfinite(p.attenuationStart)||!std::isfinite(p.attenuationEnd)||p.attenuationStart<0||p.attenuationEnd<=p.attenuationStart)return prepared;
    for(const auto& d:input.additionalDirections)if(!finite(d.direction)||!finite(d.irradiance)||dot(d.direction,d.direction)<.1f)return prepared;
    auto& l=prepared.lighting_;l=input;l.movingGeometry=nullptr;
    l.sunIrradiance=nonnegative(l.sunIrradiance);l.skyRadiance=nonnegative(l.skyRadiance);
    l.sunDirection=normalized(l.sunDirection);l.worldUp=normalized(l.worldUp);
    l.maxBounces=std::min(l.maxBounces,16u);
    for(auto& d:l.additionalDirections){d.direction=normalized(d.direction);d.irradiance=nonnegative(d.irradiance);}
    for(auto& p:l.points)p.irradiance=nonnegative(p.irradiance);
    prepared.valid_=true;return prepared;
}
Probe solveProbe(const BVH& bvh,Vec3 position,const Lighting& input,unsigned rays,uint32_t seed){
    // Preserve early rejection without allocating/copying lighting vectors.
    if(!finite(position)||rays<6||rays>1048576){Probe out;out.position=position;return out;}
    return solveProbePrepared(bvh,position,prepareLighting(input),rays,seed,input.movingGeometry);
}
namespace {
// Quality presets use at most 64 rays. Reuse their latitude and azimuth lattice
// on each solver thread; a probe only rotates it by its seeded global azimuth.
struct ProbeSphere {
    unsigned count=0;std::array<Vec3,64> directions;
    void prepare(unsigned rays){
        if(count==rays)return;
        for(unsigned i=0;i<rays;++i){
            const float z=1.f-2.f*(float(i)+.5f)/float(rays),radius=std::sqrt(std::max(0.f,1-z*z));
            const float phi=float(i)*2.39996322972865332f;
            float sine,cosine;sincosAngle(phi,sine,cosine);directions[i]={radius*cosine,radius*sine,z};
        }
        count=rays;
    }
};
// Shared by the direct and replayed moving solves so ray directions, RNG
// streams and accumulation order are the same code. Ray(i,direction,rng,
// distance,backFace) returns the path radiance of ray i.
template<class Ray>
Probe accumulateProbe(Vec3 position,const Lighting& l,unsigned rays,uint32_t seed,Ray&& ray){
    Probe out;out.position=position;out.maxDistance=l.maxDistance;
    double weights[6]={},sums[6]={},squares[6]={};float closest=l.maxDistance;
    RNG orientation(seed^0xa511e9b3u);float azimuth=orientation.uniform()*2*PI;
    thread_local ProbeSphere sphere;
    const bool cached=rays<=sphere.directions.size();
    if(cached)sphere.prepare(rays);
    float rotateCos=0,rotateSin=0;if(cached)sincosAngle(azimuth,rotateSin,rotateCos);
    const float golden=2.39996322972865332f;
    for(unsigned i=0;i<rays;++i){
        // Equal-area Fibonacci sphere, randomized global azimuth. Path random
        // numbers are independent per ray, stable across scheduling order.
        Vec3 direction;
        if(cached){const auto p=sphere.directions[i];direction={p.x*rotateCos-p.y*rotateSin,p.x*rotateSin+p.y*rotateCos,p.z};}
        else{const float z=1.f-2.f*(float(i)+.5f)/float(rays),radius=std::sqrt(std::max(0.f,1-z*z));
            const float phi=azimuth+float(i)*golden;float sine,cosine;sincosAngle(phi,sine,cosine);direction={radius*cosine,radius*sine,z};}
        RNG rng(seed^((i+1u)*0x9e3779b9u));float distance;bool backFace;
        Vec3 color=ray(i,direction,rng,distance,backFace);
        if(backFace)++out.backFaceSamples;closest=std::min(closest,distance);
        const float sh[4]={.2820947918f,.4886025119f*direction.x,.4886025119f*direction.y,.4886025119f*direction.z};
        for(unsigned j=0;j<4;++j)out.sh[j]=out.sh[j]+color*(sh[j]*(4*PI/float(rays)));
        const float components[3]={direction.x,direction.y,direction.z};
        for(unsigned axis=0;axis<3;++axis){const unsigned j=2*axis+(components[axis]<0);float w=std::abs(components[axis]);w*=w;w*=w;w*=w;
            weights[j]+=w;sums[j]+=double(w)*distance;squares[j]+=double(w)*distance*distance;}
    }
    for(unsigned j=0;j<6;++j){
        if(weights[j]>1e-10)packDistanceMoment(out.moments[j],sums[j],squares[j],weights[j]);
        else{out.moments[j].mean=l.maxDistance;out.moments[j].meanSquare=l.maxDistance*l.maxDistance;}
    }
    out.samples=rays;out.valid=out.backFaceSamples<=rays/4&&closest>=.02f;
    if(!out.valid)for(auto& coefficient:out.sh)coefficient={};
    return out;
}
}
Probe solveProbePrepared(const BVH& bvh,Vec3 position,const PreparedLighting& prepared,unsigned rays,uint32_t seed,const BVH* movingGeometry){
    if(!prepared.valid()||!finite(position)||rays<6||rays>1048576){Probe out;out.position=position;return out;}
    const Lighting& l=prepared.values();
    return accumulateProbe(position,l,rays,seed,[&](unsigned,Vec3 direction,RNG& rng,float& distance,bool& backFace){
        return transport(bvh,position,direction,l,movingGeometry,rng,distance,backFace);});
}
Probe solveProbeResponse(const BVH& bvh,Vec3 position,const PreparedLighting& prepared,unsigned rays,uint32_t seed,ProbeResponse& response){
    response={};
    if(!prepared.valid()||prepared.values().additionalDirections.size()>1||!finite(position)||rays<6||rays>1048576)
        return solveProbePrepared(bvh,position,prepared,rays,seed);
    Vec3 fixed[4];const auto& l=prepared.values();
    Probe out=accumulateProbe(position,l,rays,seed,[&](unsigned,Vec3 direction,RNG& rng,float& distance,bool& backFace){
        Vec3 terms[4];
        const Vec3 color=transport<false,true>(bvh,position,direction,l,nullptr,rng,distance,backFace,nullptr,terms);
        const float sh[4]={.2820947918f,.4886025119f*direction.x,.4886025119f*direction.y,.4886025119f*direction.z};
        for(unsigned j=0;j<4;++j){
            const float weight=sh[j]*(4*PI/float(rays));
            for(unsigned source=0;source<3;++source)response.sources[source][j]=response.sources[source][j]+terms[source]*weight;
            fixed[j]=fixed[j]+terms[3]*weight;
        }
        return color;
    });
    response.fixed=out;
    for(unsigned j=0;j<4;++j)response.fixed.sh[j]=out.valid?fixed[j]:Vec3{};
    if(!out.valid)for(auto& source:response.sources)for(auto& sh:source)sh={};
    return out;
}
Probe relightProbe(const ProbeResponse& response,const PreparedLighting& prepared){
    Probe out=response.fixed;const auto& l=prepared.values();
    const Vec3 secondary=l.additionalDirections.empty()?Vec3{}:l.additionalDirections[0].irradiance;
    for(unsigned j=0;j<4;++j)out.sh[j]=out.sh[j]+response.sources[0][j]*l.sunIrradiance+
        response.sources[1][j]*l.skyRadiance+response.sources[2][j]*secondary;
    return out;
}
Probe solveProbeMoving(const BVH& bvh,Vec3 position,const PreparedLighting& prepared,unsigned rays,uint32_t seed,
                       const BVH& moving,StaticPathRecord& record,uint64_t generation,MovingSolveStats* stats){
    if(!prepared.valid()||!finite(position)||rays<6||rays>1048576){Probe out;out.position=position;return out;}
    const Lighting& l=prepared.values();
    const bool reusable=record.generation==generation&&record.bvh==&bvh&&record.rayCount==rays&&record.seed==seed&&
        std::memcmp(&record.position,&position,sizeof position)==0&&record.rays.size()==rays;
    if(!reusable){
        // Record the static-only paths once per probe and static generation.
        // They depend on neither the moving BVH nor its signature.
        record.rays.clear();record.queries.clear();record.generation=0;
        try{
            record.rays.reserve(rays);
            accumulateProbe(position,l,rays,seed,[&](unsigned,Vec3 direction,RNG& rng,float& distance,bool& backFace){
                Vec3 color=transport<true>(bvh,position,direction,l,nullptr,rng,distance,backFace,&record.queries);
                record.rays.push_back({color,distance,uint32_t(record.queries.size()),backFace});return color;});
        }catch(...){record=StaticPathRecord{};return solveProbePrepared(bvh,position,prepared,rays,seed,&moving);}
        record.rays.shrink_to_fit();record.queries.shrink_to_fit();
        record.generation=generation;record.bvh=&bvh;record.rayCount=rays;record.seed=seed;record.position=position;
        if(stats)++stats->recorded;
    }
    return accumulateProbe(position,l,rays,seed,[&](unsigned i,Vec3 direction,RNG& rng,float& distance,bool& backFace){
        // A static path is the moving path iff every moving query it would
        // issue misses. occluded() tests the same [.0005,max) hit existence
        // as trace(), so the stored result is exact; otherwise retrace.
        const auto& r=record.rays[i];
        for(uint32_t q=i?record.rays[i-1].queryEnd:0;q<r.queryEnd;++q){
            const auto& query=record.queries[q];
            if(moving.occluded(query.origin,query.direction,query.maxDistance,.0005f)){
                if(stats)++stats->retraced;
                return transport(bvh,position,direction,l,&moving,rng,distance,backFace);
            }
        }
        if(stats)++stats->replayed;
        distance=r.firstDistance;backFace=r.backFace;return r.color;});
}
Vec3 evaluateIrradiance(const Probe& p,Vec3 n){
    if(!p.valid)return {};
    n=normalized(n);Vec3 e=p.sh[0]*(PI*.2820947918f);
    e=e+(p.sh[1]*n.x+p.sh[2]*n.y+p.sh[3]*n.z)*((2*PI/3)*.4886025119f);
    return nonnegative(e);
}
float probeVisibility(const Probe& p,Vec3 direction,float distance,float bias){
    if(!p.valid||!std::isfinite(distance)||distance<0||!finite(direction)||!std::isfinite(bias))return 0;
    if(distance<=std::max(0.f,bias))return 1;
    direction=normalized(direction);if(dot(direction,direction)<.5f)return 0;
    float sum=0,mean=0,second=0;
    const float components[3]={direction.x,direction.y,direction.z};
    for(unsigned i=0;i<3;++i){float w=components[i]*components[i];const auto&m=p.moments[i*2+(components[i]<0?1:0)];
        mean+=m.mean*w;second+=m.meanSquare*w;sum+=w;}
    mean/=sum;second/=sum;
    float delta=distance-mean-std::max(0.f,bias);if(delta<=0)return 1;
    float variance=std::max(0.f,second-mean*mean);
    float probability=variance/(variance+delta*delta+1e-8f);
    // Cubic suppression reduces distant leaks; moment visibility remains an
    // approximation and cannot establish visibility of a thin nearby wall.
    return clamp01(probability*probability*probability);
}
} // namespace NorthlightGI
