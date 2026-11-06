#!/usr/bin/env python3
# northlight-test: requires=zig
"""Read-only scene inventory and animation tests; --client adds actual MPQ lamps."""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client',action='store_true')
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='native-scene-lights-') as folder:
    tmp=Path(folder);fixtures=[]
    if args.client:
        fp.use_source_modules()
        from world_scene_builder import Assets
        assets=Assets()
        try:
            for name in ('dalaran_lampwall01','dalaran_lamppost01'):
                path=tmp/(name+'.m2');path.write_bytes(assets.read('world\\expansion02\\doodads\\dalaran\\'+name+'.m2'));fixtures.append(str(path))
            for name in ('spells\\enchantments\\shaman_fire.m2','spells\\enchantments\\skullballs.m2','spells\\torch_missile.m2'):
                path=tmp/Path(name.replace('\\','/')).name;path.write_bytes(assets.read(name));fixtures.append(str(path))
            for name in ('Creature\\HumanMaleWarriorLight\\HumanMaleWarriorLight.m2','item\\objectcomponents\\weapon\\club_1h_torch_a_01.m2'):
                path=tmp/Path(name.replace('\\','/')).name;path.write_bytes(assets.read(name));fixtures += ['--parent',str(path)] if 'Creature' in name else [str(path)]
        finally:
            assets.close()
        fixtures+=['--exe',str(fp.client_root()/'Wow.exe')]
    for mode in ('O0','O2'):
        exe=tmp/('test.exe' if sys.platform=='win32' else 'test')
        target=['-target','x86-windows-gnu'] if sys.platform=='win32' else []
        subprocess.run([str(fp.zig()),'c++',*target,'-std=c++17','-'+mode,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_native_scene_lights.cpp'),'-o',str(exe)],check=True,env=fp.zig_env())
        subprocess.run([str(exe),*fixtures],check=True)
