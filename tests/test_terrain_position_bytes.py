#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe,client
"""Exact strided XYZ validation: production helper, x86 checks and MPQ-cache timing."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

header = fp.src('terrain_capture_bounds.h').read_text()
helper = header[header.index('inline bool samePositionBytes('):header.index('enum class RejectReason')]
out = fp.output_dir()
source = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
struct Position {float x,y,z;};
'''+helper+r'''
template<bool Legacy> __attribute__((noinline)) bool scan(const unsigned char* source,const Position* expected,size_t count,size_t stride){
 for(size_t i=0;i<count;++i){
  if constexpr(Legacy){if(std::memcmp(source+i*stride,expected+i,sizeof(Position)))return false;}
  else if(!samePositionBytes(source+i*stride,expected[i]))return false;
 }
 return true;
}
template<bool Legacy> double timeScan(const std::vector<unsigned char>& bytes,const std::vector<Position>& expected){
 volatile unsigned matches=0;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<100;++i){
  asm volatile("" : : "r"(bytes.data()) : "memory");
  matches+=scan<Legacy>(bytes.data(),expected.data(),expected.size(),32);
 }
 assert(matches==100);
 return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(100*expected.size());
}
int main(int argc,char**argv){
 uint32_t seed=20261026;unsigned checks=0;
 for(unsigned offset=0;offset<16;++offset)for(unsigned trial=0;trial<100;++trial){
  // The final XYZ ends at the allocation boundary, including unaligned cases.
  auto bytes=std::make_unique<unsigned char[]>(offset+sizeof(Position));Position p;
  for(unsigned i=0;i<sizeof(Position);++i){seed=seed*1664525u+1013904223u;reinterpret_cast<unsigned char*>(&p)[i]=static_cast<unsigned char>(seed>>24);}
  std::memcpy(bytes.get()+offset,&p,sizeof p);
  assert(samePositionBytes(bytes.get()+offset,p));++checks;
  for(unsigned bit=0;bit<96;++bit){
   bytes[offset+bit/8]^=static_cast<unsigned char>(1u<<(bit%8));
   assert(!samePositionBytes(bytes.get()+offset,p));++checks;
   bytes[offset+bit/8]^=static_cast<unsigned char>(1u<<(bit%8));
  }
 }
 // Exact equality distinguishes signed zeros and identical/different NaN payloads.
 for(uint32_t value:{0u,0x80000000u,0x7fc01234u,0x7f801234u,0xffffffffu}){
  Position p;uint32_t bits[]={value,value,value};std::memcpy(&p,bits,sizeof p);
  assert(samePositionBytes(bits,p));++checks;
  for(unsigned word=0;word<3;++word){bits[word]^=1;assert(!samePositionBytes(bits,p));++checks;bits[word]^=1;}
 }
 Position zero{0,0,0};uint32_t signedZero[]={0x80000000u,0,0};
 assert(!samePositionBytes(signedZero,zero));++checks;
 assert(argc==2);FILE* file=std::fopen(argv[1],"rb");assert(file);
 unsigned info[5];assert(std::fread(info,4,5,file)==5&&info[0]==0x32534746&&info[1]==2&&info[2]>0);
 std::vector<unsigned char> bytes(size_t(info[2])*32);assert(std::fread(bytes.data(),1,bytes.size(),file)==bytes.size());std::fclose(file);
 std::vector<Position> expected(info[2]);for(size_t i=0;i<expected.size();++i)std::memcpy(&expected[i],bytes.data()+i*32,12);
 assert(scan<false>(bytes.data(),expected.data(),expected.size(),32));
 for(size_t i=0;i<expected.size();i+=std::max(size_t(1),expected.size()/257)){
  bytes[i*32+11]^=1;assert(!scan<false>(bytes.data(),expected.data(),expected.size(),32));bytes[i*32+11]^=1;
 }
 std::vector<double> oldTimes,newTimes;
 for(unsigned round=0;round<9;++round){
  if(round&1){newTimes.push_back(timeScan<false>(bytes,expected));oldTimes.push_back(timeScan<true>(bytes,expected));}
  else{oldTimes.push_back(timeScan<true>(bytes,expected));newTimes.push_back(timeScan<false>(bytes,expected));}
 }
 std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
 std::printf("{\"pass\":true,\"bit_checks\":%u,\"vertices\":%u,\"legacy_ns_per_vertex\":%.6f,\"current_ns_per_vertex\":%.6f}\n",checks,info[2],oldTimes[4],newTimes[4]);
}
'''
cpp = out/'position-bytes.cpp'
cpp.write_text(source)
key = hashlib.sha256(b'FGS3-v1:terrain:Azeroth:32:50').hexdigest()
model = fp.client_root()/'world-cache/models'/(key+'.fgs')
results = []
if '--native' in sys.argv:
    modes = [('native', ['-O2']), ('sanitizers', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]
else:
    modes = [('x86', ['-O2']), ('debug', ['-O0', '-g'])]
for label, flags in modes:
    binary = out/(label+('.exe' if '--native' not in sys.argv else ''))
    command = ['clang++'] if '--native' in sys.argv else [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu']
    subprocess.run([*command, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags, str(cpp), '-o', str(binary)], env=fp.zig_env(), check=True)
    data = json.loads(subprocess.check_output([str(binary), str(model)], text=True))
    results.append(dict(mode=label, **data))
print(json.dumps(results, indent=2))
(out/'validation.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(header.encode()).hexdigest(), tests=results, game_launched=False), indent=2)+'\n')
