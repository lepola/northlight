#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Deterministic CPU state/geometry oracle for the 40 CPU + 40 GPU follow-up."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--baseline',type=Path,help='saved repository tree before this optimization wave')
parser.add_argument('--record-reference',action='store_true',help='record baseline digests after comparison')
args=parser.parse_args()
fixture=fp.TESTS/'fixtures/cpu-gpu-20261103-cpu.json'
expected=json.loads(fixture.read_text()) if fixture.exists() else {}
out=fp.output_dir();runs=[]
for mode in ['-O0','-O2']:
    hashes={}
    variants=[('baseline',args.baseline)] if args.baseline else []
    variants.append(('current',fp.REPO))
    for label,root in variants:
        flags=[value for directory in fp.SRC_DIRS for value in ['-I',str(root/directory.relative_to(fp.REPO))]]
        exe=out/('cpu-gpu-'+label+mode+'.exe');snapshot=out/('cpu-gpu-'+label+mode+'.bin')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',mode,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        *flags,str(fp.TESTS/'cpu_gpu_optimizations.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe),str(snapshot)],check=True,timeout=60)
        hashes[label]=hashlib.sha256(snapshot.read_bytes()).hexdigest()
    oracle=hashes.get('baseline',expected.get(mode))
    assert oracle and hashes['current']==oracle,(mode,hashes,oracle)
    runs.append({'mode':mode,'sha256':oracle,'status':'PASS'})
    if args.record_reference:
        assert args.baseline
        expected[mode]=oracle
checks=[]
for name in ['test_probe_activation.cpp','test_probe_blend.cpp','test_probe_pool_dispatch.cpp','test_streaming_retirement.cpp',
             'cpu_retirement_wake.cpp','test_static_shadow_dedup.cpp','test_material_byte_ops.cpp','test_replay_bounds_schedule.cpp',
             'position_bounds.cpp','test_light_scissor_pairs.cpp','test_local_light_scissor.cpp']:
    exe=out/('cpu-check-'+str(len(checks))+'.exe')
    extra=[str(fp.src('static_shadow_scene.cpp')),str(fp.src('world_gi.cpp'))] if name=='test_static_shadow_dedup.cpp' else []
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra',
                    '-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/name),*extra,'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
    (out/(Path(name).stem+'.log')).write_text(result.stdout+result.stderr)
    print(result.stdout.strip(),flush=True);result.check_returncode();checks.append(name)
if args.record_reference:
    fixture.parent.mkdir(exist_ok=True)
    fixture.write_text(json.dumps(expected,indent=2)+'\n',newline='\n')
(out/'validation.json').write_text(json.dumps({'exact':runs,'checks':checks},indent=2)+'\n')
print('PASS exact baseline/current CPU state, geometry, and dither; '+str(out/'validation.json'))
