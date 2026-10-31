#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include "northlight_mem.h"
extern "C" int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}
#include "actor_deformation.h"
using namespace NorthlightActorDeformation;using Mesh=NorthlightDrawSnapshot::Mesh;
__attribute__((noinline)) bool base(const Program& program,const Mesh& mesh,const PreparedBindings& prepared,const float* constants,float* low,float* high){
    if(!program.kernel.singleSkin()||!constants||prepared.bindings.size()!=2||prepared.bindings[0].input.reg>=2||prepared.bindings[1].input.reg>=2||prepared.bindings[0].input.reg==prepared.bindings[1].input.reg)return false;
    const Four control=singleSkinControl(program,constants);Four inputs[2];
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){for(const auto& b:prepared.bindings){const auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four view;if(!evaluateSingleSkin<true>(program,inputs,constants,view,&control))return false;
        for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k]))return false;low[k]=std::min(low[k],view[k]);high[k]=std::max(high[k],view[k]);}}
    return true;
}
__attribute__((noinline)) bool next(const Program& p,const Mesh& m,const PreparedBindings& b,const float* c,float* l,float* h){return singleSkinBoundsPrepared<true>(p,m,b,c,l,h);}
volatile float sink;
static void failures(){Program p;p.major=3;p.kernel.evaluator=Kernel::SingleFirst;p.definitions.push_back({0,{3,1,0,0}});float c[1024]={};for(unsigned i=0;i<1024;++i)c[i]=float(i%13)*.01f;
 Mesh mesh;mesh.vertexCount=3;mesh.streams[0].stride=12;mesh.streams[0].bytes.resize(36);mesh.streams[1].stride=4;mesh.streams[1].bytes.resize(12);
 for(unsigned i=0;i<3;++i){float v[3]={float(i+1),float(i+2),float(i+3)};std::memcpy(mesh.streams[0].bytes.data()+12*i,v,12);mesh.streams[1].bytes[4*i]=i;}
 D3DVERTEXELEMENT9 x={0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},y={1,0,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0};PreparedBindings b;b.bindings={{{0,0,0},x},{{1,0,0},y}};
 for(unsigned test=0;test<8;++test){auto m=mesh;auto program=p;auto bindings=b;auto bank=std::vector<float>(c,c+1024);const float* input=bank.data();
  switch(test){case 1:std::swap(bindings.bindings[0],bindings.bindings[1]);break;case 2:bindings.bindings[0].element.Type=255;break;case 3:bindings.bindings[1].input.reg=0;break;case 4:input=nullptr;break;case 5:program.kernel.evaluator=Kernel::Generic;break;case 6:bank[31*4]=std::numeric_limits<float>::quiet_NaN();break;case 7:m.vertexCount=0;bindings.bindings[0].element.Stream=255;break;}
  float a[3]={1e30f,1e30f,1e30f},z[3]={-1e30f,-1e30f,-1e30f},d[3]={1e30f,1e30f,1e30f},e[3]={-1e30f,-1e30f,-1e30f};bool old=base(program,m,bindings,input,a,z),actual=next(program,m,bindings,input,d,e);assert(old==actual&&!std::memcmp(a,d,12)&&!std::memcmp(z,e,12));}
}
int main(){failures();Program p;p.major=3;p.kernel.evaluator=Kernel::SingleFirst;p.definitions.push_back({0,{3,1,0,0}});float c[1024];for(unsigned i=0;i<1024;++i)c[i]=float(i%13)*.01f;for(unsigned count:{32u,128u,512u,2048u})for(unsigned reverse:{0u,1u}){Mesh m;m.vertexCount=count;m.streams[0].stride=16;m.streams[0].bytes.resize(count*16);for(unsigned i=0;i<count;++i){float v[3]={float(i%10),float(i%7),float(i%5)};std::memcpy(m.streams[0].bytes.data()+i*16,v,12);m.streams[0].bytes[i*16+12]=i%4;}PreparedBindings b;D3DVERTEXELEMENT9 x={0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},y={0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0};b.bindings={{{0,0,0},x},{{1,0,0},y}};if(reverse)std::swap(b.bindings[0],b.bindings[1]);float lo[3],hi[3],l2[3],h2[3];for(int k=0;k<3;++k)lo[k]=l2[k]=1e30f,hi[k]=h2[k]=-1e30f;assert(base(p,m,b,c,lo,hi)==next(p,m,b,c,l2,h2));assert(!std::memcmp(lo,l2,12)&&!std::memcmp(hi,h2,12));std::vector<double> times[2];for(int r=0;r<14;++r)for(int turn=0;turn<2;++turn){int mode=(r+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned rep=0;rep<100;++rep){float l[3]={1e30f,1e30f,1e30f},h[3]={-1e30f,-1e30f,-1e30f};assert(mode?next(p,m,b,c,l,h):base(p,m,b,c,l,h));sink=l[0];}times[mode].push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}for(auto& v:times)std::sort(v.begin(),v.end());printf("vertices=%u reverse=%u baseline=%.1f candidate=%.1f ratio=%.3f\n",count,reverse,times[0][7],times[1][7],times[1][7]/times[0][7]);}}
