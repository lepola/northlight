#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe,client
"""Actual client lamp skin/bone anchors plus executable live-pose regression."""
import collections
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import light_attachments as attachments
from world_scene_builder import Assets
from world_lights_builder import extract_m2
from outdoor_light_profiles import fallback, load

out=fp.output_dir()/'model-light-attachments';out.mkdir(parents=True,exist_ok=True)
witness=(0.,0.,0.,1.,0.,0.,0.,1.,0.)
records=[]
for source,x in ((42,10.),(43,100.)):
    records.append((source,1,0,*witness,1.,2.,3.,0.,0.,0.,1.,0.,0.,0.,1.,0.,0.,0.,1.,x,20.,30.))
path=out/'fixture.fga';attachments.write(path,records)
assert len(path.read_bytes())==16+2*128
exe=out/'attachments.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-std=c++17',*fp.test_include_flags(),
         fp.TESTS/'test_model_light_attachments.cpp','-o',exe]
subprocess.run(list(map(str,command)),env=fp.zig_env(),check=True,timeout=120)
subprocess.run([str(exe),str(path)],check=True,timeout=30)
catalog=load()
for excluded in ('','zy'):
    assets=Assets(without=excluded)
    try:
        for name,expected_bone,expected_reference in (('zangar',1,0),('darnassus',0,1)):
            model=('world\\expansion01\\doodads\\zangar\\lamp\\zangarlamppost01.m2' if name=='zangar' else
                   'world\\generic\\nightelf\\passive doodads\\lamps\\darnassusstreetlamp01.m2')
            blob=assets.read(model)
            lights=fallback(catalog,model,blob,extract_m2(blob,collections.Counter()))
            templates=attachments.templates(assets,model,blob,lights)
            assert templates and all(t[1:3]==(expected_bone,expected_reference) for t in templates),(model,templates)
            placement=(b'x'*32,17,1,*([0.]*6),1.,0.,0.,0.,1.,0.,0.,0.,1.,10.,20.,30.)
            placed=attachments.placed(templates,placement,[(lights[0][4],(0,)*8+(42,))])
            assert placed and placed[0][0]==42 and placed[0][-12:]==placement[9:]
            print('PASS actual model attachment',excluded or 'active',model,'sections',len(templates))
            if excluded=='zy' and name=='zangar':
                from m2_emitter_patch import append_lights
                patched=bytearray(append_lights(blob,[dict(position=lights[0][0],rgb=(0,.8,1),intensity=1,start=1,end=10)]))
                offset=struct.unpack_from('<I',patched,268)[0];struct.pack_into('<h',patched,offset+2,1)
                dynamic=attachments.native_sources(patched)
                assert len(dynamic)==1 and dynamic[0][4:]==(0,2,2)
                assert all(t[1:3]==(1,0) for t in attachments.templates(assets,model,patched,dynamic))
                struct.pack_into('<h',patched,offset+56,2) # Unsupported changing colour must not freeze into a static source.
                assert attachments.native_sources(patched)==[]
        model='world\\generic\\nightelf\\passive doodads\\lamps\\kalidarstreetlamp02.m2'
        blob=assets.read(model);lights=fallback(catalog,model,blob,extract_m2(blob,collections.Counter()))
        assert not attachments.templates(assets,model,blob,lights) # Camera-facing glow on a stationary lamp.
    finally:
        assets.close()
# Reject malformed files through the production reader, without accepting a prefix.
for label,data in [('truncated',path.read_bytes()[:-1]),('magic',b'BAD!'+path.read_bytes()[4:]),
                   ('version',path.read_bytes()[:4]+struct.pack('<I',2)+path.read_bytes()[8:])]:
    bad=out/(label+'.fga');bad.write_bytes(data)
    result=subprocess.run([str(exe),str(bad),'invalid'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                          env={**os.environ,'LLVM_DISABLE_CRASH_REPORT':'1'},timeout=30)
    assert result.returncode==0,label
