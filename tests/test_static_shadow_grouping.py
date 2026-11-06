#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Static worker grouping oracle; optional frozen-source and real FGS CPU comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref')
parser.add_argument('--model', action='append', type=Path, default=[])
args = parser.parse_args()
out = fp.out()/'static-shadow-grouping'
out.mkdir(parents=True, exist_ok=True)
sources = [('current', [])]
if args.baseline_ref:
    relative = fp.src('static_shadow_scene.cpp').relative_to(fp.REPO).as_posix()
    baseline = out/'baseline.cpp'
    baseline.write_bytes(subprocess.check_output(['git', 'show', args.baseline_ref+':'+relative], cwd=fp.REPO))
    # Freeze declarations too: the production Streamer may gain optional providers.
    (out/'static_shadow_scene.h').write_bytes(subprocess.check_output(['git','show',args.baseline_ref+':'+fp.src('static_shadow_scene.h').relative_to(fp.REPO).as_posix()],cwd=fp.REPO))
    sources.append(('baseline', ['-DSTATIC_SHADOW_IMPLEMENTATION="'+baseline.as_posix()+'"']))
inputs = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.model}
results = {}
binaries = {}
for name, flags in sources:
    exe = out/(name+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                    str(fp.TESTS/'test_static_shadow_grouping.cpp'), str(fp.src('world_gi.cpp')),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    binaries[name] = exe
    results[name] = {'runs': []}
for round in range(3 if args.model and args.baseline_ref else 1):
    order = list(binaries) if round&1 else list(reversed(binaries))
    for name in order:
        text = subprocess.check_output([str(binaries[name]), str(out/name), *map(str, args.model)], text=True)
        print(name, 'round', round, text, flush=True)
        parsed = dict(line.split(' ', 1) for line in text.splitlines() if line.startswith('STATIC_GROUP_FIXTURE'))
        parsed['assets'] = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in text.splitlines() if line.startswith('STATIC_GROUP_ASSET')]
        results[name]['runs'].append(parsed)
for name in binaries:
    runs = results[name]['runs']
    results[name]['STATIC_GROUP_FIXTURE'] = runs[0]['STATIC_GROUP_FIXTURE']
    assert all(r['STATIC_GROUP_FIXTURE']==runs[0]['STATIC_GROUP_FIXTURE'] for r in runs)
    results[name]['assets'] = []
    for i, asset in enumerate(runs[0]['assets']):
        stable = {k:v for k,v in asset.items() if k!='decodeMedianMs'}
        assert all({k:v for k,v in r['assets'][i].items() if k!='decodeMedianMs'}==stable for r in runs)
        results[name]['assets'].append(dict(stable, decodeMedianMs=statistics.median(float(r['assets'][i]['decodeMedianMs']) for r in runs)))
if args.baseline_ref:
    assert results['current']['STATIC_GROUP_FIXTURE'] == results['baseline']['STATIC_GROUP_FIXTURE']
    for actual, expected in zip(results['current']['assets'], results['baseline']['assets']):
        assert {k:v for k,v in actual.items() if k!='decodeMedianMs'} == {k:v for k,v in expected.items() if k!='decodeMedianMs'}
        actual['speedup'] = float(expected['decodeMedianMs'])/float(actual['decodeMedianMs'])
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==sha for p,sha in inputs.items())
results['inputs'] = inputs
(out/'validation.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
print('PASS static worker grouping: exact input/output digests; no source asset writes')
