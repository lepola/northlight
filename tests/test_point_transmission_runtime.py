#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Actual FGS readers and upload plans, independent binary fixture, sanitizer gate."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='point-transmission-runtime-') as folder:
    root=Path(folder);models=root/'models';models.mkdir();key=hashlib.sha256(b'FGS3-point-transmission-v1:fixture-table.m2').digest()
    vertices=[(-2,-2,1,0,0,-1,0,0),(2,-2,1,0,0,-1,1,0),(2,2,1,0,0,-1,1,1),(-2,2,1,0,0,-1,0,1)]
    rgba=b''.join(bytes((255,255,255,255 if i<20 else 0)) for i in range(64))
    mesh=struct.pack('<4s4I',b'FGS2',2,4,2,1)+b''.join(struct.pack('<8f',*v) for v in vertices)
    mesh+=struct.pack('<8I',0,1,2,0,0,2,3,0)+struct.pack('<3fIIfI',1,1,1,8,8,.5,len(rgba))+rgba
    (models/(key.hex()+'.fgs')).write_bytes(mesh)
    record=struct.pack('<32sQI18f',key,123,3,-2,-2,1,2,2,1,1,0,0,0,1,0,0,0,1,0,0,0)
    tile=root/'32_32.fg3';tile.write_bytes(struct.pack('<4sII',b'FGS3',3,1)+record)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=root/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                        str(fp.TESTS/'test_point_transmission_runtime.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(tile),str(models)],check=True)
        filter_exe=root/('filter-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                        str(fp.TESTS/'test_point_transmission_filter.cpp'),'-o',str(filter_exe)],check=True)
        subprocess.run([str(filter_exe)],check=True)
