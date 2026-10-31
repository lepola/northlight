#include "snapshot_harness.h"
#include <cassert>
#include <cstdio>
#include <random>
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {static bool decode(Frame& f,const void* data,UINT count,const Draw& d,Mesh& mesh,UINT& low,UINT& high,Diagnostics& why){return f.decode(data,count,D3DFMT_INDEX32,d,mesh,low,high,&why);}};
}
int main(){using namespace NorthlightDrawSnapshot;std::mt19937 random(426711);unsigned cases=0;
 for(unsigned n=1;n<140;++n)for(unsigned pattern=0;pattern<8;++pattern)for(unsigned offset=0;offset<16;++offset){
  Draw draw{};draw.minimum=pattern==1?0x7ffffff0u:pattern==2?0xffffff00u:0;draw.vertices=pattern==2?256u:pattern==1?64u:pattern==3?UINT(-1):1000;
  std::vector<UINT> values(n);for(auto& v:values)v=draw.minimum+random()%draw.vertices;
  if(pattern>=4){values[n/2]=draw.minimum+draw.vertices;}
  std::vector<unsigned char> raw(n*4+offset);std::memcpy(raw.data()+offset,values.data(),n*4);
  Frame frame;Mesh mesh;Diagnostics why;UINT low=0,high=0;const bool ok=SnapshotTestAccess::decode(frame,raw.data()+offset,n,draw,mesh,low,high,why);
  if(pattern>=4){assert(!ok&&why.error==Error::IndexRange);++cases;continue;}
  assert(ok);auto unique=values;std::sort(unique.begin(),unique.end());unique.erase(std::unique(unique.begin(),unique.end()),unique.end());assert(mesh.vertexCount==unique.size()&&low==unique.front()&&high==unique.back());
  for(unsigned i=0;i<n;++i)assert(mesh.indices[i]==UINT(std::lower_bound(unique.begin(),unique.end(),values[i])-unique.begin()));
  Mesh repeated;assert(SnapshotTestAccess::decode(frame,raw.data()+offset,n,draw,repeated,low,high,why));assert(repeated.indices==mesh.indices&&repeated.vertexCount==mesh.vertexCount);++cases;
 }
 std::printf("SNAPSHOT_WIDE_DECODE_PASS cases=%u exactRemaps=1 invalidRanges=1 timings=0\n",cases);
}
