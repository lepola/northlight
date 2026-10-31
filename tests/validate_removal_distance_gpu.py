#!/usr/bin/env python3
"""Manual Windows GPU regression; no game is launched.

Set NORTHLIGHT_ZIG and NORTHLIGHT_DXVK_D3D9, then run this script.
Optional --capture-prefix replays a Northlight diagnostic capture as well.
Synthetic tests provide full-resolution depth; saved captures reconstruct it
from half-resolution distance and therefore cannot alone prove edge behavior.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def image(path, width, height, fmt, pixel_format, pixels):
    size = struct.calcsize('<' + pixel_format)
    with path.open('wb') as f:
        f.write(struct.pack('<5I', 0x31524746, width, height, fmt, size))
        for pixel in pixels:
            f.write(struct.pack('<' + pixel_format, *pixel))


def fixture(folder, name, width, height):
    prefix = str(folder / (name + '-'))
    c = [[0.] * 4 for _ in range(68)]
    c[0] = [1 / width, 1 / height, .2, 1277.]
    c[1] = [1., 1., 1., 0.]
    c[2] = [1., 48., 192., 1 / 1024]
    for i in range(4):
        c[3 + i][i] = 1.
    c[20][2] = .85
    c[30] = [1., float(name != 'no-source'), 1., 794.8572]
    c[33] = [width, height, width // 2, height // 2]
    if name == 'fog':
        c[25] = [.002, 0., 1., 1.]
        c[26] = [.3, .22, .43, 0.]
    if name == 'dense-fog':
        c[25] = [-.00078308536, 1., 1.872, 1.]
        c[26] = [.29803923, .21960786, .43137258, 0.]
    Path(prefix + 'constants.f32').write_bytes(struct.pack('<272f', *sum(c, [])))
    depth = []
    for y in range(height):
        for x in range(width):
            z = 25.
            if name in ('edges', 'odd', 'no-source', 'fog'):
                z = 5 + 2 * math.sin(x * .2) if (x // 11 + y // 13) % 3 == 0 else 100 + 10 * math.sin(y * .3)
            if name == 'near-far':
                z = math.exp(math.log(.202) + x / (width - 1) * math.log(1277 * .9 / .202))
            if name == 'dense-fog':
                z = 400 + 860 * x / (width - 1)
            d = 1277 / (1277 - .2) * (1 - .2 / z)
            if name == 'sky' or (name == 'edges' and x > width // 2):
                d = 1.
            depth.append((d,))
    image(Path(prefix + 'depth.fgr'), width, height, 114, 'f', depth)
    image(Path(prefix + 'scene.fgr'), width, height, 21, '4B',
          (((x * 17 + y * 3) % 201 + 20, (x * 5 + y * 7) % 201 + 20, (x * 11 + y * 9) % 201 + 20, 255)
           for y in range(height) for x in range(width)))
    w, h = width // 2, height // 2
    image(Path(prefix + 'light.fgr'), w, h, 113, '4e',
          ((-.4 + .3 * (math.sin(x * .2) + 1), -.2 + .15 * (math.cos(y * .2) + 1), .1,
            .2 if (x // 5 + y // 7) % 2 == 0 else 1.) for y in range(h) for x in range(w)))
    image(Path(prefix + 'baseline.fgr'), w, h, 113, '4e', ((.6, .6, .6, 1.) for _ in range(w * h)))
    image(Path(prefix + 'normals.fgr'), w, h, 113, '4e', ((0., 0., 1., 1.) for _ in range(w * h)))
    image(Path(prefix + 'distance.fgr'), w, h, 114, 'f', ((25.,) for _ in range(w * h)))
    image(Path(prefix + 'ao.fgr'), 61, 39, 21, '4B',
          ((0, 0, 0, (x * 37 + y * 19) % 256) for y in range(39) for x in range(61)))
    return prefix


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture-prefix')
    ap.add_argument('--reference-shader', type=Path)
    ap.add_argument('--reference-normals', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    backend = os.environ.get('NORTHLIGHT_DXVK_D3D9')
    if os.name != 'nt' or not backend or not Path(backend).is_file():
        raise SystemExit('Requires Windows and NORTHLIGHT_DXVK_D3D9 pointing to DXVK 3 d3d9.dll')
    with tempfile.TemporaryDirectory(prefix='removal-distance-') as temp:
        folder = Path(temp)
        binary = folder / 'test.exe'
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', str(fp.TESTS / 'removal_distance_gpu.cpp'),
                        '-luser32', '-o', str(binary)], env=fp.zig_env(), check=True)
        reference = fp.TESTS / 'support/removal_distance'
        cases = [fixture(folder, name, 137 if name in ('odd', 'no-source', 'fog', 'near-far') else 128,
                         79 if name in ('odd', 'no-source', 'fog', 'near-far') else 96)
                 for name in ('flat', 'edges', 'odd', 'sky', 'no-source', 'fog', 'dense-fog', 'near-far')]
        if args.capture_prefix:
            cases.append(args.capture_prefix)
        rows = []
        for prefix in cases:
            constants = Path(prefix + 'constants.f32').read_bytes()
            if len(constants) not in (68 * 16, 79 * 16):
                raise ValueError('Invalid constants length: expected 68 or 79 float4 registers')
            provenance = dict(constant_register_count=len(constants) // 16,
                              constant_bank_sha256=hashlib.sha256(constants).hexdigest(),
                              constant_bank_stage='unversioned',
                              input_origin='saved-aggregate' if prefix == args.capture_prefix else 'authored-fixture',
                              depth_reconstructed=not Path(prefix + 'depth.fgr').is_file(),
                              invented_fog_environment=False, per_source_bank_missing=True,
                              material_inputs_complete=False, authentic_source_replay=False, whole_scene_replay=False)
            result = subprocess.run([str(binary), prefix, str(args.reference_shader or reference / 'RemovalSmooth.bin'),
                                     str(fp.COMPILED / 'RemovalSmooth.bin'), str(args.reference_normals or reference / 'WorldNormals.bin'),
                                     str(fp.COMPILED / 'WorldNormals.bin'), Path(prefix).name, backend],
                                    cwd=folder, capture_output=True, text=True, check=True, timeout=60)
            print(result.stdout, end='', flush=True)
            for line in result.stdout.splitlines():
                if line.startswith('{'):
                    row = json.loads(line)
                    row['input_provenance'] = provenance
                    rows.append(row)
        if args.output:
            args.output.write_text(json.dumps(rows, indent=2) + '\n')
        print('PASS removal distance: alpha/confidence exact, finite output, component error <= 1/1024')


if __name__ == '__main__':
    main()
