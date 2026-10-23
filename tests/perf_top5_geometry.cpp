#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include "draw_snapshot.h"
#include "actor_deformation.h"
#include "displacement_geometry.h"
int main(){
    using namespace NorthlightDisplacementGeometry;
    struct V{float x,y,z,nx,ny,nz,u,v;};
    Mesh source;source.vertexCount=21*21;source.primitiveCount=20*20*2;source.indexed=true;source.topology=D3DPT_TRIANGLELIST;source.streams[0].stride=sizeof(V);source.streams[0].bytes.resize(source.vertexCount*sizeof(V));
    std::vector<std::array<float,2>> uv;
    for(unsigned y=0;y<=20;++y)for(unsigned x=0;x<=20;++x){V v={float(x),float(y),std::sin(x*.3f)*.2f,0,0,1,x*.13f,y*.17f};std::memcpy(source.streams[0].bytes.data()+(y*21+x)*sizeof(V),&v,sizeof v);uv.push_back({v.u,v.v});}
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x){unsigned a=y*21+x;source.indices.insert(source.indices.end(),{a,a+1,a+21,a+21,a+1,a+22});}
    D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    Height h;h.width=h.height=64;for(unsigned i=0;i<4096;++i)h.values.push_back(.5f+.3f*std::sin(i*.37f));
    uint64_t hash=1469598103934665603ull;auto feed=[&](const void* p,size_t n){for(size_t i=0;i<n;++i)hash=(hash^static_cast<const unsigned char*>(p)[i])*1099511628211ull;};
    std::vector<double> times;LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    for(unsigned round=0;round<11;++round){LARGE_INTEGER a,b;QueryPerformanceCounter(&a);for(unsigned factor:{2u,4u,8u,16u}){Mesh out;assert(build(source,decl,4,uv,h,factor,false,false,false,out));if(round==0){feed(out.streams[0].bytes.data(),out.streams[0].bytes.size());feed(out.indices.data(),out.indices.size()*4);}}QueryPerformanceCounter(&b);if(round>1)times.push_back(double(b.QuadPart-a.QuadPart)*1000/f.QuadPart);}
    std::sort(times.begin(),times.end());std::printf("GEOMETRY_PERF_PASS hash=%016llx medianMs=%.6f\n",(unsigned long long)hash,times[times.size()/2]);return 0;
}
