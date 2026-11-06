#!/usr/bin/env python3
# northlight-test: requires=zig,client,stormlib
"""Native M2 transport is identical for old caches, fresh builders and live lights."""
from pathlib import Path
import collections
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import world_lights_builder as builder
from world_scene_builder import Assets

# Version-12340 CM2Light constructor: the local client's initialized world-
# distance coefficients, independent of authored M2 range/placement scale.
pe=(fp.client_root()/'Wow.exe').read_bytes();nt=struct.unpack_from('<I',pe,60)[0]
sections=struct.unpack_from('<H',pe,nt+6)[0];optional=struct.unpack_from('<H',pe,nt+20)[0]
def read_va(address,size):
    for index in range(sections):
        at=nt+24+optional+index*40;rva,length,offset=struct.unpack_from('<III',pe,at+12)
        relative=address-0x400000-rva
        if 0<=relative and relative+size<=length:return pe[offset+relative:offset+relative+size]
    raise AssertionError(hex(address))
constructor=read_va(0x834a40,0x67)
assert constructor.startswith(bytes.fromhex('d9 ee 8b c1 d9 50 0c'))
assert bytes.fromhex('d9 58 54') in constructor
assert bytes.fromhex('d9 05 c0 2e 9e 00 89 48 04 d9 58 58') in constructor
assert bytes.fromhex('d9 05 c8 23 9f 00 89 48 60 d9 58 5c') in constructor
assert struct.unpack('<f',read_va(0x9e2ec0,4))[0]==struct.unpack('<f',struct.pack('<f',.7))[0]
assert struct.unpack('<f',read_va(0x9f23c8,4))[0]==struct.unpack('<f',struct.pack('<f',.03))[0]
call=read_va(0x8249e6,5);assert call[0]==0xe8 and 0x8249e6+5+struct.unpack_from('<i',call,1)[0]==0x834a40
print('ACTUAL_CM2LIGHT_DENOMINATOR_PASS constructor=0x834a40 modelLightConstructor=0x8249e6 coefficients=0,.7,.03 readOnly=1',flush=True)

with tempfile.TemporaryDirectory(prefix='native-light-transport-') as folder:
    root=Path(folder);assets=Assets()
    try:
        name='world\\kalimdor\\orgrimmar\\passivedoodads\\orgrimmarbonfire\\orgrimmarbonfire01.m2'
        blob=assets.read(name);sources=builder.extract_m2(blob,collections.Counter())
        assert len(sources)==1 and sources[0][1][0]==4 and sources[0][2:4]==(16,40)
        print('ACTUAL_BONFIRE_TRANSPORT',json.dumps(dict(asset=name,archive=assets.origin(name),sha256=hashlib.sha256(blob).hexdigest(),raw=sources)))
    finally:assets.close()
    scale=214.718948/40
    instance=(b'x'*32,71,1,*([0]*6),scale,0,0,0,scale,0,0,0,scale,0,0,0)
    placed=builder.instance_lights(sources,instance,name)[0]
    assert placed[3]==1 and placed[6:8]==(1,12)
    raw=(*placed[:3],*sources[0][1],sources[0][2]*scale,sources[0][3]*scale,1,2,2)
    normalized=(*placed[:8],1,2,2)
    dim=(0,0,0,.35,.14,.07,2,100,2,2,2)
    rgb,start,end=builder.native_transport(dim[3:6]);dim_normalized=(*dim[:3],*rgb,start,end,*dim[8:])
    wmo=(*raw[:8],3,1,3);curated=(*raw[:8],4,3,2)
    for version,records in [(1,[raw,dim,wmo,curated]),(2,[normalized,dim_normalized,wmo,curated])]:
        (root/f'v{version}.fgl').write_bytes(struct.pack('<4sIII',b'FGL1',version,48,len(records))+b''.join(builder.RECORD.pack(*r) for r in records))
    for mode in ('O0','O2'):
        exe=root/('test.exe' if sys.platform=='win32' else 'test')
        target=['-target','x86-windows-gnu'] if sys.platform=='win32' else []
        subprocess.run([str(fp.zig()),'c++',*target,'-std=c++17','-'+mode,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_native_light_transport.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe),str(root/'v1.fgl'),str(root/'v2.fgl')],check=True)
