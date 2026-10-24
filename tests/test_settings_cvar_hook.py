"""Exercise the production CVar signature gate, including the installed optimizer's detour."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('settings_cvar_bridge.h').read_text()
functions = source[source.index('inline bool clientBytes'):source.index('inline bool cvarClientSupported')]
cpp = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
using SIZE_T=size_t;
using MEMORY_BASIC_INFORMATION=struct {void* AllocationBase;unsigned Type,Protect;};
enum {MEM_IMAGE=0x1000000,PAGE_EXECUTE=16,PAGE_EXECUTE_READ=32,PAGE_EXECUTE_READWRITE=64,PAGE_EXECUTE_WRITECOPY=128};
unsigned char entry[32]={},hook[32]={};
unsigned type=MEM_IMAGE,protection=PAGE_EXECUTE_READ;void* owner=(void*)0x10000000;bool module=true;
void* GetCurrentProcess(){return nullptr;}
void* GetModuleHandleW(const wchar_t*){return module?(void*)0x10000000:nullptr;}
bool ReadProcessMemory(void*,void* address,void* output,size_t count,size_t* got){
    auto value=(std::uintptr_t)address;const unsigned char* data=nullptr;
    if(value>=0x767460&&value+count<=0x767480)data=entry+value-0x767460;
    if(value>=0x10080730&&value+count<=0x10080750)data=hook+value-0x10080730;
    if(!data)return false;std::memcpy(output,data,count);*got=count;return true;
}
size_t VirtualQuery(void* address,MEMORY_BASIC_INFORMATION* page,size_t size){
    if((std::uintptr_t)address!=0x10080730)return 0;*page={owner,type,protection};return size;
}
'''+functions+r'''
int main(){
    const unsigned char original[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c,0x84,0xdb,0x56,0x57,0x8b,0x7d,0x08,0x8b,0xf1};
    const unsigned char optimizer[]={0x55,0x8b,0xec,0x51,0x53,0x8b,0xd9,0x89,0x55,0xfc,0x56,0x8b,0x75,0x08,0x8b,0xce};
    auto accepted=[&]{return cvarSetterSupported(0x767460,original,sizeof original);};
    std::memcpy(entry,original,sizeof original);assert(accepted());
    entry[0]=0xcc;assert(!accepted());entry[0]=0xe9;
    std::int32_t relative=0x10080730-0x767465;std::memcpy(entry+1,&relative,4);std::memcpy(hook,optimizer,sizeof optimizer);assert(accepted());
    entry[8]^=1;assert(!accepted());entry[8]^=1;
    module=false;assert(!accepted());module=true;
    owner=(void*)0x20000000;assert(!accepted());owner=(void*)0x10000000;
    type=0x20000;assert(!accepted());type=MEM_IMAGE;
    protection=4;assert(!accepted());protection=PAGE_EXECUTE_READ;
    hook[7]^=1;assert(!accepted());hook[7]^=1;
    relative=0x10080731-0x767465;std::memcpy(entry+1,&relative,4);assert(!accepted());
}
'''
out = fp.out()/'cvar-hook-test'
out.mkdir(exist_ok=True)
(out/'test.cpp').write_text(cpp)
env = fp.zig_env()
env.update(TEMP=str(fp.out()/'tmp'), TMP=str(fp.out()/'tmp'))
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', str(out/'test.cpp'), '-o', str(out/'test.exe')], env=env, check=True)
subprocess.run([str(out/'test.exe')], check=True)
print('CVAR_HOOK_PASS original, known optimizer detour, corrupt tail/body, foreign/missing module, non-image/non-executable and wrong destination')
