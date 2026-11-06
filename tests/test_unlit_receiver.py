#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Unlit M2 RGB proof ignores alpha/fog normals; stencil marks share no name bit."""
import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

tree = ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
stub = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'stub' for t in n.targets))
source = r'''
#include "unlit_receiver.h"
#include <cassert>
#include <cstdio>
using Word=uint32_t;
static std::vector<Word> code(bool lit,bool alphaNormal){
 std::vector<Word> w={0xfffe0300};
 auto op=[&](unsigned n,std::initializer_list<Word> v){w.push_back(n|(unsigned(v.size())<<24));w.insert(w.end(),v);};
 op(31,{0x80000003,0x900f0001}); // NORMAL v1
 op(31,{0x8000000a,0xe00f0001}); // COLOR0 o1
 op(1,{0xe0070001,lit?0x90e40001u:0xa0e4001cu});
 op(1,{0xe0080001,alphaNormal?0x90000001u:0xa0ff001cu});
 w.push_back(0xffff);return w;
}
int main(){
#ifdef REAL_CLIENT_PROGRAMS
 assert(NorthlightUnlitReceiver::shader(ClientShaders::NoBoneVs3,sizeof ClientShaders::NoBoneVs3/4,true));
 assert(!NorthlightUnlitReceiver::shader(ClientShaders::OneBoneVs3,sizeof ClientShaders::OneBoneVs3/4,true));
#endif
 for(bool alpha:{false,true})for(bool lit:{false,true}){
  auto w=code(lit,alpha);
  assert(NorthlightUnlitReceiver::shader(w.data(),w.size(),true)==!lit);
  assert(!NorthlightUnlitReceiver::shader(w.data(),w.size(),false));
  w.back()=0;assert(!NorthlightUnlitReceiver::shader(w.data(),w.size(),true));
 }
 assert(!NorthlightUnlitReceiver::shader(nullptr,0,true));
 using namespace NorthlightUnlitReceiver;
 assert(stencilFormat(75,123)&&stencilFormat(81,123)&&stencilFormat(123,123));
 assert(!stencilFormat(79,123)&&!stencilFormat(73,123)&&!stencilFormat(77,123));
 assert((Bit&NorthlightNameStencil::kBit)==0&&(PrelitBit&(Bit|NorthlightNameStencil::kBit))==0);
 unsigned long marks=0,names=NorthlightNameStencil::kBit;
 for(const auto& s:Mark)if(s.state==StencilWriteMask)marks=s.value;
 assert(marks==(Bit|PrelitBit|names)&&((names&~marks)|(Bit&marks))==Bit);
 for(const auto& s:Unmark)if(s.state==StencilWriteMask)assert(s.value==(Bit|PrelitBit|names));
 for(const auto& s:MarkPrelit){if(s.state==StencilRef)assert(s.value==PrelitBit);if(s.state==StencilWriteMask)assert(s.value==(Bit|PrelitBit|names));}
 for(bool normal:{false,true})for(bool global:{false,true}){
  std::vector<Word> w={0xfffe0300,0x0200001f,0x8000000a,0x900f0002,0x0200001f,0x80000003,0x900f0001,
   0x0200001f,0x8000000a,0xe00f0001,0x03000005,0xe0070001,0x90e40002,global?0xa0e4000au:normal?0x90e40001u:0x90e40002u,
   0x02000001,0xe0080001,0x90000001,0x0000ffff};
  assert(prelitShader(w.data(),w.size(),true)==(!normal&&!global));
  assert(!prelitShader(w.data(),w.size(),false)); // Identical M2 RGB does not qualify as a prelit WMO.
 }
 assert(!prelitShader(nullptr,0,true));
 std::puts("PASS unlit RGB proof: normal-dependent lighting rejected, alpha/fog independent, unknown/malformed rejected, stencil bits isolated");
}
'''
with tempfile.TemporaryDirectory(prefix='unlit-receiver-') as tmp:
    folder=Path(tmp)
    if os.name!='nt':(folder/'d3d9.h').write_text(stub)
    if client_fixtures.shaders_available():
        client_fixtures.actor_client_programs(folder)
        source='#define REAL_CLIENT_PROGRAMS\n#include "actor_client_programs.h"\n'+source
    (folder/'test.cpp').write_text(source)
    compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
    for flags in ([['-O2']] if os.name=='nt' else [['-O2'], ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']]):
        exe=folder/('test.exe' if os.name=='nt' else 'test')
        subprocess.run([*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(folder),*fp.test_include_flags(),str(folder/'test.cpp'),'-o',str(exe)],env=fp.zig_env() if os.name=='nt' else None,check=True)
        subprocess.run([str(exe)],check=True)
