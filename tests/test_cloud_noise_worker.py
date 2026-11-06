#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Retry cloud-noise generation after a worker allocation failure."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('world_renderer.h').read_text()
worker = source[source.index('    struct FogCloudNoise{'):source.index('    static FogCloudNoise& fogCloudNoise()')]
test = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <new>
#include <thread>
#include <vector>
using DWORD=uint32_t;
std::atomic<DWORD> tick{5000};DWORD GetTickCount(){return tick.load();}
template<class... T> void logf(const char*,T...){}
namespace NorthlightFogClouds {
using Quantiles=unsigned;
std::atomic<unsigned> calls{0};
std::vector<uint8_t> generate(){if(++calls==1)throw std::bad_alloc();return {1,2,3};}
unsigned quantiles(const uint8_t* data){return data[0]+data[1]+data[2];}
}
''' + worker + r'''
int main(){
 FogCloudNoise noise;noise.request();
 auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while((!noise.retryPending.load()||noise.started.load())&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
 assert(noise.retryPending&&!noise.started&&!noise.ready&&NorthlightFogClouds::calls==1);
 tick=5999;for(int i=0;i<100;++i)noise.request();assert(NorthlightFogClouds::calls==1&&!noise.started);
 tick=6000;std::vector<std::thread> callers;for(int i=0;i<8;++i)callers.emplace_back([&]{noise.request();});for(auto& t:callers)t.join();
 until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(!noise.ready.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
 assert(noise.ready&&noise.started&&NorthlightFogClouds::calls==2&&noise.data.size()==3&&noise.quantiles==6);
 for(int i=0;i<100;++i)noise.request();assert(NorthlightFogClouds::calls==2);
}
'''
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out / 'cloud-noise-worker.cpp', out / 'cloud-noise-worker.exe'
cpp.write_text(test, encoding='utf-8')
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True, timeout=30)
print('PASS cloud noise worker: allocation failure, throttled retry, concurrent requests and ready data')
