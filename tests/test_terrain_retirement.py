#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Actual terrain cache: bounded CPU retirement, reset/refusal/allocation fallbacks and concurrent reuse."""
import argparse,ast,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'terrain-retirement');p.add_argument('--benchmark',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
nodes=ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text()).body
values={n.targets[0].id:ast.literal_eval(n.value) for n in nodes if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
(a.output/'d3d9.h').write_text(values['stub'])
source=r'''
#include <cstdlib>
#include <new>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
static thread_local bool refuseAllocation=false;
#ifndef NORTHLIGHT_TERRAIN_TSAN
void* operator new(std::size_t n){if(refuseAllocation)throw std::bad_alloc();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#endif
'''+values['harness'].split('int main(){')[0]+r'''
static uint64_t identity(void*,bool index){return index?2:1;}
static uint64_t rev=1;static uint64_t version(void*,bool index){return index?1:rev;}
static void geometry(Device& d){
 d.vb.bytes.resize(16+7000*290*24);std::vector<uint16_t> indices;int rows[17];unsigned vertex=0;
 for(unsigned row=0;row<17;++row){rows[row]=int(vertex);vertex+=(row&1)?8:9;}assert(vertex==145);
 for(unsigned chunk=0;chunk<2;++chunk)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned a=chunk*145+unsigned(rows[y*2]+x),b=a+1,c=chunk*145+unsigned(rows[y*2+2]+x),e=c+1,m=chunk*145+unsigned(rows[y*2+1]+x);for(unsigned i:{a,b,m,b,e,m,e,c,m,c,a,m})indices.push_back(uint16_t(i));}
 d.ib.bytes.resize(indices.size()*2);std::memcpy(d.ib.bytes.data(),indices.data(),d.ib.bytes.size());
 for(unsigned draw=0;draw<7000;++draw)for(unsigned chunk=0;chunk<2;++chunk){const double origin=17066.666666666666,step=100./3.,x0=origin-(102+2*(draw%300)+chunk)*step,y0=origin-(101+draw/300)*step;
  unsigned v=0;for(unsigned row=0;row<17;++row)for(unsigned col=0;col<((row&1)?8u:9u);++col)d.put(draw*290+chunk*145+v++,{float(x0+(col+.5*(row&1))*step/8),float(y0+row*step/16),float(draw%17)+float(row)*.1f});}
}
static std::shared_ptr<const MeshSnapshot> read(FrameCache& c,Device& d,unsigned draw){
 std::shared_ptr<const MeshSnapshot> s;assert(c.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,d.view,s));return s;
}
static void seed(FrameCache& c,Device& d){
 c.setIdentityProvider(identity);c.setVersionProvider(version);
 for(unsigned i=0;i<7000;++i){if(i%200==0)c.clearFrame();read(c,d,i);}c.clearFrame();
 assert(c.persistentEntries()>5000&&c.persistentBytes()<=(32u<<20));
 assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
}
struct Block {std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;};
struct Wait {Block& block;explicit Wait(Block& b):block(b){}~Wait(){std::unique_lock<std::mutex> lock(block.mutex);block.entered=true;block.changed.notify_all();block.changed.wait(lock,[&]{return block.released;});}};
static void blockWorker(NorthlightStreaming::CpuRetirement& queue,Block& b,size_t bytes){
 auto owner=std::make_shared<Wait>(b);assert(queue.retire(owner,bytes));std::unique_lock<std::mutex> lock(b.mutex);b.changed.wait(lock,[&]{return b.entered;});
}
static void release(Block& b){std::lock_guard<std::mutex> lock(b.mutex);b.released=true;b.changed.notify_all();}
static uint64_t digest(const MeshSnapshot& s){uint64_t h=0;for(const auto& p:s.positions)for(float v:{p.x,p.y,p.z}){uint32_t u;std::memcpy(&u,&v,4);h=h*1099511628211ull+u;}for(auto i:s.indices)h=h*1099511628211ull+i;return h;}
int main(int argc,char**){
 Device d;geometry(d);NorthlightStreaming::CpuRetirement queue;
 for(unsigned mode=0;mode<7;++mode){
#ifdef NORTHLIGHT_TERRAIN_TSAN
 if(mode==3)continue; // Allocation injection is covered by O2/O0/ASan; TSan owns new/delete.
#endif
 FrameCache c;seed(c,d);auto held=read(c,d,6999);const auto saved=digest(*held),live=c.sourceLiveBytes();const auto cap=c.persistentByteLimit();size_t deferred=0;Block blocked;
  if(mode==1||mode==4||mode==6)blockWorker(queue,blocked,1);else if(mode==2)blockWorker(queue,blocked,128u<<20);
  const auto start=std::chrono::steady_clock::now();
  if(mode==3)refuseAllocation=true;
  if(mode>=4){NorthlightStreaming::CpuRetirementBatch batch(&queue);deferred=c.clearPersistent(&queue,&batch);assert(deferred&&batch.count()==1&&batch.bytes()==deferred);
   if(mode>=5){auto other=std::make_shared<int>(3);assert(batch.retire(other,128u<<20));}
   const bool all=batch.finish();assert(all==(mode!=6));
   if(mode==5)assert(queue.stats().oversized>0);if(mode==6)assert(batch.queuedBytes()==deferred);
  }
  else deferred=c.clearPersistent(mode?&queue:nullptr);
  refuseAllocation=false;
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  assert(deferred==0||deferred<=32u*1024u*1024u+65536u);
  assert(!c.persistentEntries()&&c.persistentByteLimit()==cap&&digest(*held)==saved);
  if(mode==1||mode==4||mode==6){assert(deferred&&c.sourceLiveBytes()==live&&"TERRAIN_RETIREMENT_DID_NOT_DEFER");}
  else if(mode!=5)assert(!deferred&&c.sourceLiveBytes()<live&&"TERRAIN_SYNC_FALLBACK_MISSING");
  // A detached cache cannot republish an old snapshot, even with identical bytes.
  auto fresh=read(c,d,6999);assert(fresh!=held&&digest(*fresh)==saved);
  ++rev;auto moved=held->positions[0];moved.z+=13;d.put(6999*290,moved);auto changed=read(c,d,6999);assert(changed->positions[0].z==moved.z&&digest(*held)==saved);d.put(6999*290,held->positions[0]);++rev;
  if(mode==1||mode==2||mode==4||mode==6)release(blocked);assert(queue.waitIdle(10000));
  assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
  std::printf("TERRAIN_RETIREMENT mode=%u deferredBytes=%zu boundaryMs=%.6f\n",mode,deferred,ms);
 }
 // Clear/repopulate while the worker destroys the previous immutable generation.
 for(unsigned i=0;i<3;++i){FrameCache c;seed(c,d);assert(c.clearPersistent(&queue));for(unsigned draw=0;draw<200;++draw)read(c,d,draw);assert(queue.waitIdle(10000));}
 if(argc>1)for(unsigned pair=0;pair<10;++pair)for(unsigned n=0;n<2;++n){const bool async=(pair+n)&1;FrameCache c;seed(c,d);assert(queue.waitIdle(10000));const auto start=std::chrono::steady_clock::now();const auto deferred=c.clearPersistent(async?&queue:nullptr);const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();assert(bool(deferred)==async);assert(queue.waitIdle(10000));std::printf("TERRAIN_CLEAR_BENCH async=%u ms=%.6f\n",unsigned(async),ms);}
 std::puts("TERRAIN_RETIREMENT_PASS reset critical allocation_refusal busy_byte_cap exact_rebuild held_snapshot concurrent_capture bounded_batch oversized_idle busy_batch_individual_fallback no_COM_owners");
}
'''
(a.output/'retirement.cpp').write_text(source)
configs=[('O2',['-O2'])] if a.benchmark else [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined']),('tsan',['-O1','-g','-fsanitize=thread','-DNORTHLIGHT_TERRAIN_TSAN','-fno-pie','-no-pie'])]
for label,flags in configs:
 exe=a.output/label
 with (a.output/(label+'-compile.log')).open('w') as log:subprocess.run(['clang++','-std=c++17','-pthread','-UNDEBUG',*flags,'-I',str(a.output),*fp.test_include_flags(),str(a.output/'retirement.cpp'),'-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
 run=subprocess.run([str(exe),*(['benchmark'] if a.benchmark else [])],capture_output=True,text=True,timeout=180)
 (a.output/(label+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0,(label,run.stdout[-1000:],run.stderr[-2000:]);assert 'TERRAIN_RETIREMENT_PASS' in run.stdout
 print(label,'TERRAIN_RETIREMENT_PASS',flush=True)

if not a.benchmark:
    negative=a.output/'negative';negative.mkdir(exist_ok=True)
    header=fp.src('terrain_capture_bounds.h').read_text()
    old='if(batch?batch->retire(retired,bytes):retirement->retire(retired,bytes))deferred=bytes;'
    assert header.count(old)==1
    (negative/'terrain_capture_bounds.h').write_text(header.replace(old,'if(false)deferred=bytes;'))
    exe=negative/'disabled'
    with (negative/'compile.log').open('w') as log:subprocess.run(['clang++','-std=c++17','-pthread','-O2','-UNDEBUG','-I',str(negative),'-I',str(a.output),*fp.test_include_flags(),str(a.output/'retirement.cpp'),'-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=180)
    (negative/'disabled.log').write_text(run.stdout+run.stderr)
    assert run.returncode!=0 and 'TERRAIN_RETIREMENT_DID_NOT_DEFER' in run.stderr
    print('PASS disabled-retirement destructive control rejected',flush=True)
