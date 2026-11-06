#!/usr/bin/env python3
# northlight-test: requires=zig
"""Exercise the production cloud upload gate with simulated VA and device failures."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

host = fp.src('world_renderer.h').read_text()
body = host[host.index('    bool ensureCloudNoise(){'):host.index('    unsigned localDirectCount=')]
source = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "geometry_memory.h"
using DWORD=uint32_t; using HRESULT=int32_t;
constexpr HRESULT E_OUTOFMEMORY=-1,D3DERR_OUTOFVIDEOMEMORY=-2,UNSUPPORTED=-3;
constexpr int D3DFMT_L8=1,D3DFMT_A8R8G8B8=2,D3DPOOL_MANAGED=0;
#define FAILED(hr) ((hr)<0)
#define SUCCEEDED(hr) ((hr)>=0)
static DWORD ticks=100; static DWORD GetTickCount(){return ticks;}
namespace NorthlightFogClouds {constexpr unsigned N=64;}
namespace NorthlightDiagnostics {bool enabled(){return true;}}
struct D3DLOCKED_BOX {void* pBits=nullptr;int RowPitch=0,SlicePitch=0;};
struct IDirect3DVolumeTexture9 {
 std::vector<uint8_t> data=std::vector<uint8_t>(64*64*64*4);
 HRESULT LockBox(int,D3DLOCKED_BOX* b,void*,int){b->pBits=data.data();b->RowPitch=256;b->SlicePitch=64*256;return 0;}
 void UnlockBox(int){} void Release(){delete this;}
};
struct Device {
 int calls=0;HRESULT l8=0,fallback=0;
 HRESULT CreateVolumeTexture(unsigned,unsigned,unsigned,int,int,int fmt,int,IDirect3DVolumeTexture9** t,void*){
  ++calls;HRESULT hr=fmt==D3DFMT_L8?l8:fallback;if(hr>=0)*t=new IDirect3DVolumeTexture9;return hr;
 }
};
static void logf(const char*,...){}
struct Renderer {
 struct Noise {std::atomic<bool> ready{true};std::vector<uint8_t> data=std::vector<uint8_t>(64*64*64,17);} noise;
 Noise& fogCloudNoise(){return noise;}
 Device device;Device* d=&device;IDirect3DVolumeTexture9* cloudNoise=nullptr;
 bool cloudNoiseFailed=false,cloudNoiseRetry=false,cloudNoiseReported=false,memoryPressure=true;
 DWORD cloudNoiseRetryAt=0,cloudNoiseReportAt=0;unsigned samples=0;
 NorthlightGeometryMemory::Sample sample{401*NorthlightGeometryMemory::MiB,187*NorthlightGeometryMemory::MiB,true};
 NorthlightGeometryMemory::Sample geometryAdmission(NorthlightGeometryMemory::Budget b,bool* exact){
  ++samples;*exact=true;assert(b.available==259*NorthlightGeometryMemory::MiB&&b.largest==18*NorthlightGeometryMemory::MiB);return sample;
 }
 static void logGeometryMemory(const char*,NorthlightGeometryMemory::Sample,size_t,bool,const NorthlightGeometryMemory::Budget*){}
 @BODY@
 ~Renderer(){if(cloudNoise)cloudNoise->Release();}
};
int main(){
 {Renderer r;assert(r.ensureCloudNoise()&&r.device.calls==1&&r.samples==1);r.sample={};assert(r.ensureCloudNoise()&&r.samples==1);}
 {Renderer r;r.memoryPressure=false;r.sample={};assert(r.ensureCloudNoise()&&r.samples==0);}
 {Renderer r;r.sample.available=259*NorthlightGeometryMemory::MiB;r.sample.largest=18*NorthlightGeometryMemory::MiB;assert(r.ensureCloudNoise());}
 {Renderer r;r.sample.available=258*NorthlightGeometryMemory::MiB;assert(!r.ensureCloudNoise()&&r.device.calls==0);r.sample.available=401*NorthlightGeometryMemory::MiB;
  ticks+=999;assert(!r.ensureCloudNoise()&&r.samples==1);++ticks;assert(r.ensureCloudNoise()&&r.samples==2);}
 {Renderer r;r.sample.largest=17*NorthlightGeometryMemory::MiB;assert(!r.ensureCloudNoise()&&r.device.calls==0);}
 {Renderer r;r.sample.valid=false;assert(!r.ensureCloudNoise()&&r.device.calls==0);}
 {Renderer r;r.device.l8=UNSUPPORTED;assert(r.ensureCloudNoise()&&r.device.calls==2);}
 {Renderer r;r.device.l8=E_OUTOFMEMORY;assert(!r.ensureCloudNoise()&&!r.cloudNoiseFailed&&r.device.calls==1);r.device.l8=0;
  ticks+=1000;assert(r.ensureCloudNoise()&&r.device.calls==2);}
 {Renderer r;r.device.l8=UNSUPPORTED;r.device.fallback=D3DERR_OUTOFVIDEOMEMORY;assert(!r.ensureCloudNoise()&&!r.cloudNoiseFailed&&r.device.calls==2);r.device.fallback=0;
  ticks+=1000;assert(r.ensureCloudNoise()&&r.device.calls==4);}
 {Renderer r;r.device.l8=r.device.fallback=UNSUPPORTED;assert(!r.ensureCloudNoise()&&r.cloudNoiseFailed&&r.device.calls==2);ticks+=1000;assert(!r.ensureCloudNoise()&&r.device.calls==2);}
}
'''.replace('@BODY@', body)

with tempfile.TemporaryDirectory(prefix='cloud-noise-') as folder:
    cpp = Path(folder) / 'cloud.cpp'
    exe = Path(folder) / 'cloud.exe'
    cpp.write_text(source)
    subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS cloud noise admission: demand pressure, reserve and contiguous limits, retries, supported fallback')
