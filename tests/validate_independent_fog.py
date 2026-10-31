#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache manual
"""MPQ-derived fog field equivalence and real x86 VA-pressure checks; no game/GPU."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
paths=[fp.TESTS/'validate_independent_fog.cpp',fp.src('regional_fog_cache.h'),fp.src('regional_fog.h'),fp.src('world_gi.cpp'),fp.src('world_gi.h'),fp.src('geometry_memory.h')]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in paths}
exe=a.output/'fog.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(paths[0]),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
image=bytearray(exe.read_bytes());nt=struct.unpack_from('<I',image,0x3c)[0];assert image[:2]==b'MZ' and image[nt:nt+4]==b'PE\0\0'
flags=nt+22;struct.pack_into('<H',image,flags,struct.unpack_from('<H',image,flags)[0]|0x20);exe.write_bytes(image)
raw=subprocess.check_output([str(exe),str(fp.world_cache())],text=True);(a.output/'validation.log').write_text(raw);print(raw,end='')
assert len([line for line in raw.splitlines() if line.startswith('INDEPENDENT_FOG ')])==5
assert all(sha(Path(path))==value for path,value in inputs.items())
(a.output/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'gpu_used':False,'actual_x86_va_pressure':True,'exact_terrain_and_city_fields':True,'inputs':inputs,'output':raw},indent=2)+'\n')
