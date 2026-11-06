#define NOMINMAX
#include "draw_snapshot.h"
#include <cassert>
#include <random>
#include <cstdio>
namespace NorthlightDrawSnapshot { struct SnapshotTestAccess {static bool run(Frame& f,const void* p,UINT n,D3DFORMAT fmt,const Draw& d,Mesh& m,UINT& l,UINT& h,Diagnostics& why){return f.decode(p,n,fmt,d,m,l,h,&why);}}; }
using namespace NorthlightDrawSnapshot;
int main(){std::mt19937 rng(194823);Frame frame;for(unsigned trial=0;trial<10000;++trial){
 const unsigned count=1+trial%73;const bool wide=trial%9==0;const unsigned min=(trial%4==0)?65520:(trial%4==1?0:unsigned(rng()%60000));const unsigned available=65536-min;const unsigned range=(trial%17==0)?UINT(-1)-min:1+rng()%std::min(available,500u);Draw draw{};draw.minimum=min;draw.vertices=range;
 std::vector<unsigned char> storage(count*(wide?4:2)+3);auto* input=storage.data()+1;std::vector<UINT> inputValues;bool valid=true;
 for(unsigned i=0;i<count;++i){UINT v=min+rng()%std::min(range,available);if(trial%3==0&&i==trial%count)v=min?min-1:UINT(std::min<std::uint64_t>(std::uint64_t(min)+range,65535));if(wide)std::memcpy(input+i*4,&v,4);else{std::uint16_t t=std::uint16_t(v);std::memcpy(input+i*2,&t,2);v=t;}inputValues.push_back(v);valid&=v>=min&&std::uint64_t(v)<std::uint64_t(min)+range;}
 for(unsigned repeat=0;repeat<2;++repeat){Mesh mesh;Diagnostics why;UINT low=0,high=0;const bool ok=SnapshotTestAccess::run(frame,input,count,wide?D3DFMT_INDEX32:D3DFMT_INDEX16,draw,mesh,low,high,why);assert(ok==valid);if(!ok){assert(why.error==Error::IndexRange);continue;}
 auto unique=inputValues;std::sort(unique.begin(),unique.end());unique.erase(std::unique(unique.begin(),unique.end()),unique.end());assert(low==unique.front()&&high==unique.back()&&mesh.vertexCount==unique.size());for(unsigned i=0;i<count;++i)assert(mesh.indices[i]==unsigned(std::lower_bound(unique.begin(),unique.end(),inputValues[i])-unique.begin()));}
 if(trial%128==0)frame.clearIndexCache();}
 // Dense reverse-order input exercises the dense remap path at both ends of the 16-bit domain.
 for(unsigned n=1;n<=64;++n)for(unsigned base:{0u,65536u-n}){std::vector<std::uint16_t> indices(n);for(unsigned i=0;i<n;++i)indices[i]=std::uint16_t(base+n-1-i);Draw d{};d.minimum=base;d.vertices=n;Mesh m;UINT l=0,h=0;Diagnostics why;assert(SnapshotTestAccess::run(frame,indices.data(),n,D3DFMT_INDEX16,d,m,l,h,why));assert(l==base&&h==base+n-1&&m.vertexCount==n);for(unsigned i=0;i<n;++i)assert(m.indices[i]==n-1-i);}
 // Every lane and scalar tail can be the first invalid index, including full unsigned range boundaries.
 for(unsigned count=1;count<=25;++count)for(unsigned bad=0;bad<count;++bad)for(unsigned side=0;side<2;++side){std::vector<std::uint16_t> indices(count,32768);indices[bad]=side?65535:0;Draw d{};d.minimum=1;d.vertices=65534;Mesh m;UINT l=0,h=0;Diagnostics why;assert(!SnapshotTestAccess::run(frame,indices.data(),count,D3DFMT_INDEX16,d,m,l,h,why)&&why.error==Error::IndexRange);}
 std::puts("index SSE2 exact remap, bounds, lanes, tails, cache and fallback passed");}
