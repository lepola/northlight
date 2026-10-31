#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Frozen shipped assembly oracle and bounded far-terrain chunk provenance, CPU only."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
parser.add_argument('--baseline-ref', default='cc69996')
args = parser.parse_args()
out = args.output or fp.out()/'scene-assembly'
out.mkdir(parents=True, exist_ok=True)
for name in ['world_gi.h', 'world_gi.cpp']:
    data = subprocess.check_output(['git', 'show', args.baseline_ref+':src/gi/'+name], cwd=fp.REPO).decode()
    data = data.replace('NorthlightGI', 'FrozenGI').replace('"world_gi.h"', '"frozen_world_gi.h"')
    (out/('frozen_'+name)).write_bytes(data.encode())
results = []
for label, flags in [('O2', ['-O2']), ('debug', ['-O0', '-g'])]:
    exe = out/(label+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-UNDEBUG', *flags,
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-I', str(out),
                    *fp.test_include_flags(), str(fp.TESTS/'test_scene_assembly.cpp'),
                    str(fp.src('world_mesh_plan.cpp')), '-o', str(exe)], env=fp.zig_env(), check=True)
    text = subprocess.check_output([str(exe), *([str(fp.world_cache())] if label == 'O2' else [])], text=True)
    (out/(label+'.log')).write_bytes(text.encode())
    print(label+'\n'+text, end='')
    results.append({'configuration': label, 'pass': True, 'real_sources': label == 'O2'})
(out/'validation.json').write_bytes((json.dumps({'baseline_ref': args.baseline_ref, 'game_launched': False,
    'gpu_used': False, 'runs': results, 'all_pass': True}, indent=2)+'\n').encode())
