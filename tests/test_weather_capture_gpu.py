#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""One-shot weather depth/stencil/context capture using the real GPU export helper."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--backend', type=Path)
args = parser.parse_args()
if os.name != 'nt':
    raise SystemExit('Windows required')
out = Path(tempfile.mkdtemp(prefix='weather-capture-', dir=fp.output_dir()))
backend = args.backend or Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'
exe = out / 'weather-capture.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec',
                *fp.test_include_flags(), str(fp.TESTS / 'weather_capture_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
guard = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Wow.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True, check=True)
assert not any(line.startswith('"Wow.exe"') for line in guard.stdout.splitlines()), 'Stop the game before the private GPU fixture'
subprocess.run([str(exe), str(backend.resolve()), str(out), '1' if args.backend else '0'], cwd=out, check=True, timeout=180)
for folder in out.glob('case-*'):
    metadata = json.loads((folder / 'capture-1-weather.json').read_text())
    assert metadata['schema'] == 'northlight-weather-capture-v1'
    if not args.backend and metadata['source_samples']:
        assert not metadata['complete'] and metadata['reason'] == 'multisample-capture-not-verified'
        assert metadata['depth_file'] is None and metadata['name_mask_file'] is None
        continue
    assert metadata['complete']
    assert metadata['renderer_frame'] == 123 and (metadata['width'], metadata['height']) == (37, 23)
    assert [metadata['quality'][key] for key in ('WeatherWindDirection', 'WeatherWindStrength', 'WeatherCameraMotion')] == [270, 175, 0]
    assert len(metadata['constants']) == 10 and all(len(row) == 4 for row in metadata['constants'])
    assert metadata['constants'][1][3] == 7.25 and metadata['constants'][0][3] == 1277
    assert metadata['name_mask_required'] and metadata['stencil_exported']
    assert metadata['kind'] == 'rain' and not metadata['depth_reconstructed'] and not metadata['per_sample']
    for key in ('depth_file', 'scene_file', 'name_mask_file'):
        assert (folder / metadata[key]).is_file()
for name in ('missing-mask', 'failed-mask', 'skipped', 'invalid-context'):
    metadata = json.loads((out / name / 'capture-1-weather.json').read_text())
    assert not metadata['complete']
    if name in ('skipped', 'invalid-context'):
        assert metadata['status'] == 'unavailable' and metadata['constants'] is None and not metadata['depth_file']
    else:
        assert metadata['name_mask_required'] and not metadata['stencil_exported'] and metadata['name_mask_file'] is None
print('PASS weather capture metadata and incomplete exports:', out)
