#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact bounds eviction against frozen production under rotating visibility; no GPU/game."""
from pathlib import Path
import hashlib
import json
import statistics
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.out()/'bounds-rotation'
out.mkdir(parents=True,exist_ok=True)
baseline=(fp.FIXTURES/'displacement-bounds-0be2a10.h').read_bytes()
(out/'bounds_rotation_baseline.h').write_bytes(baseline)
exe=out/'bounds-rotation.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'displacement_bounds_rotation.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
result=subprocess.check_output([str(exe)],text=True)
print(result,end='')
rows=[json.loads(line) for line in result.splitlines() if line.startswith('{')]
report={'baseline_ref':'0be2a10','baseline_sha256':hashlib.sha256(baseline).hexdigest(),'game_launched':False,'GPU_used':False,'rounds':rows,'medians':{}}
for workload in ('warm','rotating'):
    rows_here=[row for row in rows if row['workload']==workload]
    old=statistics.median(row['old_ms'] for row in rows_here)
    new=statistics.median(row['new_ms'] for row in rows_here)
    report['medians'][workload]={'old_ms':old,'new_ms':new,'ratio':new/old}
(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report['medians'],indent=2))
