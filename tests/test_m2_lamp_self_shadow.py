#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-exe
"""Stock, winning mod assets and unpinned variants through the production cache."""
import collections
import json
from pathlib import Path
import struct
import subprocess
import sys

REPO=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(REPO),str(REPO/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets,Builder,InstancedBuilder,unpack,array
from world_lights_builder import extract_m2,m2_sources,instance_lights,RECORD
from world_emitter_index import Index

NAME='world\\generic\\human\\passive doodads\\lamps\\stormwindstreetlamp01.m2'
ALIAS='world\\custom\\unlisted-replacement.m2'


class Variant:
    def __init__(self,assets,blob,skin):
        self.assets,self.blob,self.skin=assets,bytes(blob),bytes(skin)
        self.stats=collections.Counter();self.missing=set();self.unsupported=set();self.clamped=set()
    def read(self,path):
        if path==ALIAS:return self.blob
        if path==ALIAS[:-3]+'00.skin':return self.skin
        return self.assets.read(path)


def tagged(mesh):return [i for i,m in enumerate(mesh[2]) if len(m)==4]


def fixtures(out):
    cases=[]
    for view in ('stock','all'):
        assets=Assets(archives=view)
        try:
            for variant in ('original','renamed','scaled') if view=='all' else ('original',):
                path=NAME;source=assets
                if variant!='original':
                    path=ALIAS;blob=bytearray(assets.read(NAME));skin=assets.read(NAME[:-3]+'00.skin')
                    if variant=='scaled':
                        # Different proportions and bulb height, no filename/hash allowlist.
                        n,o=unpack('<II',blob,60)
                        for i in range(n):
                            p=unpack('<3f',blob,o+i*48);struct.pack_into('<3f',blob,o+i*48,p[0]*1.7,p[1]*.6,p[2]*1.3)
                        n,o=unpack('<II',blob,264)
                        for i in range(n):
                            p=unpack('<3f',blob,o+i*156+4);struct.pack_into('<3f',blob,o+i*156+4,p[0]*1.7,p[1]*.6,p[2]*1.3)
                    source=Variant(assets,blob,skin)
                folder=out/(view+'-'+variant);folder.mkdir(parents=True,exist_ok=True)
                builder=InstancedBuilder(source,folder)
                mesh=builder.m2(path);assert len(tagged(mesh))==1,(view,variant,mesh[2])
                # Exact QA placement, plus a second independent copy for ownership.
                matrix=((.00865055,1.239077568,0),(-1.239077568,.00865055,0),(0,0,1.239107728))
                translation=(-8989.830078125,780.1417236328125,101.792839)
                builder.add_mesh(mesh,matrix,translation,'wmo_doodad',43151536429163)
                tile=folder/'tile.fg3';builder.write_tile(tile,builder.instances)
                lights=m2_sources(path,source.read(path),extract_m2(source.read(path),collections.Counter()),Index(source,lazy=True))
                rows=instance_lights(lights,builder.instances[0],path);assert len(rows)==1
                with (folder/'sources.fgl').open('wb') as f:
                    f.write(struct.pack('<4sIII',b'FGL1',2,RECORD.size,len(rows)))
                    for r in rows:f.write(RECORD.pack(*r))
                cases.append(folder)
                if view=='all' and variant=='original':
                    # A panel without a source or an ambiguous pair of native
                    # sources must remain in the caster set with every face.
                    original=source.read(path);skin=source.read(path[:-3]+'00.skin')
                    changed=bytearray(original);n,o=unpack('<II',changed,112)
                    for i in range(n):
                        flags,blend=unpack('<2H',changed,o+i*4)
                        if blend==0:struct.pack_into('<H',changed,o+i*4,flags&~1)
                    struct.pack_into('<II',changed,264,0,0)
                    plain=Builder(Variant(assets,changed,skin)).m2(ALIAS)
                    assert not tagged(plain) and len(plain[1])==len(mesh[1])
                    changed=bytearray(original);n,o=unpack('<II',changed,264);assert n==1
                    new=len(changed);changed.extend(changed[o:o+156]*2);struct.pack_into('<II',changed,264,2,new)
                    ambiguous=Builder(Variant(assets,changed,skin)).m2(ALIAS)
                    assert not tagged(ambiguous) and len(ambiguous[1])==len(mesh[1])
                print('FIXTURE',view,variant,'triangles',len(mesh[1]),'shell',sum(t[3] in tagged(mesh) for t in mesh[1]),flush=True)
        finally:assets.close()
    return cases+component_fixtures(out)


def component_fixtures(out):
    names={
        'westfall':r'world\azeroth\westfall\passivedoodads\lamppost\westfalllamppost.m2',
        'northrend-torch':r'world\expansion02\doodads\generic\northrend fires\northrendgeneraltorch01.m2',
        'dwarf-lantern':r'world\generic\dwarf\passive doodads\lanterns\dwarvenhanginglanternsmall01.m2',
        'druid-lantern':r'world\expansion06\doodads\nightelf\7ne_druid_hanginglamp01.m2'}
    assets=Assets();cases=[]
    try:
        for label,name in names.items():
            original=assets.read(name);skin=assets.read(name[:-3]+'00.skin')
            for variant in ('original','renamed','scaled'):
                blob=bytearray(original);path=name;source=assets
                if variant!='original':
                    path=ALIAS
                    if variant=='scaled':
                        for header,stride,relative in ((60,48,0),(264,156,4)):
                            n,o=unpack('<II',blob,header)
                            for i in range(n):
                                point=unpack('<3f',blob,o+i*stride+relative)
                                struct.pack_into('<3f',blob,o+i*stride+relative,*(x*1.7 for x in point))
                    source=Variant(assets,blob,skin)
                folder=out/(label+'-'+variant);folder.mkdir(parents=True,exist_ok=True)
                builder=InstancedBuilder(source,folder);mesh=builder.m2(path)
                own=tagged(mesh);assert own,(label,variant)
                shell=sum(t[3] in own for t in mesh[1]);assert 0<shell<len(mesh[1])
                # There is one native light, and no asset/profile name is needed.
                lights=m2_sources(path,blob,extract_m2(blob,collections.Counter()),Index(source,lazy=True));assert len(lights)==1
                builder.add_mesh(mesh,((1,0,0),(0,1,0),(0,0,1)),(-8989.83,780.14,101.79),'m2',1234567)
                builder.write_tile(folder/'tile.fg3',builder.instances)
                rows=instance_lights(lights,builder.instances[0],path)
                (folder/'sources.fgl').write_bytes(struct.pack('<4sIII',b'FGL1',2,RECORD.size,len(rows))+b''.join(RECORD.pack(*r) for r in rows))
                print('COMPONENT_FIXTURE',label,variant,'triangles',len(mesh[1]),'emitter',shell,'parts',len(own),flush=True)
                cases.append(folder)
    finally:assets.close()
    return cases


def main():
    out=fp.output_dir();cases=fixtures(out)
    for opt in ('-O0','-O2'):
        exe=out/('m2-lamp'+opt+'.exe')
        cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),
             fp.TESTS/'m2_lamp_self_shadow.cpp',fp.src('world_gi.cpp'),fp.src('world_mesh_plan.cpp'),fp.src('static_shadow_scene.cpp'),'-o',exe]
        subprocess.run(list(map(str,cmd)),check=True,env=fp.zig_env())
        for folder in cases:subprocess.run([str(exe),str(folder)],check=True)
    (out/'validation.json').write_text(json.dumps({'pass':True,'cases':[p.name for p in cases],'optimizations':['O0','O2'],'game_launched':False},indent=2)+'\n')


if __name__=='__main__':main()
