#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Actual terrain capture phase accounting, unchanged geometry and disabled zero-clock path."""
import ast
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

tree=ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
values={n.targets[0].id:ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
stub=values['stub'].replace('using UINT=unsigned;using INT=int;using DWORD=unsigned;using HRESULT=std::int32_t;\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu),E_POINTER=HRESULT(0x80004003u);\ninline bool FAILED(HRESULT value){return value<0;}','#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);')
source=values['harness'].split('int main(){')[0]+r'''
static uint64_t revision=1;
static uint64_t identity(void*,bool index){return index?12:11;}
static uint64_t version(void*,bool){return revision;}
static uint64_t scenario(bool profile){
 Device d;constexpr double origin=17066.666666666666,step=100./3.;int x=547,y=820;
 float loX=float(origin-(y+1)*step),hiX=float(origin-y*step),loY=float(origin-(x+1)*step),hiY=float(origin-x*step);
 d.put(4,{loX,loY,3});d.put(5,{hiX,loY,4});d.put(6,{loX,hiY,5});
 FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);revision=1;
 uint64_t hash=1469598103934665603ull;std::shared_ptr<const MeshSnapshot> held,mesh;
 for(unsigned frame=0;frame<5;++frame){
  cache.clearFrame();assert(!cache.capturePhases().clockReads);cache.profileCapture(profile);
  if(frame==2){++revision;d.put(4,{loX,loY,31});}
  if(frame==3){++revision;std::uint16_t indices[]={123,8,9,7};std::memcpy(d.ib.bytes.data(),indices,sizeof indices);}
  if(frame==4){revision=0;d.put(4,{loX,loY,37});}
  Diagnostics detail;assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,mesh,&detail));
  assert(mesh->positions.size()==3&&mesh->indices.size()==3&&mesh->bounds.chunks.size()==1);
  if(!frame)held=mesh;
  if(profile){const auto& p=cache.capturePhases();assert(p.clockReads==(frame==1?3u:7u));
   if(frame==1){assert(detail.trackedCacheHit&&p.ticks[FrameCache::IndexPhase]==0&&p.ticks[FrameCache::RetainPhase]==0);}
   if(frame==2){const auto& m=cache.maintenance();assert(m.lookupProbes==1&&m.eraseProbes==1&&m.roomChecks==2&&m.decodedComparedBytes>0);}
  }else{assert(!cache.capturePhases().clockReads);for(auto tick:cache.capturePhases().ticks)assert(tick==0);}
  for(const auto& p:mesh->positions)for(float value:{p.x,p.y,p.z}){uint32_t bits;std::memcpy(&bits,&value,4);hash=(hash^bits)*1099511628211ull;}
  for(auto index:mesh->indices)hash=(hash^index)*1099511628211ull;
 }
 assert(held->positions[0].z==3);
 cache.clearFrame();cache.profileCapture(true);assert(!cache.readMeshShared(nullptr,D3DPT_TRIANGLELIST,0,0,0,0,0,nullptr,mesh));assert(cache.capturePhases().clockReads==2);
 ++revision;cache.clearFrame();cache.profileCapture(true);d.vb.failLock=true;assert(!cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,mesh));assert(cache.capturePhases().clockReads==5);
 cache.clearFrame();d.vb.failLock=false;assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,mesh));assert(!cache.capturePhases().clockReads);
 cache.clearPersistent();assert(held->positions[0].z==3&&d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 return hash;
}
int main(){const auto plain=scenario(false),profile=scenario(true);assert(plain==profile);std::printf("TERRAIN_CAPTURE_PHASES_PASS hash=%llu cold7 warm3 reject2 lockFailure5 disabled0 exactGeometry mutation lifetime reset\n",(unsigned long long)plain);}
'''
out=fp.output_dir();(out/'d3d9.h').write_text(stub);(out/'phases.cpp').write_text(source)
for optimization in ('-O2','-O0'):
    exe=out/('phases'+optimization[1:]+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',optimization,'-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(out),*fp.test_include_flags(),str(out/'phases.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
print('PASS production terrain capture uses exclusive phase spans only when sampled; geometry/state/read budgets unchanged')
