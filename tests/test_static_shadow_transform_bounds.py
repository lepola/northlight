#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Production static-caster bounds and independent endpoints; no game/GPU/benchmark."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
out = args.output or fp.output_dir() / 'static-shadow-transform-bounds'
out.mkdir(parents=True, exist_ok=True)
source = fp.src('static_shadow_scene.cpp')
watched = [Path(__file__).resolve(), fp.TESTS / 'test_static_shadow_transform_bounds.cpp',
           source, fp.src('static_shadow_scene.h'), fp.src('static_shadow_gpu.h'),
           fp.src('shadow_bounds.h'), fp.TESTS / 'test_static_shadow_fake_d3d.h', fp.src('world_gi.cpp')]
before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
results = {'inputs': before, 'cases': [], 'pass': False, 'game_or_gpu_used': False, 'fps_claimed': False}


def run(label, implementation, flags, expected=0):
    exe = out / label
    command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *flags,
               *fp.test_include_flags(), '-I', str(fp.SUPPORT),
               '-DSTATIC_SHADOW_IMPLEMENTATION="' + implementation.as_posix() + '"',
               str(fp.TESTS / 'test_static_shadow_transform_bounds.cpp'), str(fp.src('world_gi.cpp')), '-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True)
    (out / (label + '-compile.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe.resolve())], capture_output=True, text=True)
    (out / (label + '-run.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == expected, result.stdout + result.stderr
    marker = 'STATIC_CASTER_FALSE_REJECTION' if expected else 'PASS conservative static caster bounds:'
    assert marker in result.stdout + result.stderr, result.stdout + result.stderr
    results['cases'].append({'label': label, 'command': command, 'returncode': result.returncode,
                             'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'marker': marker})


try:
    for label, flags in (('O2', ['-O2']), ('O0', ['-O0', '-g']),
                         ('ASan-UBSan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])):
        run(label, source, flags)
    # Restore only the frozen old bounds function in a separate source copy.
    # All other production code, including actual plan/draw selection, is unchanged.
    text = source.read_text(encoding='utf-8')
    begin = text.index('void transformBounds(')
    at = text.index('{', begin)
    depth = 1
    end = at + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    frozen = '''void transformBounds(const Placement& p,Vec3 low,Vec3 high,Vec3& outLow,Vec3& outHigh){
Vec3 c=transformPoint(p,(low+high)*.5f),r=(high-low)*.5f;const float* m=p.matrix;
Vec3 e={std::fabs(m[0])*r.x+std::fabs(m[1])*r.y+std::fabs(m[2])*r.z,std::fabs(m[3])*r.x+std::fabs(m[4])*r.y+std::fabs(m[5])*r.z,std::fabs(m[6])*r.x+std::fabs(m[7])*r.y+std::fabs(m[8])*r.z};outLow=c-e;outHigh=c+e;}'''
    mutant = out / 'old-bounds-only.cpp'
    mutant.write_text(text[:begin] + frozen + text[end:], encoding='utf-8')
    results['negative_source_sha256'] = hashlib.sha256(mutant.read_bytes()).hexdigest()
    run('old-bounds-negative', mutant.resolve(), ['-O2'], expected=2)
    results['pass'] = True
finally:
    results['inputs_unchanged'] = before == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    results['pass'] = results['pass'] and results['inputs_unchanged']
    (out / 'validation.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
assert results['pass']
print('PASS static shadow transform bounds, exact negative-control marker and source guards')
