#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Replay unchanged terrain XYZ with changing buffer revisions, sharing or rescanning material bounds."""
import argparse,json,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=fp.out()/'shared-terrain-bounds-bench');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
prefix=(fp.TESTS/'position_bounds.cpp').read_text().split('int main(){')[0]
source=prefix+r'''
#include "terrain_capture_bounds.h"
#include <chrono>
struct Indices:ForwardIDirect3DIndexBuffer9 {
 unsigned refs=1;std::vector<uint16_t> data;
 Indices():ForwardIDirect3DIndexBuffer9(nullptr){}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
 HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* p)override{*p={};p->Size=UINT(data.size()*2);p->Format=D3DFMT_INDEX16;return 0;}
 HRESULT STDMETHODCALLTYPE Lock(UINT start,UINT bytes,void** p,DWORD)override{assert(uint64_t(start)+bytes<=data.size()*2);*p=reinterpret_cast<char*>(data.data())+start;return 0;}
 HRESULT STDMETHODCALLTYPE Unlock()override{return 0;}
};
struct Declaration:IDirect3DVertexDeclaration9 {
 unsigned refs=1;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
 HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
 HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
  D3DVERTEXELEMENT9 e[]={{0,4,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
  if(out)std::memcpy(out,e,sizeof e);*count=2;return 0;
 }
};
struct TerrainDevice:Device {
 Indices ib;Declaration decl;
 HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** p)override{*p=&ib;ib.AddRef();return 0;}
 HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** p)override{*p=&decl;decl.AddRef();return 0;}
};
static uint64_t identity(void* p,bool index){return index?77:static_cast<Buffer*>(p)->identity;}
static uint64_t revision(void* p,bool index){return index?1:static_cast<Buffer*>(p)->revision;}
int main(){
 TerrainDevice d;d.vb.data.resize(20+700*290*48);int rows[17];unsigned vertex=0;
 for(unsigned row=0;row<17;++row){rows[row]=int(vertex);vertex+=(row&1)?8:9;}assert(vertex==145);
 for(unsigned chunk=0;chunk<2;++chunk)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned a=chunk*145+unsigned(rows[y*2]+x),b=a+1,c=chunk*145+unsigned(rows[y*2+2]+x),e=c+1,m=chunk*145+unsigned(rows[y*2+1]+x);for(unsigned i:{a,b,m,b,e,m,e,c,m,c,a,m})d.ib.data.push_back(uint16_t(i));}
 assert(d.ib.data.size()==1536);
 for(unsigned draw=0;draw<700;++draw)for(unsigned chunk=0;chunk<2;++chunk){const double origin=17066.666666666666,step=100./3.,x0=origin-(102+2*(draw%300)+chunk)*step,y0=origin-(101+draw/300)*step;
  unsigned v=0;for(unsigned row=0;row<17;++row)for(unsigned col=0;col<((row&1)?8u:9u);++col)d.vb.position(draw*290+chunk*145+v++,float(x0+(col+.5*(row&1))*step/8),float(y0+row*step/16),float(draw%17)+float(row)*.1f);
 }
 struct Trial {NorthlightTerrainCapture::FrameCache world;NorthlightPositionBounds::Cache material;Trial(){world.setIdentityProvider(identity);world.setVersionProvider(revision);material.tracking(nullptr,nullptr,metadata);}} baseline,shared;
 // Simulate lighting/attribute writes: revisions change, every referenced XYZ stays exact.
 auto run=[&](Trial& trial,bool share){++d.vb.revision;for(unsigned draw=0;draw<700;++draw)d.vb.data[16+draw*290*48+16]^=1;trial.world.clearFrame();trial.material.beginFrame();unsigned known=0;uint64_t digest=0;const auto locks=d.vb.locks;const auto start=std::chrono::steady_clock::now();
  for(unsigned draw=0;draw<700;++draw){NorthlightDrawSnapshot::Draw args{D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,true};std::shared_ptr<const NorthlightTerrainCapture::MeshSnapshot> snapshot;NorthlightTerrainCapture::Diagnostics why;
   assert(trial.world.readMeshShared(&d,args.topology,args.base,args.minimum,args.vertices,args.start,args.primitives,nullptr,snapshot,&why));
   float low[3],high[3];bool ok=true;if(share){std::copy(why.low,why.low+3,low);std::copy(why.high,why.high+3,high);}else ok=trial.material.read(&d,args,4,low,high);
   if(ok){++known;assert(!std::memcmp(low,why.low,sizeof low)&&!std::memcmp(high,why.high,sizeof high));}
   for(float value:{why.low[0],why.low[1],why.low[2],why.high[0],why.high[1],why.high[2]}){uint32_t bits;std::memcpy(&bits,&value,4);digest=digest*1099511628211ull+bits;}
  }
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  assert(trial.world.persistentBytes()<=(32u<<20)&&trial.material.count()<=4096);
  std::printf("SHARED_BOUNDS_BENCH shared=%u ms=%.6f known=%u scans=%u bytes=%zu rejected=%u locks=%u digest=%016llx\n",unsigned(share),ms,known,trial.material.scans(),trial.material.bytesRead(),trial.material.maintenance().budgetRejected,d.vb.locks-locks,(unsigned long long)digest);
  assert(share?known==700&&trial.material.scans()==0&&trial.material.bytesRead()==0:known<700&&trial.material.maintenance().budgetRejected==700-known);
 };
 for(unsigned pair=0;pair<20;++pair){if(pair&1){run(shared,true);run(baseline,false);}else{run(baseline,false);run(shared,true);}}
 assert(d.vb.refs==1&&d.ib.refs==1&&d.decl.refs==1);
}
'''
cpp=a.output/'benchmark.cpp';cpp.write_text(source);exe=a.output/'benchmark.exe'
command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(cpp),'-o',str(exe)]
with (a.output/'compile.log').open('w') as log:subprocess.run(command,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,check=True);(a.output/'benchmark.log').write_text(run.stdout+run.stderr)
rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in run.stdout.splitlines() if line.startswith('SHARED_BOUNDS_BENCH')]
assert len(rows)==40 and len({r['digest'] for r in rows})==1
# Exclude each cache's first cold population; alternate order for all warm pairs.
medians={k:statistics.median(float(r['ms']) for r in rows[2:] if r['shared']==k) for k in ('0','1')}
report=dict(component='terrain exact revalidation plus material range proof; not whole-frame FPS',medians_ms=medians,ratio=medians['1']/medians['0'],rows=rows)
(a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='rows'}),flush=True)
