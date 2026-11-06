#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Issue #101: stationary sources, partial batches and direct/fog scissor parity."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['SystemRoot']) / 'SysWOW64' / 'd3d9.dll'))
output = fp.output_dir() / Path(backend).stem
output.mkdir(parents=True, exist_ok=True)
executable = output / 'particle-lighting.exe'
source = fp.TESTS / 'particle_lighting_gpu.cpp'
paths = [source, fp.src('particle_lights.h'), fp.src('local_light_selection.h'), fp.src('local_light_scissor.h'),
         fp.COMPILED / 'LocalDirect.bin', fp.COMPILED / 'LocalDirect1.bin', fp.COMPILED / 'LocalFog.bin']
identity = {str(path.relative_to(fp.REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-return-type',
                *fp.include_flags(), str(source), '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
for label,extra,count in [('original',[],144),('single',[str(fp.COMPILED/'LocalDirect1.bin')],192)]:
    result = subprocess.run([str(executable), str(fp.COMPILED), backend,*extra], cwd=output, text=True, capture_output=True, timeout=90)
    (output / (label+'.log')).write_text(result.stdout + result.stderr)
    print(result.stdout, end='')
    assert result.returncode == 0, result.stdout + result.stderr
    assert f'cases={count} lit={count} failures=0 stationaryFrames=64 actualItemGeometry=0' in result.stdout
assert identity == {str(path.relative_to(fp.REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
(output / 'validation.json').write_text(json.dumps({'passed': True, 'backend': backend, 'inputs': identity,
                                                 'actual_item_geometry': False, 'whole_scene': False,
                                                 'game_launched': False, 'fps_measurement': False}, indent=2) + '\n')
