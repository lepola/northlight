#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#define NorthlightDisplacementGeometry NorthlightDisplacementReference
#include "fixtures/reference/displacement_geometry-93fd5436.h"
#undef NorthlightDisplacementGeometry
#include "displacement_geometry.h"

namespace G=NorthlightDisplacementGeometry;
namespace R=NorthlightDisplacementReference;
struct Vertex {float position[3],normal[3],uv[2],weights[4];unsigned char bones[4];DWORD color;};
const D3DVERTEXELEMENT9 declaration[]={
    {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
    {0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
    {0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
    {0,32,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},
    {0,48,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},
    {0,52,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
struct Fixture {
    G::Mesh mesh;std::vector<std::array<float,2>> uv;std::vector<float> blend;
    G::Height height;R::Height reference;
    Fixture(unsigned side,bool seams,bool duplicate=false){
        height.width=height.height=32;height.values.resize(1024);
        for(unsigned i=0;i<1024;++i)height.values[i]=float((i*37+i/11)%256)/255;
        reference.width=height.width;reference.height=height.height;reference.values=height.values;
        mesh.indexed=true;mesh.topology=D3DPT_TRIANGLELIST;mesh.streams[0].stride=sizeof(Vertex);
        const auto vertex=[&](unsigned x,unsigned y){
            Vertex v={{float(x),float(y),float((x+y)%3)*.02f},{.1f,.2f,1},{float(x)/side,float(y)/side}, {.5f,.25f,.25f,0},{0,3,7,0},0x7f123456};
            const unsigned id=mesh.vertexCount++;const auto* p=reinterpret_cast<const unsigned char*>(&v);
            mesh.streams[0].bytes.insert(mesh.streams[0].bytes.end(),p,p+sizeof v);
            uv.push_back({v.uv[0],v.uv[1]});blend.push_back(float((x+y)%5)/4);return id;
        };
        if(!seams)for(unsigned y=0;y<=side;++y)for(unsigned x=0;x<=side;++x)vertex(x,y);
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
            unsigned a,b,c,d;
            if(seams){a=vertex(x,y);b=vertex(x+1,y);c=vertex(x,y+1);d=vertex(x+1,y+1);}
            else{a=y*(side+1)+x;b=a+1;c=a+side+1;d=c+1;}
            mesh.indices.insert(mesh.indices.end(),{a,b,c,c,b,d});
        }
        if(duplicate){const auto indices=mesh.indices;for(size_t i=0;i<indices.size();i+=3)mesh.indices.insert(mesh.indices.end(),{indices[i+2],indices[i+1],indices[i]});}
        mesh.primitiveCount=unsigned(mesh.indices.size()/3);
    }
    bool build(bool old,G::Mesh& output,unsigned factor,bool terrain,bool gpu,bool layered,unsigned influences,const D3DVERTEXELEMENT9* decl=declaration,unsigned count=7)const{
        if(old)return R::build(mesh,decl,count,uv,reference,factor,terrain,true,false,output,
            layered?&reference:nullptr,layered?&uv:nullptr,layered?&blend:nullptr,influences,gpu);
        return G::build(mesh,decl,count,uv,height,factor,terrain,true,false,output,
            layered?&height:nullptr,layered?&uv:nullptr,layered?&blend:nullptr,influences,gpu);
    }
};
static unsigned cases=0;static std::uint64_t compared=0;
struct ScratchMemory:std::pmr::memory_resource {
    std::pmr::memory_resource* upstream=std::pmr::get_default_resource();
    size_t allocations=0,live=0,peak=0;
    ScratchMemory(){std::pmr::set_default_resource(this);}
    ~ScratchMemory(){std::pmr::set_default_resource(upstream);assert(live==0);}
    void* do_allocate(size_t bytes,size_t alignment)override{
        void* p=upstream->allocate(bytes,alignment);++allocations;live+=bytes;peak=std::max(peak,live);return p;
    }
    void do_deallocate(void* p,size_t bytes,size_t alignment)override{assert(live>=bytes);live-=bytes;upstream->deallocate(p,bytes,alignment);}
    bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override{return this==&other;}
};
static void compare(const Fixture& f,unsigned factor,bool terrain,bool gpu,bool layered,unsigned influences,const D3DVERTEXELEMENT9* decl=declaration,unsigned count=7){
    G::Mesh a,b;const bool before=f.build(true,a,factor,terrain,gpu,layered,influences,decl,count);
    ScratchMemory memory;
    const bool after=f.build(false,b,factor,terrain,gpu,layered,influences,decl,count);
    assert(memory.live==0&&memory.peak<4u*1024*1024);
    assert(before==after&&a.vertexCount==b.vertexCount&&a.primitiveCount==b.primitiveCount&&a.indices==b.indices);
    for(unsigned s=0;s<4;++s){assert(a.streams[s].stride==b.streams[s].stride&&a.streams[s].bytes==b.streams[s].bytes);compared+=a.streams[s].bytes.size();}
    ++cases;
}
static volatile unsigned sink;
static void benchmark(const char* name,const Fixture& f,unsigned factor,unsigned influences,bool gpu){
    std::vector<double> times[2];
    for(unsigned round=0;round<13;++round)for(unsigned turn=0;turn<2;++turn){
        const unsigned mode=(round+turn)&1;const auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<8;++repeat){G::Mesh output;assert(f.build(mode==0,output,factor,false,gpu,false,influences));sink=output.vertexCount;}
        if(round>=3)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/8);
    }
    for(auto& t:times)std::sort(t.begin(),t.end());
    const double a=times[0][times[0].size()/2],b=times[1][times[1].size()/2];
    std::printf("SUBDIVISION_BENCH name=%s baselineMs=%.6f currentMs=%.6f ratio=%.4f\n",name,a,b,b/a);
}
int main(int argc,char**){
    for(unsigned side:{1u,8u,32u})for(bool seams:{false,true})for(bool duplicate:{false,true}){
        Fixture f(side,seams,duplicate);
        for(unsigned factor:{2u,4u,8u,128u})for(bool terrain:{false,true})for(unsigned mode=0;mode<3;++mode)
            compare(f,factor,terrain,mode==1,mode==2,seams?4:0);
    }
    Fixture f(8,false);const D3DVERTEXELEMENT9 noNormal[]={declaration[0],declaration[2],declaration[5],D3DDECL_END()};
    compare(f,8,false,false,false,0,noNormal,4);
    for(auto& p:f.uv){p[0]*=7;p[1]-=3;}compare(f,8,false,false,true,4);
    for(unsigned i=0;i<f.mesh.vertexCount;++i)f.mesh.streams[0].bytes[i*sizeof(Vertex)+48]=static_cast<unsigned char>(i%31);
    compare(f,8,false,false,false,4);
    f.uv[0][0]=NAN;compare(f,8,false,false,false,0);
    f.uv[0][0]=0;f.mesh.indices[0]=UINT_MAX;compare(f,8,false,false,false,0);
    Fixture strip(1,false);strip.mesh.indexed=false;strip.mesh.topology=D3DPT_TRIANGLESTRIP;strip.mesh.indices.clear();compare(strip,8,true,true,false,0);
    {Fixture dense(32,true);ScratchMemory memory;G::Mesh output;assert(dense.build(false,output,4,false,true,false,0));
     assert(memory.live==0);std::printf("SUBDIVISION_SCRATCH allocations=%zu peakBytes=%zu retainedBytes=%zu vertices=%u\n",memory.allocations,memory.peak,memory.live,output.vertexCount);}
    std::printf("SUBDIVISION_EXACT_PASS cases=%u comparedBytes=%llu\n",cases,(unsigned long long)compared);
    if(argc>1){benchmark("wmo-grid",Fixture(32,false),4,0,true);benchmark("wmo-seams",Fixture(32,true),4,0,true);benchmark("model",Fixture(16,false),8,4,false);}
}
