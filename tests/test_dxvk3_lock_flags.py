#!/usr/bin/env python3
# northlight-test: requires=cxx
"""DXVK 3.1.1 lock rules our uploads rely on (0.3.192), native host C++ with a fake d3d9.h: a fake buffer modelling LockBuffer
(READONLY stripped for non-MANAGED, NOOVERWRITE/DISCARD stripped for non-DEFAULT, skipWait, the discard counter), fake event queries
with controllable latency, NorthlightUpload::readBackLock() with the gate on and off, NorthlightBackend::dxvkMajor, and 2000 frames of
random sizes through the fence-checked rings of dynamic_ring.h against a byte-level buffer model (renaming DISCARD, race check,
byte-exact read-back of every in-flight draw). No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
STUB='''#pragma once
// Minimal fake d3d9.h for the host test: just what upload_lock.h and dynamic_ring.h use.
#include <cstdint>
using UINT=unsigned;using DWORD=unsigned;using HRESULT=std::int32_t;
constexpr HRESULT S_OK=0,S_FALSE=1;
inline bool FAILED(HRESULT h){return h<0;}
constexpr DWORD D3DLOCK_READONLY=0x10,D3DLOCK_NOOVERWRITE=0x1000,D3DLOCK_DISCARD=0x2000,D3DISSUE_END=1;
enum D3DQUERYTYPE{D3DQUERYTYPE_EVENT=8};
struct IDirect3DQuery9{virtual HRESULT Issue(DWORD)=0;virtual HRESULT GetData(void*,DWORD,DWORD)=0;virtual unsigned long AddRef()=0;virtual unsigned long Release()=0;};
struct IDirect3DDevice9{virtual HRESULT CreateQuery(D3DQUERYTYPE,IDirect3DQuery9**)=0;};
'''
# The sources under test are the production headers, not copies.
for name,needle in [('upload_lock.h','inline DWORD readBackLock()'),('dynamic_ring.h','inline UINT ringCapacity('),('dynamic_ring.h','struct FrameFence'),('backend_policy.h','inline int dxvkMajor(')]:
    assert needle in fp.src(name).read_text(),f'{name}: {needle} moved'
with tempfile.TemporaryDirectory(prefix='northlight-dxvk3-lock-') as tmp:
    tmp=Path(tmp);(tmp/'d3d9.h').write_text(STUB)
    for label,flags,frames in [('O2',['-O2'],['2000','500']),('asan+ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],['1200','100'])]:
        exe=tmp/('test-'+label.replace('+','_'))
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,'-I'+str(tmp),*fp.test_include_flags(),str(HERE/'test_dxvk3_lock_flags.cpp'),'-o',str(exe)],check=True)
        print(label,flush=True);subprocess.run([str(exe),*frames],check=True)
print('PASS DXVK 3 lock flags and fence-checked rings')
