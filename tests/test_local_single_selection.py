#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Execute the production optional single-light selector with failure injection."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
source=fp.src('world_renderer.h').read_text();begin=source.index('    IDirect3DPixelShader9* localDirectShader(unsigned count){');end=source.index('\n    IDirect3DPixelShader9* fogVolumeShader(',begin)
assert 'localDirectShader(batch.count)' in source
assert 'drop(localDirectSinglePS);localDirectSingleRefused=false;' in source
method=source[begin:end]
harness=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using HRESULT=std::int32_t;using DWORD=unsigned;
#define FAILED(x) ((x)<0)
static constexpr DWORD kLocalDirect1Shader[]={123};
struct IDirect3DPixelShader9 {unsigned releases=0;void Release(){++releases;}};
static void drop(IDirect3DPixelShader9*& p){if(p)p->Release();p=nullptr;}
struct Device {unsigned mode=0,calls=0;IDirect3DPixelShader9 shader;
 HRESULT CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** p){assert(code==kLocalDirect1Shader);++calls;*p=mode<2?&shader:nullptr;return mode==1||mode==3?HRESULT(-1):0;}
};
struct Renderer {
 Device* d;IDirect3DPixelShader9 fallback;IDirect3DPixelShader9* localDirectPS=&fallback,*localDirectSinglePS=nullptr;bool localDirectSingleRefused=false;
 METHOD
};
int main(){for(unsigned mode=0;mode<4;++mode){Device device;device.mode=mode;Renderer w;w.d=&device;
 for(unsigned count=0;count<137;++count)if(count!=1)assert(w.localDirectShader(count)==w.localDirectPS);assert(!device.calls);
 auto* first=w.localDirectShader(1);assert(device.calls==1);if(!mode)assert(first==&device.shader&&!w.localDirectSingleRefused);else assert(first==w.localDirectPS&&w.localDirectSingleRefused&&!w.localDirectSinglePS);
 for(unsigned i=0;i<1000;++i)assert(w.localDirectShader(1)==first);assert(device.calls==1);assert(device.shader.releases==(mode==1));
 drop(w.localDirectSinglePS);w.localDirectSingleRefused=false;assert(w.localDirectShader(1)==first&&device.calls==2);
 }std::puts("PASS production single-light selector: 4 HRESULT/null modes, exact count, cached reuse, failure release, retry reset, full shader fallback");}
'''.replace('METHOD',method)
(out/'selector.cpp').write_text(harness)
for opt in ['2','0']:
    exe=out/('selector-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-std=c++17',str(out/'selector.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    r=subprocess.run([str(exe)],check=True,capture_output=True,text=True);(out/('O'+opt+'.log')).write_text(r.stdout);print(r.stdout,end='')
