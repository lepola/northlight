#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Verify immutable terrain sharing, ownership and CPU cost without D3D/Wine/game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import ast,hashlib,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
def literal(file,name):
 return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
fixture=literal(HERE/'test_terrain_snapshot.py','harness').split('int main(){')[0]
body=r'''
#include <type_traits>
#include <chrono>
void ownership(){
 Device d;d.put(4,{-10200,-1180,3});d.put(5,{-10198,-1180,4});d.put(6,{-10200,-1178,5});
 FrameCache cache;Diagnostics why;std::shared_ptr<const MeshSnapshot> first,out;
 static_assert(std::is_const<typename decltype(out)::element_type>::value,"snapshot API must be immutable");
 auto read=[&](UINT count=3){return cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,count,1,1,d.view,out,&why);};
 assert(read()&&!why.contentCacheHit);first=out;auto vertices=out->positions.data();auto indices=out->indices.storageIdentity();const auto bytes=cache.bytesRead();
 unsigned locks=d.vb.locks;cache.clearFrame();assert(read()&&why.contentCacheHit&&out==first);
 assert(out->positions.data()==vertices&&out->indices.storageIdentity()==indices&&cache.bytesRead()==bytes&&d.vb.locks==locks+1);
 MeshSnapshot value;assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,value,&why));
 assert(value.positions.data()!=vertices&&value.indices.storageIdentity()!=indices);value.positions[0].z=999;value.indices.set(0,999);value.bounds.chunks.clear();
 assert(read()&&out==first&&out->positions[0].z==3&&out->indices[0]==0&&out->bounds.chunks.size()==1);
 d.put(4,{-10200,-1180,30});assert(read()&&!why.contentCacheHit&&out!=first&&first->positions[0].z==3&&out->positions[0].z==30);
 auto changed=out;std::uint16_t swap[]={123,8,7,9};std::memcpy(d.ib.bytes.data(),swap,sizeof swap);
 assert(read()&&!why.contentCacheHit&&out!=changed&&out->positions[0].x==-10198&&changed->positions[0].x==-10200);
 d.setIndices16();assert(read()&&!why.contentCacheHit);d.vb.failLock=true;
 assert(!read()&&!out&&why.reason==RejectReason::VertexLock&&first->positions[0].z==3);d.vb.failLock=false;
 float wrong[16];std::memcpy(wrong,d.view,64);wrong[12]=1;out=first;
 assert(!cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,wrong,out,&why)&&!out&&why.reason==RejectReason::ViewMismatch);
 cache.clearPersistent();cache.clearFrame();assert(read());auto retained=out;std::weak_ptr<const MeshSnapshot> weak=retained;
 for(unsigned i=1;i<=FrameCache::persistentEntryLimit()+88;++i){cache.clearFrame();assert(read(3+i));assert(cache.persistentEntries()<=FrameCache::persistentEntryLimit()&&cache.persistentBytes()<=32u*1024u*1024u);}
 assert(retained->positions[0].z==30&&!weak.expired());cache.clearPersistent();assert(retained->positions[0].z==30);
 retained.reset();assert(weak.expired()); // Evicted payload has no hidden retained owner.
 assert(first->positions[0].z==3);first.reset();changed.reset();out.reset();
 Limits limit;limit.maxReadBytesPerFrame=bytes;FrameCache budget(limit);
 assert(budget.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why));auto kept=out;
 assert(!budget.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&!out&&why.reason==RejectReason::ByteBudget);
 budget.clearFrame();assert(budget.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&why.contentCacheHit&&out==kept);
 limit.maxReadBytesPerFrame=16u*1024u*1024u;limit.maxEntries=1;FrameCache capped(limit);
 assert(capped.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why));
 assert(!capped.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&!out&&why.reason==RejectReason::CaptureLimit);
 assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks&&d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
}
int main(){ownership();
 Device d;d.vb.bytes.resize(16+145*24);int rows[17];unsigned vertex=0;const double origin=17066.666666666666,step=100./3.;
 const double x0=origin-821*step,y0=origin-548*step;
 for(int row=0;row<17;++row){rows[row]=int(vertex);for(int col=0;col<((row&1)?8:9);++col)d.put(vertex++,{float(x0+(col+.5*(row&1))*step/8),float(y0+row*step/16),float(std::sin(row*.23+col*.13)*3)});}
 std::vector<std::uint16_t> list;
 for(int y=0;y<8;++y)for(int x=0;x<8;++x){unsigned a=unsigned(rows[y*2]+x),b=a+1,c=unsigned(rows[y*2+2]+x),e=c+1,m=unsigned(rows[y*2+1]+x);for(unsigned index:{a,b,m,b,e,m,e,c,m,c,a,m})list.push_back(std::uint16_t(index));}
 assert(vertex==145&&list.size()==768);d.ib.bytes.resize(list.size()*2*256);for(unsigned i=0;i<256;++i)std::memcpy(d.ib.bytes.data()+i*list.size()*2,list.data(),list.size()*2);
 FrameCache cache;std::vector<std::shared_ptr<const MeshSnapshot>> held(256);
 for(unsigned i=0;i<256;++i){cache.clearFrame();assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,145,i*768,256,d.view,held[i]));}
 for(unsigned draw=0;draw<256;++draw){cache.clearFrame();MeshSnapshot out;assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,0,0,145,draw*768,256,d.view,out));assert(out.indices==held[draw]->indices);assert(out.positions.size()==held[draw]->positions.size());assert(!std::memcmp(out.positions.data(),held[draw]->positions.data(),out.positions.size()*sizeof(Position)));}
 auto bench=[&](bool shared){const unsigned iterations=10000;std::uint64_t total=0;auto begin=std::chrono::steady_clock::now();
  for(unsigned i=0;i<iterations;++i){cache.clearFrame();unsigned draw=i%256;if(shared){std::shared_ptr<const MeshSnapshot> out;assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,145,draw*768,256,d.view,out));assert(out==held[draw]);total+=out->indices.size();}
   else{MeshSnapshot out;assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,0,0,145,draw*768,256,d.view,out));total+=out.indices.size();}}
  assert(total==std::uint64_t(iterations)*768);return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/iterations;};
 double value[5],shared[5];for(unsigned i=0;i<5;++i){value[i]=bench(false);shared[i]=bench(true);}std::sort(value,value+5);std::sort(shared,shared+5);
 std::printf("{\"value_us_per_draw\":%.6f,\"shared_us_per_draw\":%.6f,\"same_snapshot_identity\":true,\"immutable_lifetime_tests\":true}\n",value[2],shared[2]);
}
'''
report={}
with tempfile.TemporaryDirectory(prefix='northlight-terrain-shared-') as temp:
 p=Path(temp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(fixture+body)
 for label,flags in [('native',['-O2']),('sanitizer',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  result=subprocess.run([str(p/'test')],capture_output=True,text=True);print(label+': '+result.stdout+result.stderr,end='');result.check_returncode();report[label]=json.loads(result.stdout)
report['fixture']='256 terrain draw contracts,145 vertices/256triangles,10000 verified hits per sample,5samples; native ARM64 fakeD3D, not game/Wine timings. Full geometry equivalence checked outside timed loops.'
report['source_sha256']=hashlib.sha256(fp.src('terrain_capture_bounds.h').read_bytes()).hexdigest()
report['cache_limit_bytes']=33554432;report['caller_retained_outputs']='Immutable shared outputs survive cache eviction/reset; caller must bound its own retained output set.'
(fp.output_dir()/'terrain-shared-snapshot-validation.json').write_text(json.dumps(report,indent=2)+'\n')
