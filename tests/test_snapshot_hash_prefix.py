#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact snapshot lookup hashes with cached immutable metadata prefixes."""
from pathlib import Path
import ast,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
here=Path(__file__).resolve().parent

def literal(name,key):
    return next(ast.literal_eval(n.value) for n in ast.parse((here/name).read_text(encoding='utf-8')).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==key for t in n.targets))

with tempfile.TemporaryDirectory(prefix='snapshot-hash-prefix-') as tmp:
    out=Path(tmp)
    (out/'d3d9.h').write_text(literal('test_terrain_snapshot.py','stub'),encoding='utf-8')
    (out/'snapshot_harness.h').write_text('#pragma once\n'+literal('test_draw_snapshot.py','harness').split('int main()')[0],encoding='utf-8')
    for name,flags in [('O2',['-O2']),('ASan-UBSan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        exe=out/name
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(out),*fp.test_include_flags(),str(here/'test_snapshot_hash_prefix.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('test_snapshot_hash_prefix: PASS')
