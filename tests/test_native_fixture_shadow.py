#!/usr/bin/env python3
# northlight-test: requires=zig,stormlib,client
"""Exact installed Dalaran shells through the production rigid replay proof."""
from pathlib import Path
import struct
import subprocess
import sys
import hashlib
from unittest.mock import patch
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets,Builder
from light_attachments import native_sources
import m2_light_geometry

out=fp.output_dir()/'native-fixture-shadow';out.mkdir(parents=True,exist_ok=True)
assets=Assets();fixtures=[]
try:
    for name in ('dalaran_lamppost01','dalaran_lampwall01'):
        path='world\\expansion02\\doodads\\dalaran\\'+name+'.m2';blob=assets.read(path);mesh=Builder(assets).m2(path)
        with patch.object(m2_light_geometry,'paired_source_layers',return_value={}):
            original=Builder(assets).m2(path)
        def owned(value):
            return {i for i,t in enumerate(value[1]) if len(value[2][t[3]])>3 and value[2][t[3]][3]==0x800000}
        before,after=owned(original),owned(mesh)
        assert before<=after and mesh[0]==original[0] and [t[:3] for t in mesh[1]]==[t[:3] for t in original[1]]
        known={'dalaran_lamppost01':('0c56d35185061616c730fe58f53c033da8b3562ff53872310271c21639773c04',136,0,638),
               'dalaran_lampwall01':('6e7556ff3097f0c6305f611d91d1c0bba8e8f3ddc01bb356e8b47ec7ced82d08',56,56,266)}
        digest,source_count,paired_count,metal=known[name]
        if hashlib.sha256(blob).hexdigest()==digest:
            assert len(before)==source_count and len(after-before)==paired_count and len(mesh[1])-len(after)==metal,(name,len(before),len(after))
        print('PASS source layers preserved',name,'original',len(before),'paired',len(after-before),'retained',len(mesh[1])-len(after),flush=True)
        sources={s[4]:s[0] for s in native_sources(blob)};shell=[]
        for triangle in mesh[1]:
            material=mesh[2][triangle[3]]
            if len(material)>3 and 0x800000<=material[3]<0x800040:
                source=material[3]-0x800000
                shell.append(struct.pack('<I12f',source,*sources[source],*(value for vertex in triangle[:3] for value in mesh[0][vertex][:3])))
        assert len(shell)>4,name
        fixture=out/(name+'.bin');fixture.write_bytes(struct.pack('<I',len(shell))+b''.join(shell));fixtures.append(fixture)
finally:assets.close()
exe=out/'native-fixture-shadow.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_native_fixture_shadow.cpp'),'-o',str(exe)],check=True,env=fp.zig_env(),timeout=180)
subprocess.run([str(exe),*map(str,fixtures)],check=True,timeout=30)
