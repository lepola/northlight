#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Compare shadow pages with actual fragmented address space in a separate x86 process."""
from pathlib import Path
import argparse,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path);args=parser.parse_args()
out=args.output or fp.out()/'segmented-shadow-fragmentation'
if out.exists() and any(out.iterdir()):raise RuntimeError('Output must be absent or empty: '+str(out))
out.mkdir(parents=True,exist_ok=True)
s=(fp.TESTS/'test_shadow_paging_memory.cpp').read_text()
s=s.replace('int main(int argc,char**argv){','int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);')
prefix=r'''
#define FRAGMENTED_STAND 1
#include <windows.h>
#undef far
#include <algorithm>
#include <cassert>
#include <vector>
#include "geometry_memory.h"
using namespace NorthlightGeometryMemory;
static Sample sample(){SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;Sample s;s.valid=true;
 for(uint64_t p=0;p<limit;){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((const void*)uintptr_t(p),&m,sizeof m)){s.valid=false;break;}const uint64_t end=uint64_t(uintptr_t(m.BaseAddress))+m.RegionSize;if(end<=p){s.valid=false;break;}if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max(s.largest,uint64_t(m.RegionSize));}p=end;}return s;}
struct Fragment{std::vector<void*> blocks;Fragment(){blocks.reserve(256);SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
 for(uint64_t p=0;p<limit;){MEMORY_BASIC_INFORMATION m{};assert(VirtualQuery((const void*)uintptr_t(p),&m,sizeof m));const uint64_t end=uint64_t(uintptr_t(m.BaseAddress))+m.RegionSize;assert(end>p);
  if(m.State==MEM_FREE){const uint64_t base=(uint64_t(uintptr_t(m.BaseAddress))+65535)&~uint64_t(65535);for(uint64_t x=base+40*MiB;x+65536<=end;x+=40*MiB){void* v=VirtualAlloc((void*)uintptr_t(x),65536,MEM_RESERVE,PAGE_NOACCESS);assert(v);blocks.push_back(v);}}p=end;}}
 ~Fragment(){for(void* v:blocks)assert(VirtualFree(v,0,MEM_RELEASE));}};
'''
s=prefix+s
old='    NorthlightWorldMesh::WorldMeshUploadPlan segmented;\n    unsigned refused=0;auto limit=[&](uint64_t n){if(n>8ull*1024*1024){++refused;return false;}return true;};'
new='''    const auto before=sample();Fragment fragment;const auto fragmented=sample();
    std::printf("FRAGMENT beforeFreeMiB=%llu beforeLargestMiB=%llu afterFreeMiB=%llu afterLargestMiB=%llu blocks=%zu\\n",(unsigned long long)(before.available>>20),(unsigned long long)(before.largest>>20),(unsigned long long)(fragmented.available>>20),(unsigned long long)(fragmented.largest>>20),fragment.blocks.size());
    assert(fragmented.valid&&fragmented.available>384*MiB&&fragmented.largest<=41*MiB);
    NorthlightWorldMesh::WorldMeshUploadPlan segmented;
    unsigned refused=0;auto limit=[&](uint64_t n){if(n<MiB)return true;const auto current=sample();const auto budget=inBuildBudget(n);if(!admits(current,budget)){++refused;std::printf("REFUSED requestMiB=%.3f availableMiB=%.3f largestMiB=%.3f requiredMiB=%.3f\\n",double(n)/MiB,double(current.available)/MiB,double(current.largest)/MiB,double(budget.largest)/MiB);return false;}return true;};'''
assert s.count(old)==1;s=s.replace(old,new)
s=s.replace('PASS segmented shadow scene capMiB=8','PASS segmented shadow scene fragmented=1')
(out/'fragmented.cpp').write_text(s)
exe=out/'fragmented.exe'
cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(out/'fragmented.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)]
subprocess.run(cmd,env=fp.zig_env(),check=True)
text=subprocess.check_output([str(exe),str(fp.world_cache())],text=True);(out/'run.log').write_text(text);print(text)
