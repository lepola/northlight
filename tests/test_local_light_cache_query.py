#!/usr/bin/env python3
# northlight-test: requires=cxx
"""FGL query geometry and corruption tests independent of StormLib or game assets."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

record = struct.Struct('<8fQII')
with tempfile.TemporaryDirectory(prefix='northlight-light-query-') as tmp:
    root = Path(tmp)
    values = [(0,0,0,1,.5,.2,2,10,1,1,1), (30,0,0,1,1,1,0,10,2,2,0), (1000,0,0,1,1,1,0,10,3,2,0)]
    good = struct.pack('<4sIII', b'FGL1', 1, 48, 3) + b''.join(record.pack(*v) for v in values)
    (root/'Alpha.fgl').write_bytes(good)
    (root/'Beta.fgl').write_bytes(struct.pack('<4sIII', b'FGL1', 1, 48, 1) + record.pack(25,0,0,1,1,1,0,5,4,2,0))
    bad = [good[:15], good[:-1], good+b'\0', b'BAD!'+good[4:]]
    for offset, fmt, value in [(4,'<I',2), (8,'<I',52), (12,'<I',1000001), (16,'<f',float('nan')), (44,'<f',1), (96,'<Q',1)]:
        blob = bytearray(good)
        struct.pack_into(fmt, blob, offset, value)
        bad.append(bytes(blob))
    for i, blob in enumerate(bad):
        (root/f'bad{i}.fgl').write_bytes(blob)
    for name, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        exe = root/name
        subprocess.run(['clang++', '-std=c++17', *flags, *fp.test_include_flags(),
                        str(fp.TESTS/'test_world_local_lights.cpp'), '-o', str(exe)], check=True)
        print(name, subprocess.check_output([str(exe), str(root), ''], text=True), end='', flush=True)
