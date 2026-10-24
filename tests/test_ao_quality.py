#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production quality variants retain the existing AO and grazing contracts."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    assert os.name == 'nt', 'Requires Windows D3D9'
    output = Path(os.environ.get('NORTHLIGHT_TEST_OUTPUT_DIR', ROOT / 'out')) / 'ao-quality'
    output.mkdir(parents=True, exist_ok=True)
    backend = Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'
    baseline = json.loads((ROOT / 'tests/fixtures/ao_baseline_sha256.json').read_text())
    for entry, digest in baseline.items():
        assert hashlib.sha256((fp.COMPILED / (entry + '.bin')).read_bytes()).hexdigest() == digest, entry + ' High bytecode changed'
    # Count the loop trip counts in the actual Microsoft disassembly, rather
    # than checking source labels that could disagree with the compiled kernel.
    counts = {'HDAO': ([16], [8], [8]), 'HBAOPlus': ([32], [18], [16]), 'GTAO': ([4, 4], [3, 4], [2, 4])}
    for method, expected in counts.items():
        for quality, trips in zip(('', 'Medium', 'Low'), expected):
            assembly = (fp.COMPILED / (method + 'ContactBloom' + quality + '.bin.asm')).read_text()
            constants = dict(re.findall(r'\bdefi\s+(i\d+),\s*(\d+),', assembly))
            actual = [int(constants[register]) for register in re.findall(r'\brep\s+(i\d+)\b', assembly)]
            assert actual == trips, (method, quality, actual, trips)
    executables = {}
    for stand in ('ao_methods_gpu', 'gtao_grazing_gpu'):
        exe = output / (stand + '.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        '-Wno-return-type', *fp.include_flags(), str(ROOT / 'tests' / (stand + '.cpp')),
                        '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
        executables[stand] = exe
    for quality in ('', 'Medium', 'Low'):
        if quality:
            methods = output / (quality + '-methods.json')
            subprocess.run([str(executables['ao_methods_gpu']), str(fp.COMPILED), str(backend), str(methods), quality], cwd=output, check=True, timeout=120)
            result = json.loads(methods.read_text())
            assert result['case_count'] == 99 and result['failures'] == 0, result
        grazing = output / ((quality or 'High') + '-grazing.json')
        subprocess.run([str(executables['gtao_grazing_gpu']), str(fp.COMPILED / ('GTAOContactBloom' + quality + '.bin')), str(backend), str(grazing)], cwd=output, check=True, timeout=60)
        result = json.loads(grazing.read_text())
        assert result['case_count'] == 48 and result['failures'] == 0, result
        assert all(case['min'] < .99 and case['dark'] > 0 for case in result['cases'] if case['shape'] == 3), 'Real concave contact was lost'
    print('PASS Medium/Low: original 99 AO assertions each; High/Medium/Low: original 48 grazing assertions each; exact compiled loop counts and unchanged High bytecode')


if __name__ == '__main__':
    main()
