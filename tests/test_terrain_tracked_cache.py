#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Tracked terrain lifetime/write revisions, native fake D3D; no game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import ast,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
nodes=ast.parse((HERE/'test_terrain_snapshot.py').read_text()).body
values={n.targets[0].id:ast.literal_eval(n.value) for n in nodes if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
source=values['harness'].split('int main(){')[0]+r'''
#include <chrono>
static uint64_t identities[2]={11,12},revisions[2]={21,22};
static uint64_t identity(void*,bool index){return identities[index];}
static uint64_t version(void*,bool index){return revisions[index];}
int main(){
 Device d;constexpr double origin=17066.666666666666,step=100./3.;
 float x=float(origin-821*step),y=float(origin-548*step);
 d.put(4,{x,y,3});d.put(5,{x+30,y,4});d.put(6,{x,y+30,5});
 FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
 std::shared_ptr<const MeshSnapshot> a,b;Diagnostics info;
 auto read=[&](FrameCache& c){return c.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,b,&info);};
 assert(read(cache)&&!info.trackedCacheHit);a=b;const auto bytes=cache.bytesRead();assert(bytes>0);
 unsigned vl=d.vb.locks,il=d.ib.locks;cache.clearFrame();
 assert(read(cache)&&info.trackedCacheHit&&a==b&&cache.bytesRead()==0&&cache.avoidedReadBytes()==bytes);
 assert(d.vb.locks==vl&&d.ib.locks==il);
 // Sampled timing observes the same tracked result without adding reads or changing ownership.
 FrameCache::PhaseStats phases;cache.clearFrame();
 assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,b,&info,&phases));
 assert(b==a&&info.trackedCacheHit&&phases.clockReads==2);
 assert(d.vb.locks==vl&&d.ib.locks==il);
 // Write/DISCARD/NOOVERWRITE revisions invalidate within the SAME frame.
 d.put(4,{x,y,30});++revisions[0];il=d.ib.locks;
 assert(read(cache)&&!info.trackedCacheHit&&b->positions[0].z==30&&a->positions[0].z==3);
 assert(d.ib.locks==il&&cache.maintenance().indexTokenHits==1);
 std::uint16_t swapped[]={123,8,7,9};std::memcpy(d.ib.bytes.data(),swapped,sizeof swapped);++revisions[1];
 assert(read(cache)&&!info.trackedCacheHit&&b->positions[0].z==4);
 // Reused pointers with a different lifetime token can never hit old content.
 ++identities[0];vl=d.vb.locks;assert(read(cache)&&!info.trackedCacheHit&&d.vb.locks>vl);
 ++identities[1];il=d.ib.locks;assert(read(cache)&&!info.trackedCacheHit&&d.ib.locks>il);
 // A pending/failed lock or untracked buffer returns revision 0. Full byte
 // fallback catches mutations even when the other buffer remains tracked.
 revisions[0]=0;d.put(5,{x+30,y,40});il=d.ib.locks;
 assert(read(cache)&&!info.trackedCacheHit&&b->positions[0].z==40&&d.ib.locks>il);
 vl=d.vb.locks;assert(read(cache)&&!info.trackedCacheHit&&d.vb.locks>vl);revisions[0]=29;
 assert(read(cache)&&!info.trackedCacheHit);assert(read(cache)&&info.trackedCacheHit);
 // Missing identity also forbids tracked reuse, including unknown COM buffers.
 identities[1]=0;il=d.ib.locks;assert(read(cache)&&!info.trackedCacheHit&&d.ib.locks>il);identities[1]=90;
 assert(read(cache)&&!info.trackedCacheHit);cache.clearPersistent();assert(cache.trackedHits()==0);assert(read(cache)&&!info.trackedCacheHit);
 // Admission remains identical although fast hits physically read no bytes.
 Limits limits;limits.maxReadBytesPerFrame=bytes;FrameCache bounded(limits);bounded.setIdentityProvider(identity);bounded.setVersionProvider(version);
 assert(read(bounded));bounded.clearFrame();assert(read(bounded)&&info.trackedCacheHit&&bounded.bytesRead()==0);
 assert(!read(bounded)&&info.reason==RejectReason::ByteBudget);
 // Independent proof saves reads, never the admission charge. A VB-only write
 // must still reject the second draw when the per-frame byte budget is spent.
 ++revisions[0];bounded.clearFrame();il=d.ib.locks;
 assert(read(bounded)&&info.contentCacheHit&&bounded.maintenance().indexTokenHits==1);
 assert(d.ib.locks==il&&bounded.bytesRead()==bytes-6);
 assert(!read(bounded)&&info.reason==RejectReason::ByteBudget);
 // Providers are replaceable only by invalidating their proof domain.
 cache.setVersionProvider(nullptr);vl=d.vb.locks;assert(read(cache)&&!info.trackedCacheHit&&d.vb.locks>vl);
 cache.setVersionProvider(version);assert(read(cache));vl=d.vb.locks;il=d.ib.locks;
 const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<10000;++i){cache.clearFrame();assert(read(cache)&&info.trackedCacheHit);}
 double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/10000;
 assert(d.vb.locks==vl&&d.ib.locks==il&&cache.bytesRead()==0);
 assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 // A rotating view revisits sparse draws: retain the decoded geometry without
 // duplicating large raw spans when lifetime/write revisions prove the hit.
 Device wide;wide.vb.bytes.resize(16+192*1004*24);
 const std::uint16_t sparse[]={123,7,500,1000};std::memcpy(wide.ib.bytes.data(),sparse,sizeof sparse);
 for(unsigned draw=0;draw<192;++draw){wide.put(draw*1004+4,{x,y,3+float(draw)});wide.put(draw*1004+497,{x+30,y,4});wide.put(draw*1004+997,{x,y+30,5});}
 FrameCache compact;compact.setIdentityProvider(identity);compact.setVersionProvider(version);compact.setPersistentByteLimit(1);
 FrameCache untracked;untracked.setPersistentByteLimit(1);
 unsigned warmHits=0;uint64_t warmBytes=0;
 std::vector<std::shared_ptr<const MeshSnapshot>> retained;
 for(unsigned cycle=0;cycle<3;++cycle){
  compact.clearFrame();untracked.clearFrame();warmHits=0;
  for(unsigned draw=0;draw<192;++draw){
   std::shared_ptr<const MeshSnapshot> tracked,raw;Diagnostics td,ud;
   assert(compact.readMeshShared(&wide,D3DPT_TRIANGLELIST,INT(draw*1004)-3,7,994,1,1,wide.view,tracked,&td));
   assert(untracked.readMeshShared(&wide,D3DPT_TRIANGLELIST,INT(draw*1004)-3,7,994,1,1,wide.view,raw,&ud));
   assert(tracked->positions.size()==3&&tracked->indices==raw->indices);
   assert(!std::memcmp(tracked->positions.data(),raw->positions.data(),3*sizeof(Position)));
   warmHits+=td.trackedCacheHit;
   if(cycle==2)retained.push_back(tracked);
  }
  if(cycle){assert(warmHits==192&&compact.bytesRead()==0&&compact.maintenance().evictions==0);assert(untracked.bytesRead()>0&&untracked.maintenance().evictions>0);}
  warmBytes=compact.bytesRead();
 }
 assert(compact.persistentBytes()<untracked.persistentBytes()/2);
 // A buffer write changes the revision even when the draw's XYZ/indices do
 // not change. Verify those inputs and retain the exact immutable snapshot.
 for(unsigned cycle=0;cycle<3;++cycle){
  ++revisions[0];++revisions[1];compact.clearFrame();
  for(unsigned draw=0;draw<192;++draw){
   std::shared_ptr<const MeshSnapshot> current;Diagnostics td;
   assert(compact.readMeshShared(&wide,D3DPT_TRIANGLELIST,INT(draw*1004)-3,7,994,1,1,wide.view,current,&td));
   assert(td.contentCacheHit&&!td.trackedCacheHit&&current==retained[draw]);
  }
  assert(compact.maintenance().exactReuses==192&&compact.maintenance().rebuilds==0&&compact.maintenance().evictions==0);
 }
 // Non-position attributes do not affect this geometry snapshot. A real XYZ
 // change must rebuild, and must not alter earlier snapshots held by readers.
 wide.vb.bytes[16+4*24+16]^=1;++revisions[0];compact.clearFrame();
 assert(compact.readMeshShared(&wide,D3DPT_TRIANGLELIST,-3,7,994,1,1,wide.view,b,&info)&&info.contentCacheHit&&b==retained[0]);
 wide.put(4,{x,y,1071});++revisions[0];compact.clearFrame();
 assert(compact.readMeshShared(&wide,D3DPT_TRIANGLELIST,-3,7,994,1,1,wide.view,b,&info)&&!info.contentCacheHit);
 assert(b->positions[0].z==1071&&retained[0]->positions[0].z==3);
 assert(wide.vb.refs==1&&wide.ib.refs==1&&wide.vb.locks==wide.vb.unlocks&&wide.ib.locks==wide.ib.unlocks);
 // The client copies identical terrain to different dynamic-buffer addresses.
 // Exact geometry survives relocation, while new indices/XYZ and invalid ranges
 // are still checked against current bytes. Old snapshots remain immutable.
 Device moving;moving.vb.bytes.resize(16+64*24);moving.ib.bytes.resize(64);
 moving.put(4,{x,y,3});moving.put(5,{x+30,y,4});moving.put(6,{x,y+30,5});
 FrameCache relocated;relocated.setIdentityProvider(identity);relocated.setVersionProvider(version);
 std::shared_ptr<const MeshSnapshot> original,current;
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,-3,7,3,1,1,moving.view,original));
 moving.put(24,{x,y,3});moving.put(25,{x+30,y,4});moving.put(26,{x,y+30,5});
 const std::uint16_t movedIndices[]={7,8,9};std::memcpy(moving.ib.bytes.data()+20,movedIndices,sizeof movedIndices);
 ++revisions[0];++revisions[1];relocated.clearFrame();
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,17,7,3,10,1,moving.view,current,&info));
 assert(current==original&&info.contentCacheHit&&!info.trackedCacheHit&&relocated.maintenance().relocatedReuses==1);
 assert(relocated.persistentEntries()==1);
 vl=moving.vb.locks;il=moving.ib.locks;relocated.clearFrame();
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,17,7,3,10,1,moving.view,current,&info)&&info.trackedCacheHit);
 assert(moving.vb.locks==vl&&moving.ib.locks==il);
 // Returning to the earlier address reads both buffers; relocation must not
 // transfer the new address's token-only proof to the old draw.
 moving.put(4,{x,y,31});++revisions[0];
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,-3,7,3,1,1,moving.view,current,&info));
 assert(current!=original&&current->positions[0].z==31&&original->positions[0].z==3);
 assert(!relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,17,8,2,10,1,moving.view,current,&info)&&info.reason==RejectReason::IndexRange);
 revisions[0]=revisions[1]=0;++identities[0];++identities[1];
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,17,7,3,10,1,moving.view,current,&info)&&current==original&&!info.trackedCacheHit);
 moving.put(24,{x,y,47});
 assert(relocated.readMeshShared(&moving,D3DPT_TRIANGLELIST,17,7,3,10,1,moving.view,current,&info)&&current->positions[0].z==47);
 relocated.clearPersistent();assert(original->positions[0].z==3&&relocated.persistentEntries()==0);
 // Differential oracle: independent writes, unrelated attributes, lifetime
 // reuse and unknown/pending revisions must match a fresh byte-read snapshot.
 Device random;random.put(4,{x,y,3});random.put(5,{x+30,y,4});random.put(6,{x,y+30,5});
 FrameCache verified,oracle;verified.setIdentityProvider(identity);verified.setVersionProvider(version);
 uint32_t seed=20261019;uint64_t vv=1000,iv=1000;
 for(unsigned op=0;op<10000;++op){
  seed=seed*1664525u+1013904223u;identities[0]=101;identities[1]=102;
  switch(seed%7){
   case 0:random.put(4,{x,y,float(seed%1000)});++vv;break;
   case 1:random.vb.bytes[16+4*24+16]^=1;++vv;break;
   case 2:std::swap(random.ib.bytes[2],random.ib.bytes[4]);++iv;break;
   case 3:++vv;++iv;break;
   case 4:identities[0]=seed+103;break;
   case 5:identities[1]=0;break;
  }
  revisions[0]=seed%11?vv:0;revisions[1]=seed%13?iv:0;
  verified.clearFrame();oracle.clearPersistent();oracle.clearFrame();
  std::shared_ptr<const MeshSnapshot> actual,expected;Diagnostics why;
  assert(verified.readMeshShared(&random,D3DPT_TRIANGLELIST,-3,7,3,1,1,random.view,actual,&why));
  assert(oracle.readMeshShared(&random,D3DPT_TRIANGLELIST,-3,7,3,1,1,random.view,expected));
  assert(actual->indices==expected->indices&&actual->positions.size()==expected->positions.size());
  assert(!std::memcmp(actual->positions.data(),expected->positions.data(),actual->positions.size()*sizeof(Position)));
  for(unsigned j=0;j<3;++j)assert(actual->bounds.low[j]==expected->bounds.low[j]&&actual->bounds.high[j]==expected->bounds.high[j]);
 }
 assert(random.vb.locks==random.vb.unlocks&&random.ib.locks==random.ib.unlocks);
 std::puts("PASS 10000 independent-write/lifetime/unknown-revision mutations against fresh byte-read oracle");
 std::printf("{\"pass\":true,\"warm_draw_us\":%.6f,\"warm_locks\":0,\"warm_read_bytes\":0,\"write_identity_reset_and_unknown_fallback\":true,\"rotation_warm_hits\":%u,\"rotation_read_bytes\":%llu,\"compact_cache_bytes\":%llu,\"raw_cache_bytes\":%llu}\n",us,warmHits,(unsigned long long)warmBytes,(unsigned long long)compact.persistentBytes(),(unsigned long long)untracked.persistentBytes());
}
'''
results=[]
with tempfile.TemporaryDirectory(prefix='fr-terrain-tracked-') as temp:
 p=Path(temp);(p/'d3d9.h').write_text(values['stub']);(p/'test.cpp').write_text(source)
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  output=subprocess.check_output([str(p/'test')],text=True)
  results.append(json.loads(output.splitlines()[-1]))
report={'tests':results,'native_fake_D3D':True,'game_launched':False}
(fp.output_dir()/'terrain-tracked-cache-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
