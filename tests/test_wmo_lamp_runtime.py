#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-exe
"""CPU-only x86 lamp ownership and occlusion checks using winning MPQ assets."""
from pathlib import Path
import struct
import subprocess
import sys

REPO=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(REPO),str(REPO/'renderer')]
import northlight_paths as fp
from test_wmo_lamp_emission import Assets,ROOT,WmoLamps
from world_scene_builder import Builder,InstancedBuilder
from world_lights_builder import RECORD,instance_lights
import wmo_lamp_emission


def run(command):
    subprocess.run(command,check=True,env=fp.zig_env(),creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)


def main():
    WmoLamps.setUpClass()
    assets=Assets(WmoLamps.files)
    matrix=((-0.7826081514,0.6225146651,0.),(-0.6225146651,-0.7826081514,0.),(0.,0.,1.))
    translation=(-8931.633789,539.293640,101.957024)
    out=fp.output_dir()
    builder=Builder(assets);mesh=builder.wmo(ROOT)
    builder.add_mesh(mesh[:3],matrix,translation,'wmo');flat=out/'flat.fgs';builder.write_scene(flat)
    inst=InstancedBuilder(assets,out/'instanced');mesh=inst.wmo(ROOT)
    inst.add_mesh(mesh[:3],matrix,translation,'wmo',10047)
    tile=out/'instanced'/'tile.fg3';inst.write_tile(tile,inst.instances)
    inst.add_mesh(mesh[:3],matrix,translation,'wmo',10048)
    overlap=tile.with_name('overlap.fg3');inst.write_tile(overlap,inst.instances)
    sources,_=wmo_lamp_emission.analyze(WmoLamps.root,WmoLamps.group,assets.read,94)
    placed=sorted(instance_lights(sources,inst.instances[0],ROOT),key=lambda row:row[8])
    fgl=out/'sources.fgl'
    with fgl.open('wb') as stream:
        stream.write(struct.pack('<4sIII',b'FGL1',1,RECORD.size,len(placed)))
        for row in placed:stream.write(RECORD.pack(*row))
    for opt in ('-O0','-O2'):
        for name in ('shadow','rays','real'):
            exe=out/f'{name}{opt}.exe'
            command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',
                     *fp.test_include_flags(),str(fp.TESTS/f'test_wmo_lamp_{name}.cpp')]
            if name!='shadow':command += [str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp'))]
            run([*command,'-o',str(exe)])
            args=[str(flat),str(fgl),str(tile),str(out/'instanced'/'models'),str(overlap)] if name=='real' else []
            run([str(exe),*args])
    print('PASS x86 O0/O2: exact fixture ownership, native dedup, foreign occluders and real MPQ instances; no game/GPU')


if __name__=='__main__':main()
