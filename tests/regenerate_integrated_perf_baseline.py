#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Rebuild the combined-ABI performance baseline with only the fog/composition optimizations reverted."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler, validate_assembly

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--compatible-compiler', action='store_true')
args = parser.parse_args()
original = (fp.SHADERS/'world_effects.hlsl').read_bytes()
source = original.decode('utf-8')
support = r'    // Skip whole world-grid intervals beyond the regional field support\.\n.*?\n    \[loop\]for\(int i=first;i<(\d+)&&max\(0,\(i-offset\)\*stepLength\)<marchEnd;\+\+i\)\{'
source, count = re.subn(support, lambda m: '    [loop]for(int i=0;i<'+m[1]+'&&max(0,(i-offset)*stepLength)<surface;++i){', source, flags=re.S)
assert count == 2
for before, after in (
    ('float transmittance=1,directWeight=0;', 'float transmittance=1;float3 directScatter=0;'),
    ('directWeight+=transmittance*absorb*(', 'directScatter+=transmittance*absorb*sunSource*('),
    ('    float3 directScatter=sunSource*directWeight;\n', ''),
):
    assert source.count(before) == 1, before
    source = source.replace(before, after)
flat = '[loop]for(int tap=0;tap<4;++tap){\n            float row=floor(tap*.5);\n            float2 bit=float2(tap-row*2,row);'
assert source.count(flat) == 1
source = source.replace(flat, '[loop]for(int pair=0;pair<2;++pair){\n        [unroll]for(int sub=0;sub<2;++sub){\n            float2 bit=float2(sub,pair);')
end = '    // Retain only fallback coordinates across the loop; fetch colours only on fallback.'
assert source.count(end) == 1
source = source.replace(end, '        }\n'+end)
out = fp.out()/'integrated-perf-baseline'
out.mkdir(parents=True, exist_ok=True)
for include in fp.SHADERS.glob('*.hlsl'):
    shutil.copy2(include, out/include.name)
path = out/'IntegratedBefore.hlsl'
path.write_text(source, encoding='utf-8', newline='\n')
compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
fixtures = fp.TESTS/'fixtures/performance-before'
hashes = {}
for entry in ('WorldFog', 'FogClouds', 'WorldComposite'):
    data, _ = compiler.compile(path, entry, 'ps_3_0', out/(entry+'.bin'))
    validate_assembly(out/(entry+'.bin.asm'), 'ps_3_0')
    target = fixtures/('perf-before-'+entry+'.bin')
    target.write_bytes(data)
    hashes[target.name] = hashlib.sha256(data).hexdigest()
(fixtures/'performance-before-sha256.json').write_text(json.dumps(hashes, indent=2)+'\n', newline='\n')
(fixtures/'integrated-baseline.json').write_text(json.dumps({
    'purpose': 'Current integrated shader ABI, with interval skipping, regional support clipping, scalar scattering and flattened composite taps reverted.',
    'optimized_source_sha256': hashlib.sha256(original).hexdigest(),
    'baseline_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'bump_include_sha256': hashlib.sha256((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes()).hexdigest(),
    **compiler.provenance,
}, indent=2)+'\n', newline='\n')
