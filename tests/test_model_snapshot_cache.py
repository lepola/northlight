#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Persistent model snapshot cache: identity tokens, DYNAMIC exclusion,
revalidation of in-place rewrites, pointer reuse, budget/eviction, and a
cached-vs-uncached timing. Fake device only; no real D3D/Wine/game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,subprocess,tempfile,json,sys
HERE=Path(__file__).resolve().parent
def literal(file,key):return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==key for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub');prefix=literal(HERE/'test_draw_snapshot.py','harness').split('int main()')[0]
body=r'''
#include <chrono>
#include <map>
#include <string>
static std::map<void*,std::uint64_t> tokens;
static std::uint64_t identity(void* p,bool){auto it=tokens.find(p);return it==tokens.end()?0:it->second;}
static std::uint64_t digest(const Mesh& m){std::uint64_t h=0xcbf29ce484222325ull;auto mix=[&](unsigned char v){h^=v;h*=0x100000001b3ull;};
 for(auto& s:m.streams)for(auto b:s.bytes)mix(b);for(auto i:m.indices)for(unsigned j=0;j<4;++j)mix((i>>(j*8))&255);mix(m.vertexCount&255);return h;}
int main(){
 Device d;for(unsigned s=0;s<2;++s)d.vb[s].desc.Usage=D3DUSAGE_WRITEONLY;d.ib.desc.Usage=0;
 tokens[&d.vb[0]]=1;tokens[&d.vb[1]]=2;tokens[&d.ib]=3;
 Frame frame;frame.setIdentityProvider(&identity);Mesh mesh;Diagnostics why;std::shared_ptr<const Mesh> shared;
 Draw draw{D3DPT_TRIANGLELIST,0,0,12,0,1,true};d.indices({7,9,8});d.ib.desc.Usage=0; // Device::indices() marks the IB DYNAMIC
 // (a) static buffers: miss then hit, identical bytes, no locks on the hit.
 frame.clearFrame();std::vector<std::string> phases;PhaseTrace trace{&phases,[](void* context,const char* name){static_cast<std::vector<std::string>*>(context)->emplace_back(name);}};
 assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared,false,trace));
 for(const char* phase:{"snapshot metadata","snapshot index lock","snapshot vertex lock","snapshot cache store"})assert(std::find(phases.begin(),phases.end(),phase)!=phases.end());assert(shared&&mesh.indices.empty()&&mesh.streams[0].bytes.empty());
 const std::size_t missCharge=frame.bytesRead();assert(missCharge>0);
 assert(frame.snapshotCacheMisses()==1&&frame.snapshotCacheHits()==0);const auto first=digest(*shared);const unsigned locksAfterMiss=d.vb[0].locks+d.vb[1].locks+d.ib.locks;
 std::shared_ptr<const Mesh> again;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&again));
 assert(again==shared&&frame.snapshotCacheHits()==1&&frame.snapshotCacheMisses()==0&&digest(*again)==first);
 assert(d.vb[0].locks+d.vb[1].locks+d.ib.locks==locksAfterMiss); // a hit locks nothing
 assert(frame.bytesRead()==missCharge); // a hit charges exactly what the miss reserved
 // Uncached read of the same draw produces identical bytes.
 Mesh plain;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,plain,&why));assert(digest(plain)==first);
 // (b) DYNAMIC buffers are never cached.
 d.vb[1].desc.Usage=D3DUSAGE_DYNAMIC;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));assert(!shared&&mesh.vertexCount==3&&frame.snapshotCacheMisses()==0&&frame.snapshotCacheHits()==0);
 d.vb[1].desc.Usage=D3DUSAGE_WRITEONLY;
 // Unidentified buffers (token 0) are never cached either.
 tokens.erase(&d.ib);frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));assert(!shared&&frame.snapshotCacheHits()==0);tokens[&d.ib]=3;
 // (c) in-place rewrite of a static buffer is caught by the revalidation slice.
 frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));assert(shared&&digest(*shared)==first);
 d.vb[0].bytes[d.offset[0]+7*d.stride[0]+5]^=0x5a;
 bool detected=false;std::uint64_t stale=0;
 for(unsigned f=0;f<40&&!detected;++f){frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));assert(shared);
  if(frame.snapshotRevalidationMismatches()){detected=true;assert(frame.snapshotCacheMisses()==1);Mesh fresh;assert(frame.read(&d,&d.decl,draw,fresh,&why));assert(digest(*shared)==digest(fresh)&&digest(fresh)!=first);}
  else{assert(frame.snapshotCacheHits()==1);stale=digest(*shared);}}
 assert(detected&&stale==first);
 // Index rewrite is caught the same way.
 std::uint16_t v=8;std::memcpy(d.ib.bytes.data(),&v,2);const auto before=digest(*shared);detected=false;
 for(unsigned f=0;f<40&&!detected;++f){frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));if(frame.snapshotRevalidationMismatches()){detected=true;assert(digest(*shared)!=before);}}
 assert(detected);
 // (d) pointer reuse with a new token is a miss; old token entry is separate.
 const auto current=digest(*shared);tokens[&d.vb[0]]=99;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));assert(frame.snapshotCacheMisses()==1&&digest(*shared)==current);
 tokens[&d.vb[0]]=1;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,mesh,&why,false,&shared));
 // (e) budget/eviction: many distinct draws stay under the cache limit.
 frame.clearSnapshotCache();assert(frame.snapshotCacheEntries()==0&&frame.snapshotCacheBytes()==0);
 constexpr unsigned vertices=4096,n=3*8000;d.vb[0].bytes.resize(d.offset[0]+vertices*d.stride[0]);d.vb[1].bytes.resize(d.offset[1]+vertices*d.stride[1]);
 for(unsigned s=0;s<2;++s)for(unsigned i=0;i<d.vb[s].bytes.size();++i)d.vb[s].bytes[i]=std::uint8_t(i*31+s);
 d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.resize(n*2);for(unsigned i=0;i<n;++i){std::uint16_t x=std::uint16_t(i%vertices);std::memcpy(d.ib.bytes.data()+i*2,&x,2);}
 Draw big{D3DPT_TRIANGLELIST,0,0,vertices,0,n/3,true};size_t peak=0;
 for(unsigned i=0;i<2000;++i){frame.clearFrame();Draw variant=big;variant.primitives=n/3-(i%1000);
  std::shared_ptr<const Mesh> m;assert(frame.read(&d,&d.decl,variant,mesh,&why,false,&m));assert(m);peak=std::max(peak,frame.snapshotCacheBytes());assert(frame.snapshotCacheBytes()<=Frame::SnapshotCacheLimit);}
 assert(peak>Frame::SnapshotCacheLimit/2&&frame.snapshotCacheEntries()<2000);
 // Timing: 900 static draws per frame, cached vs uncached, identical digests.
 frame.clearSnapshotCache();Draw bench{D3DPT_TRIANGLELIST,0,0,4096,0,256,true};std::vector<std::shared_ptr<const Mesh>> shares(900);std::vector<Mesh> plains(900);
 auto time=[&](bool cached){auto begin=std::chrono::steady_clock::now();
  for(unsigned f=0;f<20;++f){frame.clearFrame();for(unsigned i=0;i<900;++i){Draw dr=bench;dr.start=(i%30)*768;if(cached)assert(frame.read(&d,&d.decl,dr,mesh,&why,true,&shares[i]));else assert(frame.read(&d,&d.decl,dr,plains[i],&why,true));}}
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/20;};
 double uncached=time(false),cachedMs=time(true);
 for(unsigned i=0;i<900;++i)assert(digest(*shares[i])==digest(plains[i]));
 std::printf("{\"msPer900Uncached\":%.4f,\"msPer900Cached\":%.4f,\"cacheEntries\":%zu,\"cacheBytes\":%zu}\n",uncached,cachedMs,frame.snapshotCacheEntries(),frame.snapshotCacheBytes());
}
'''
report={}
with tempfile.TemporaryDirectory(prefix='northlight-snapshot-cache-') as tmp:
 t=Path(tmp);(t/'d3d9.h').write_text(stub);(t/'draw_snapshot.h').write_text(fp.src('draw_snapshot.h').read_text());(t/'test.cpp').write_text(prefix+body)
 for label,flags in [('native',['-O2']),('sanitizer',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(t),*fp.test_include_flags(),str(t/'test.cpp'),'-o',str(t/'test')],check=True)
  out=subprocess.check_output([str(t/'test')],text=True).strip().splitlines()[-1];report[label]=json.loads(out)
report['speedup']=report['native']['msPer900Uncached']/max(report['native']['msPer900Cached'],1e-9)
(fp.output_dir()/'model-snapshot-cache-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
