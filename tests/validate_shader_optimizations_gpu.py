"""Compare frozen/current production shader binaries on an offscreen D3D9 device.

The baseline must be saved before editing shaders. This never loads Northlight,
changes a client directory, or launches the game. JSONL includes pixel errors and
paired GPU timestamps; small correctness targets are not an FPS benchmark.
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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def image(path, w, h, pixel):
    values = [component for y in range(h) for x in range(w) for component in pixel(x, y)]
    path.write_bytes(struct.pack('<5I', 0x31524746, w, h, 116, 16) + struct.pack('<%df' % len(values), *values))


def fixtures(out, only=None, width=96, height=64, selected=None, msaa=(0, 4), snow_art=None):
    snow_data = None
    if snow_art:
        from PIL import Image, ImageChops, ImageOps
        with Image.open(snow_art) as source:
            rgba = source.convert('RGBA')
            level = ImageChops.multiply(ImageOps.grayscale(rgba), rgba.getchannel('A')).resize((512, 512), Image.Resampling.LANCZOS)
            values = [component for value in level.tobytes() for component in (value / 255,) * 4]
            snow_data = struct.pack('<5I', 0x31524746, *level.size, 116, 16) + struct.pack('<%df' % len(values), *values)
    entries = {
        'surface': 'RainQuality RainPerformance SnowQuality SnowPerformance RainLens SnowLens WeatherLensParticle FrostVignette SandLens'.split(),
        'ao': 'AO AOOnly AOContactBloom ContactBloom AOBlur Composite HDAOOnly HDAOOnlyMedium HDAOOnlyLow HBAOPlusOnly HBAOPlusOnlyMedium HBAOPlusOnlyLow GTAOOnly GTAOOnlyMedium GTAOOnlyLow ASSAOOnly ASSAOOnlyMedium ASSAOOnlyLow'.split(),
        'hdr': 'HdrWorld HdrMeter HdrDecodeWorld HdrBloom HdrBloomHorizontal HdrFixedUi HdrFixedUi2 HdrLegacyComposite HdrAdapt HdrWorldLinearOnly HdrLegacyEncode HdrLayer'.split(),
        'world': 'LocalDirect LocalDirect1 LocalDirect16 LocalFog DirectionalShadow WorldLighting WorldLightingSoft MaterialComposite MaterialCompositeHdr WorldComposite WorldCompositeHdr'.split(),
        'local': 'LocalShadowResolve LocalLighting LocalLightingSoft'.split(),
        'normal': ['AutoNormal', 'AutoNormalModel'],
    }
    jobs = []
    for family, shaders in entries.items():
        if only and family not in only:
            continue
        for entry in shaders:
            if selected and entry not in selected:
                continue
            for case in range(8):
                w, h = (width, height) if case % 2 == 0 else (width + 1, height + 3)
                folder = out / f'{entry}-{case}'
                folder.mkdir(parents=True, exist_ok=True)
                c = [[0.] * 4 for _ in range(128)]
                c[0] = [1 / w, 1 / h, .2, 1000]
                c[1] = [1.2, 1.4, 1, 0]
                c[2] = [1, 0, 0, 0]
                for i in range(4):
                    c[3 + i][i] = 1
                flow, state = [.07, .1, .015, (0, .25, .75, 1)[case % 4]], [.9, .372, case * .73, .7]
                textures = {s: (8, 8, lambda x, y: (.25, .375, .5, 1)) for s in range(16)}
                raw_depth = lambda z: 1000 / 999.8 * (1 - .2 / z)

                def depth(x, y):
                    z = (2, 3, 8, 16, 120, 1000)[min(5, x * 6 // w)]
                    if case == 0:
                        z = 6 + x / max(w, 1) * .12
                    elif case == 1:
                        z = 1000
                    elif case == 6:
                        z = 180
                    elif case == 7:
                        z = 3 + 4 * ((x // 7 + y // 5) % 2)
                    return (raw_depth(z), 0, 0, 1)

                palette = lambda x, y: ((x % 8) / 7, (y % 8) / 7, ((x + y) % 8) / 7, 0 if case == 0 else .8)
                textures[0] = (32, 32, palette)
                if family == 'hdr' and case == 6:
                    textures[0] = (32, 32, lambda x, y: (-.4, .2, 2., 1))
                linear = 1
                mip = 0
                cube = 0xFFFFFFFF
                blend, mask = 0, 15
                if family == 'surface':
                    c[2] = [0, 1, 0, 0]
                    c[3] = [(0, .12, .5, 1)[case % 4], 0, .3, 0]
                    for i in range(4):
                        c[4 + i] = [float(j == i) for j in range(4)]
                    c[1][3] = case * 13.17
                    c[8] = [.15, .2, .3, 1]
                    c[9] = [.6, .5, .4, 1]
                    c[10] = [case * 7.3, .8, w / h, .65]
                    if entry == 'WeatherLensParticle':
                        c[10][0] = 2 if case == 7 else 0
                    c[11] = [case / 7, case / 7, .3, 1]
                    textures[3] = (w, h, depth)
                    for s in (1, 2, 4):
                        textures[s] = (32, 32, lambda x, y: (float((x * 7 + y * 13) % 31) / 30,) * 4)
                    linear = 1 | 2 | 4 | 16
                    mip = 2 | 4 | 16
                    if entry == 'FrostVignette':
                        blend, mask = 1, 7
                elif family == 'ao':
                    c[1] = [1.2, 1.4, 0 if case == 2 else .8, .4]
                    c[2] = [0 if case % 2 == 0 else .15, (.1, .3, 1, 2, 4, 10, .1, 1)[case], 0, 0]
                    c[3] = [0, 1, 1, 0]
                    c[4] = [float(case == 3), 0, 0, 0]
                    c[5] = [.01, .1, 1, float(case == 5)]
                    c[6] = [.2, .3, .4, 0]
                    if entry == 'ContactBloom':
                        c[1][3] = 0  # Ready world: the production off path can choose a neutral clear.
                    textures[1] = (w, h, depth)
                    textures[2] = (w // 2, h // 2, lambda x, y: (.02, .01, .03, .55 if x % 3 == 0 else 1))
                    textures[3] = (w, h, lambda x, y: (2., 1 if x < w // 2 else 0, 0, 0))
                    if entry == 'AOBlur':
                        textures[0] = textures[2]
                elif family == 'hdr':
                    c[0] = [1, .9, 1.1, 12]
                    c[1] = [1 if case % 2 == 0 else 1.15, 1.1, 0 if case % 2 else 1, .15 if case % 2 else 0]
                    c[2] = [1 / w, 1 / h, float(case % 2), .3 if case > 3 else 0]
                    c[3] = [.016, .3, 1.2, .18]
                    c[4] = [-3, 3, float(case % 2), 0]
                    c[5] = [.02, 0, 1, float(case % 2)]
                    c[6] = [.3, .2, .1, 0]
                    if entry == 'HdrWorldLinearOnly':
                        c[2] = [-.1, 4, 0, 0]
                    if entry in ('HdrFixedUi', 'HdrFixedUi2'):
                        c[1] = [float(case % 2), 1, float(case % 2), 1]
                        c[2] = [0, 1, 0, 1]
                        c[3] = [0, 1, 0, 0]
                    if entry == 'HdrLayer':
                        c[2] = [float(case % 2 == 0), float(case < 4), 0, 0]
                    if entry == 'HdrAdapt' and case == 2:
                        textures[0] = (8, 8, lambda x, y: (-8, 0, 0, 1))
                        textures[1] = (8, 8, lambda x, y: (3, 0, 0, 1))
                    if entry == 'HdrLegacyComposite':
                        c[2] = [0, 1, .2, 1000]
                        textures[1] = (w, h, depth)
                        if case == 1:
                            textures[3] = (8, 8, lambda x, y: (0, 0, 0, 1))
                    if entry == 'HdrLegacyEncode' and case == 1:
                        textures[0] = (8, 8, lambda x, y: (.001, .002, .003, .4))
                    if entry == 'HdrDecodeWorld':
                        c[2] = [1 / w, 1 / h, float(case % 2), .6]
                        textures[1] = (w, h, depth)
                        if case == 5:
                            textures[2] = (8, 8, lambda x, y: (-2, .01, 0, 1))
                elif family in ('world', 'local'):
                    c[2] = [1, 48, 192, 1 / 1024]
                    c[15] = [0, 0, 0, 0]
                    c[16] = [.3, .4, .8660254, 0]
                    c[17] = [.8, .6, .4, 1]
                    c[18] = [.2, .3, .4, 0]
                    c[20] = [4, .5, .7, 0]
                    c[21] = [0, 1, 1, 128]
                    c[24] = [.0005, 1, 0, 1]
                    c[25] = [.01, .1, 1, 0]
                    c[26] = [.2, .3, .4, 0]
                    c[30] = [0, 0, 0, 0]
                    c[33] = [w, h, w // 2, h // 2]
                    c[35] = [.005, .2, .1, 0]
                    c[52] = [4, 1, .3, 0]
                    c[58] = [1, .5, 1, 2]
                    c[57] = [0, 10, .01, 3]
                    c[59] = [.03, 0, 0, 0]
                    c[60] = [.03, 0, 0, 0]
                    c[61] = [.03, 0, 0, 0]
                    c[62] = [.03, 0, 0, 0]
                    c[67] = [0, 0, .2 if case % 2 else 0, 0]
                    c[73] = [1, 120, 0, 0]
                    c[78] = [float(entry.startswith('Material')), 120, 0, 0]
                    c[34] = [.2, .3, .4, .5]
                    for i in range(4):
                        c[7 + i] = [float(j == i) * .01 for j in range(4)]
                        c[11 + i] = [float(j == i) * .005 for j in range(4)]
                    if case == 4:
                        c[10][2], c[14][2] = -.019998, -.009999
                    elif case == 5:
                        c[9][2], c[13][2] = .03, .015
                    for i in range(16):
                        pos = [float((i % 4) - 1) * 3, float(i // 4 - 1) * 3, 5, 12]
                        color = [.8, .5, .3, 1 / 12]
                        c[84 + i], c[100 + i] = pos[:], color[:]
                        if i < 8:
                            c[36 + i], c[44 + i] = pos[:], color[:]
                    if case == 1:
                        for i in range(8):
                            c[36 + i][0] += 100
                    def detail(x, y):
                        z = 6 + x / max(w, 1) * .12 if case == 0 else (2, 3, 8, 16, 120, 1000)[min(5, x * 6 // w)]
                        cover = .01 if case % 4 == 0 else 1
                        return (.58 * cover, .41 * cover, (z + 10 if case % 4 == 1 else z) * cover, cover)
                    textures[1] = (w, h, depth)
                    textures[9] = (w, h, detail)
                    textures[14] = (w, h, lambda x, y: (.99995, 0, -.01, .8) if case == 4 else (0, 0, -1 if case % 2 else -.3, .8))
                    textures[12] = (w, h, lambda x, y: (.5, .4, .3, .6))
                    textures[2] = textures[3] = (32, 32, lambda x, y: (.35 if x < 16 else 1, 0, 0, 1))
                    textures[10] = (8, 8, lambda x, y: (0, 0, 0, .8))
                    if entry.startswith('WorldComposite'):
                        textures[9] = (8, 8, lambda x, y: (.02, .03, .04, .9))
                    if family == 'local':
                        textures[0] = (w, h, depth)
                        textures[3] = textures[14]
                        textures[4] = textures[12]
                        textures[5] = textures[9]
                        textures[1] = (32, 32, lambda x, y: (.35 if x < 16 else 1, 0, 0, 1))
                        cube = 0xFFFFFFFF if entry == 'LocalLightingSoft' else 1
                        c[2] = [1, 0, 1, .03]
                        c[7] = [0, 0, 2, 1 / 12]
                        c[8] = [.8, .5, .3, 12]
                        c[9] = [.1, 50, 1 / 32, 1]
                        c[10] = [1, .3, .7, .8]
                        c[11] = [.3, .4, .866, 0]
                        c[12] = [1, 120, 0, 0]
                elif family == 'normal':
                    c[0] = [1 / 32, 1 / 32, 2, 0]
                    c[1] = [.4, 0 if case % 2 == 0 else .7, .8, 0]
                    if case == 1:
                        textures[0] = (32, 32, lambda x, y: (.4, .4, .2, 1))
                header = [w, h, 4 if case == 7 else 0, blend, cube, mask, linear, mip]
                folder.joinpath('params.bin').write_bytes(struct.pack('<8I', *header) + struct.pack('<512f', *(v for row in c for v in row)) + struct.pack('<8f', *flow, *state))
                for s, (tw, th, pixel) in textures.items():
                    if snow_data and family == 'surface' and s == 2:
                        (folder / f'{s}.fgr').write_bytes(snow_data)
                    else:
                        image(folder / f'{s}.fgr', tw, th, pixel)
                jobs.extend((entry, folder.resolve(), samples) for samples in msaa)
    path = out / 'cases.txt'
    path.write_text(''.join(f'{entry} {folder.as_posix()} {samples}\n' for entry, folder, samples in jobs), encoding='utf-8')
    return path, jobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True, help='Frozen shaders/compiled directory')
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--only', nargs='+', choices=['ao', 'surface', 'hdr', 'world', 'local', 'normal'])
    parser.add_argument('--size', nargs=2, type=int, default=[96, 64])
    parser.add_argument('--entries', nargs='+', help='Limit a tuning rerun to named entries')
    parser.add_argument('--msaa', nargs='+', type=int, choices=[0, 4], default=[0, 4])
    parser.add_argument('--snow-art', type=Path, help='Also compare the authored atlas with its full mip chain')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert os.name == 'nt' and min(args.size) > 1
    out = args.output.resolve()
    assert not fp.is_client(out)
    out.mkdir(parents=True, exist_ok=True)
    cases, jobs = fixtures(out / 'fixtures', args.only, *args.size, args.entries, args.msaa, args.snow_art)
    exe = out / 'shader-oracle.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec', *fp.test_include_flags(), str(fp.TESTS / 'shader_optimizations_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    baseline = args.baseline.resolve()
    provenance = {'game_launched': False, 'backend_sha256': hashlib.sha256(args.backend.read_bytes()).hexdigest(),
                  'input_sha256': {str(p.relative_to(cases.parent)): hashlib.sha256(p.read_bytes()).hexdigest() for p in cases.parent.rglob('*') if p.is_file()},
                  'shaders': {entry: {'before': hashlib.sha256((baseline / (entry + '.bin')).read_bytes()).hexdigest(),
                                      'after': hashlib.sha256((fp.COMPILED / (entry + '.bin')).read_bytes()).hexdigest()} for entry, _, _ in jobs}}
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    with (out / 'results.jsonl').open('w') as log:
        result = subprocess.run([str(exe), str(cases), str(baseline), str(fp.COMPILED), str(args.backend.resolve())], cwd=out, env=dict(os.environ, DXVK_LOG_PATH=str(out)), stdout=log, stderr=subprocess.STDOUT, timeout=1200)
    rows = [json.loads(line) for line in (out / 'results.jsonl').read_text().splitlines() if line.startswith('{')]
    print(json.dumps({'cases': len(rows), 'failures': sum(bool(r['over_tolerance'] or r['nonfinite']) for r in rows), 'results': str(out / 'results.jsonl')}))
    if len(rows) != len(jobs) or result.returncode:
        raise SystemExit(result.returncode or 1)


if __name__ == '__main__':
    main()
