#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=zig,client
"""Manual Windows DXVK atlas upload and diagnostic render validation; no game launch.

Pass --proxy /path/to/copied/d3d9.dll to exercise that DLL's own stream instead.
The copied proxy's backend/config must already be next to the helper executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--proxy', type=Path)
parser.add_argument('--out', type=Path, default=fp.REPO/'out/ui-atlas-gpu')
parser.add_argument('--opt', choices=('O0', 'O2'), default='O2')
args = parser.parse_args()
if sys.platform != 'win32':
    raise SystemExit('This manual backend probe needs Windows/Vulkan; no game is launched.')
folder = args.out.resolve()
folder.mkdir(parents=True, exist_ok=True)
exe = folder/'ui-atlas-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-'+args.opt,
                '-Wno-microsoft-exception-spec', '-UNDEBUG', *fp.test_include_flags(),
                str(fp.TESTS/'validate_ui_atlas_gpu.cpp'), '-luser32', '-o', str(exe)],
               check=True, env=fp.zig_env())
backend = args.proxy.resolve() if args.proxy else fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
config = folder/'dxvk-test.conf'
config.write_text('dxvk.enableAsync = False\n')
env = dict(os.environ, DXVK_LOG_PATH=str(folder), DXVK_LOG_LEVEL='info', DXVK_CONFIG_FILE=str(config))
run = subprocess.run([str(exe), str(backend), str(folder/'atlas'), *(['single'] if args.proxy else [])],
                     cwd=folder, env=env, capture_output=True, text=True, timeout=120)
report = {'returncode': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr,
          'backend': str(backend), 'backend_sha256': hashlib.sha256(backend.read_bytes()).hexdigest()}
(folder/('result-'+args.opt+'.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(run.stdout + run.stderr)
run.check_returncode()

# Check captured artifacts independently of the C++ observer's return value.
vertices = b''.join(struct.pack('<6f', n, n+1, 0, 1, n/32, (n+1)/32) for n in range(20))
for mode in ('direct', 'stream') if not args.proxy else ('direct',):
    for kind, (offset, first_vertex, indices) in enumerate(((72, 3, (3, 5, 4)), (120, 5, (3, 4, 5)),
                                                          (48, 2, (2, 4, 3)), (0, 0, (0, 1, 2)))):
        prefix = folder/f'atlas-uidraw-{mode}-kind{kind}'
        capture = json.loads(Path(str(prefix)+'.draw.json').read_text())
        assert capture['geometry_range_valid'] and capture['referenced_indices'] == list(indices)
        stream = capture['streams'][0]
        assert stream['saved'] and stream['read_offset'] == offset and stream['data_stride'] == 24
        assert stream['read_hresult'] == stream['unlock_hresult'] == '00000000'
        assert Path(str(prefix)+'.s0.vb').read_bytes() == vertices[first_vertex*24:(first_vertex+3)*24]
        assert stream['UV_samples'][0]['uv'] == [first_vertex/32, (first_vertex+1)/32]
        if kind in (0, 2):
            assert Path(str(prefix)+'.ib').read_bytes() == struct.pack('<3H', *indices)
        sampler = next(s for s in capture['textures'][0]['samplers'] if s['type'] == 6)
        assert sampler['mirror_known'] and sampler['actual'] == 1 and sampler['mirror'] == 2
        assert not sampler['matches_mirror']
        assert capture['vs_float_constants']['mirror_differences'] == [{'register': 0, 'mirror_bits': [0, 0, 0, 0]}]
        assert not capture['ps_float_constants']['mirror_differences']
        assert Path(str(prefix)+'.vs-float.bin').read_bytes()[:16] == struct.pack('<4f', .125, .25, .5, 1)
        assert capture['vertex_shader']['saved'] and capture['pixel_shader']['saved']
        assert len(Path(str(prefix)+'.vs.bin').read_bytes()) == 32
        assert len(Path(str(prefix)+'.ps.bin').read_bytes()) == 20
    oversized = json.loads((folder/f'atlas-uidraw-{mode}-oversize.draw.json').read_text())
    assert not oversized['geometry_range_valid'] and not oversized['streams'][0]['saved']
print('UIDRAW artifact bytes, offsets, UVs, shaders, constants and mirror differences: passed')
