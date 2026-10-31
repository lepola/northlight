#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe,client
"""Exact strided bounds against the prior loop, allocation/page guards and MPQ timing."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

native='--native' in sys.argv
out=fp.output_dir()
model=fp.client_root()/'world-cache/models'/(hashlib.sha256(b'FGS3-v1:terrain:Azeroth:32:50').hexdigest()+'.fgs')
modes=[('native',['-O2']),('sanitizers',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])] if native else [
    ('x86',['-O2']),('debug',['-O0','-g']),('scalar',['-O2','-U__SSE2__'])]
results=[]
for name,flags in modes:
    binary=out/(name+('' if native else '.exe'))
    compiler=['clang++'] if native else [str(fp.zig()),'c++','-target','x86-windows-gnu']
    subprocess.run([*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),
                    str(Path(__file__).with_name('position_bounds_scan.cpp')),'-o',str(binary)],env=fp.zig_env(),check=True)
    data=json.loads(subprocess.check_output([str(binary),str(model)],text=True));results.append(dict(mode=name,**data))
print(json.dumps(results,indent=2))
(out/'validation.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(fp.src('position_bounds_scan.h').read_bytes()).hexdigest(),tests=results,game_launched=False),indent=2)+'\n')
