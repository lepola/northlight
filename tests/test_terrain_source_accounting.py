#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Source-index sharing: last-reference eviction, optional allocation failures, cap/reset/lifetime."""
import ast
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.out()/'terrain-source-accounting';out.mkdir(parents=True,exist_ok=True)
tree=ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
values={n.targets[0].id:ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
(out/'d3d9.h').write_text(values['stub'])
fixture=values['harness'].split('int main(){')[0]
fixture=fixture.replace('Declaration declaration;float view[16]={};','Declaration declaration;float view[16]={};UINT stride=24;')
fixture=fixture.replace('16+actualIndex*24+4','16+actualIndex*stride+4').replace('*offset=16;*stride=24;','*offset=16;*stride=this->stride;')
prefix=r'''
#include <new>
#include <cstdlib>
#include <cstddef>
static long failAfter=-1;static long liveAllocations=0;
void* operator new(std::size_t n){if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();++liveAllocations;return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(p){--liveAllocations;std::free(p);}}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
'''
body=r'''
static uint64_t identity(void*,bool index){return index?12:11;}
static uint64_t versions[]={21,22};
static uint64_t version(void*,bool index){return versions[index];}
static void fill(Device& d,UINT stride=24){d.stride=stride;d.vb.bytes.resize(16+3*size_t(stride)+16);
 d.put(0,{-10200,-1180,3});d.put(1,{-10198,-1180,4});d.put(2,{-10200,-1178,5});
 d.ib.bytes.resize(1536*2);for(unsigned i=0;i<1536;++i){uint16_t v=uint16_t(i%3);std::memcpy(d.ib.bytes.data()+i*2,&v,2);}}
static bool read(FrameCache& cache,Device& d,UINT count,std::shared_ptr<const MeshSnapshot>& snapshot){cache.clearFrame();return cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,count,0,512,d.view,snapshot);}
static void accounting(){
 unsigned recharged=0,rejectedAfterEviction=0;
 for(UINT stride=519000;stride<=523000;stride+=128){Device d;fill(d);FrameCache cache;cache.setPersistentByteLimit(1);std::shared_ptr<const MeshSnapshot> out,held;
  const auto base=cache.persistentBytes();for(UINT count=3;count<6;++count)assert(read(cache,d,count,out));held=out;
  const auto stats=cache.indexStorage();assert(stats.sourceBlocks==1&&stats.sourceBytes==3072&&stats.sourceAvoided==6144);
  fill(d,stride);assert(read(cache,d,3,out));assert(cache.persistentBytes()<=cache.persistentByteLimit()&&held->positions[0].z==3);
  const auto after=cache.indexStorage();
  if(cache.persistentEntries()==1){++recharged;assert(after.sourceBlocks==1&&after.sourceBytes==3072&&!after.sourceAvoided);}
  if(!cache.persistentEntries()){++rejectedAfterEviction;assert(cache.persistentBytes()==base&&!after.sourceBlocks&&!after.sourceBytes&&!after.sourceAvoided);}
  cache.clearPersistent();assert(cache.persistentBytes()==base&&!cache.persistentEntries()&&!cache.indexStorage().sourceBytes);
 }
 assert(recharged&&rejectedAfterEviction);
 std::printf("PASS source-index last-entry eviction: recharged=%u rejectedAfterEviction=%u cap=unchanged rawControlOverhead=charged\n",recharged,rejectedAfterEviction);
}
static void failures(){unsigned optional=0,propagated=0;
 for(bool different:{false,true})for(long fail=0;fail<40;++fail){const auto before=liveAllocations;
  {Device d;fill(d);FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
   std::shared_ptr<const MeshSnapshot> out,held;assert(read(cache,d,3,out));held=out;const auto charged=cache.persistentBytes();
   // A different valid topology allocates a new source block; the same topology shares the weak memo.
   if(different){uint16_t v=1;std::memcpy(d.ib.bytes.data(),&v,2);}
   failAfter=fail;bool ok=false;try{ok=read(cache,d,4,out);}catch(const std::bad_alloc&){++propagated;}failAfter=-1;
   assert(cache.persistentBytes()<=cache.persistentByteLimit()&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
   assert(held->positions[0].z==3&&held->indices.size()==1536);
   if(ok&&cache.persistentEntries()==1){++optional;assert(cache.persistentBytes()==charged&&cache.indexStorage().sourceBlocks==1);}
   assert(read(cache,d,4,out));assert(out&&out->indices.size()==(different?1533u:1536u));
   cache.setPersistentByteLimit(1);assert(cache.persistentBytes()<=cache.persistentByteLimit());cache.clearPersistent();
   assert(!cache.persistentEntries()&&!cache.indexStorage().sourceBlocks&&!cache.indexStorage().sourceBytes&&!cache.indexStorage().sourceAvoided);
  }assert(liveAllocations==before);
 }
 assert(optional>=4&&propagated>=4);
 std::printf("PASS source-index allocation failures: optional=%u propagated=%u noLeakedAllocations=1 noLeakedCharge=1\n",optional,propagated);
}
static void offsets(){
 for(UINT maximum:{65535u,65536u,131070u}){
  versions[0]=21;Device d;const UINT stride=maximum==65535?13107:maximum==65536?16384:26214;const UINT last=maximum/stride;
  d.stride=stride;d.vb.bytes.resize(16+(size_t(last)+1)*stride+16);
  const uint16_t indices[]={0,1,uint16_t(last)};d.ib.bytes.resize(sizeof indices);std::memcpy(d.ib.bytes.data(),indices,sizeof indices);
  d.put(0,{-10200,-1180,3});d.put(1,{-10198,-1180,4});d.put(last,{-10200,-1178,5});
  FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
  std::shared_ptr<const MeshSnapshot> snapshot,held;Diagnostics why;
  auto read=[&](){cache.clearFrame();return cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,last+1,0,1,d.view,snapshot,&why);};
  assert(read());held=snapshot;auto stats=cache.indexStorage();
  assert(stats.offsetCompact==(maximum==65535)&&stats.offsetWide==(maximum!=65535));
  assert(stats.offsetBytes==(maximum==65535?6u:12u)&&stats.offsetAvoided==(maximum==65535?6u:0u));
  d.vb.bytes[16+4+12]^=1; // An unrelated attribute write cannot change captured XYZ.
  ++versions[0];assert(read()&&snapshot==held&&why.contentCacheHit&&!why.trackedCacheHit&&cache.maintenance().exactReuses==1);
  versions[0]=0;assert(read()&&snapshot==held&&cache.bytesRead()>0); // Unknown sources still require full live reads.
  d.put(last,{-10200,-1178,55});assert(read()&&snapshot!=held&&snapshot->positions[2].z==55&&held->positions[2].z==5);
  cache.clearPersistent();assert(!cache.indexStorage().offsetBytes&&!cache.indexStorage().offsetCompact&&!cache.indexStorage().offsetWide);
  assert(held->positions[2].z==5&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 }
 std::puts("PASS source-offset 65535/65536/wide fallback, exact live XYZ mutation, unknown-source reads, reset and immutable lifetime");
}
int main(){accounting();failures();offsets();assert(!liveAllocations);}
'''
(out/'accounting.cpp').write_bytes((prefix+fixture+body).encode())
for label,flags in [('release',['-O2']),('debug',['-O0','-g'])]:
 exe=out/(label+'.exe')
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(out),*fp.test_include_flags(),str(out/'accounting.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
 subprocess.run([str(exe)],check=True)
