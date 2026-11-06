#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Convex terrain stays open at grazing angles; real concavities retain GTAO."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', type=Path, default=fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll')
    parser.add_argument('--shader', type=Path, default=fp.COMPILED / 'GTAOContactBloom.bin')
    parser.add_argument('--report', type=Path, default=ROOT / 'out/gtao-grazing/gpu.json')
    parser.add_argument('--expect-bug', action='store_true')
    args = parser.parse_args()
    assert os.name == 'nt', 'Requires Windows D3D9'
    args.report.parent.mkdir(parents=True, exist_ok=True)
    exe = args.report.parent / 'gtao-grazing.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    '-Wno-return-type', *fp.include_flags(), str(ROOT / 'tests/gtao_grazing_gpu.cpp'),
                    '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe), str(args.shader.resolve()), str(args.backend.resolve()), str(args.report.resolve())],
                   cwd=exe.parent, check=True, timeout=60)
    result = json.loads(args.report.read_text())
    assert result['case_count'] == 48
    if args.expect_bug:
        assert result['failures'] > 0, 'Original grazing regression no longer reproduced'
    else:
        assert result['failures'] == 0, result
        contact = [case for case in result['cases'] if case['shape'] == 3]
        assert all(case['min'] < .99 and case['dark'] > 0 for case in contact), 'Real concave contact was lost'
    print('PASS original grazing regression reproduced' if args.expect_bug else 'PASS open planes/convex faces; concave contact retained')


if __name__ == '__main__':
    main()
