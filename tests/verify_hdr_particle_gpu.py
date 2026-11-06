"""Offline production additive-particle HDR checks on DXVK 2/3; no client writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def analyze(root, report):
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
    images = {}
    comparisons = []
    for run in report['runs']:
        backend = run['backend']
        folder = root / backend
        log = (folder/'hdr_particle_gpu_d3d9.log').read_text(encoding='utf-8', errors='replace')
        run['backend_version'] = next(line.split('DXVK:', 1)[1].strip() for line in log.splitlines() if 'DXVK:' in line)
        for p in sorted(folder.glob('*.rgba16f')):
            pixels = np.fromfile(p, dtype='<f2').astype(np.float32).reshape(540, 960, 4)
            assert np.isfinite(pixels).all()
            images[backend, p.name] = pixels
        for major in (2, 3):
            for samples in (0, 4):
                stem = f'sm{major}-msaa{samples}'
                a = images[backend, stem+'-baseline-linear.rgba16f']
                b = images[backend, stem+'-candidate-linear.rgba16f']
                assert np.array_equal(a[:, :, 3], b[:, :, 3])
                comparisons.append(dict(backend=backend, major=major, samples=samples,
                    baseline_peak=float(a[:, :, :3].max()), candidate_peak=float(b[:, :, :3].max()),
                    brighter_pixels=int(((b[:, :, :3]-a[:, :, :3]).max(axis=2)>.001).sum()),
                    alpha_identical=True))
    cross = []
    for (backend, name), pixels in images.items():
        if backend != 'dxvk2':
            continue
        difference = np.abs(pixels-images['dxvk3', name])
        relative = difference/np.maximum(1, np.abs(pixels))
        assert float(relative.max()) < .003
        cross.append(dict(file=name, max_abs=float(difference.max()), max_relative=float(relative.max())))
    timing = []
    for run in report['runs']:
        rows = [r for r in run['records'] if r['kind'] == 'timing']
        for baseline in (r for r in rows if not r['candidate']):
            candidate = next(r for r in rows if r['candidate'] and all(r[k] == baseline[k] for k in ('major', 'width', 'with_post')))
            a, b = np.array(baseline['gpu_ms']), np.array(candidate['gpu_ms'])
            am, bm = float(np.median(a)), float(np.median(b))
            paired = b-a
            bootstrap = np.random.default_rng(0).choice(paired, size=(10000, len(paired)), replace=True)
            confidence = np.percentile(np.median(bootstrap, axis=1), [2.5, 97.5])
            timing.append(dict(backend=run['backend'], major=baseline['major'], width=baseline['width'], height=baseline['height'],
                with_post=bool(baseline['with_post']), baseline_ms=am, candidate_ms=bm, delta_ms=bm-am, delta_percent=(bm/am-1)*100,
                paired_median_delta_ms=float(np.median(paired)), paired_median_bootstrap_95_ms=confidence.tolist(),
                baseline_range_ms=[float(a.min()), float(a.max())], candidate_range_ms=[float(b.min()), float(b.max())]))
    report['visual_comparisons'] = comparisons
    report['cross_backend'] = cross
    report['timing_summary'] = timing
    report['analysis_sha256'] = sha(__file__)
    report['preview'] = 'Same fixed 1000-nit scale mapped to SDR; not an HDR screenshot or display measurement.'
    report['limitations'] = ['Stock native pixel programs and texture; controlled geometry/tint, not a captured in-game spell.',
        'Proven single-texture shaders; SM3 fog tested at 0/0.25/0.5/1 with black fog color; not every native shader variant.',
        'One batched draw of 64 sprites through the production replay callback and Color; GPU timestamp intervals may include CPU submission stalls and exclude the game and presentation.',
        'Visuals use fixed exposure 0 EV, 203-nit white and 1000-nit peak; timings also exercise auto exposure.',
        'Source alpha is clamped; RGB-only particle writes preserve opaque scene alpha.',
        'The transfer extends the existing sRGB formula; it does not establish physically calibrated radiance.']
    font_path = Path('C:/Windows/Fonts/segoeui.ttf')
    font = ImageFont.truetype(str(font_path), 24)
    small = ImageFont.truetype(str(font_path), 18)
    canvas = Image.new('RGB', (1920, 1280), '#111820')
    draw = ImageDraw.Draw(canvas)
    draw.text((24, 12), 'Native WoW Mod2x + GenericGlow5 | controlled additive particles | DXVK 3.1.1', font=font, fill='white')
    draw.text((24, 48), 'Same exposure / tone map. SDR preview on a fixed 1000-nit scale; raw scRGB outputs saved separately.', font=small, fill='#b8c7d3')
    for row, bloom in enumerate((0, 1)):
        for col, variant in enumerate(('baseline', 'candidate')):
            pixels = images['dxvk3', f'sm3-msaa0-{variant}-bloom{bloom}-output.rgba16f'][:, :, :3]
            linear = np.clip(pixels/(1000/80), 0, 1)
            srgb = np.where(linear <= .0031308, linear*12.92, 1.055*np.power(linear, 1/2.4)-.055)
            picture = Image.fromarray(np.uint8(np.rint(srgb*255)))
            top = 120+row*584
            canvas.paste(picture, (col*960, top))
            label = f'{"BASELINE: RGB clamped" if col == 0 else "PARTICLES: RGB > 1 retained"} | bloom {bloom}'
            draw.rectangle((col*960, top-32, col*960+959, top), fill='#18212b')
            draw.text((col*960+20, top-30), label, font=small, fill='white')
    canvas.save(root/'comparison.png')
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    for row in timing:
        print(row['backend'], 'SM'+str(row['major']), row['width'], 'post='+str(row['with_post']),
              f"{row['baseline_ms']:.6f} -> {row['candidate_ms']:.6f} ms", 'paired 95% interval:', row['paired_median_bootstrap_95_ms'])


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dxvk2', type=Path)
    parser.add_argument('--dxvk3', type=Path)
    parser.add_argument('--analyze-only', action='store_true')
    args = parser.parse_args()
    root = args.output.resolve()
    if root.is_relative_to(fp.client_root().resolve()):
        parser.error('Use an isolated directory outside the game client.')
    if args.analyze_only:
        analyze(root, json.loads((root/'validation.json').read_text()))
        return
    processes()
    root.mkdir(parents=True, exist_ok=True)
    include = root/'headers'
    include.mkdir(exist_ok=True)
    fp.use_source_modules()
    from world_scene_builder import Assets, decode_blp
    from extract_signatures import variants
    from shader_compiler import Compiler
    report = dict(game_launched=False, client_modified=False, production_particle_path_enabled=True, runs=[], sources={}, native_shaders=[])
    for name in ('hdr_frame.h', 'hdr_quad.h', 'hdr_color.h', 'hdr_ui_patch.h', 'hdr_presentation.h', 'saved_state.h', 'hdr_compiled_shaders.h', 'spell_layer.h'):
        source = fp.src(name)
        shutil.copy2(source, include/name)
        report['sources'][str(source.relative_to(fp.REPO))] = sha(source)
    for name in ('hdr_particle_gpu.cpp', 'hdr_particle_transfer.hlsl', 'verify_hdr_particle_gpu.py'):
        report['sources']['tests/'+name] = sha(fp.TESTS/name)
    # Compile the actual renderer callback against a small owner of real D3D9
    # resources, so its HDR/early-drain/family decisions are exercised offline.
    renderer = fp.src('renderer.cpp').read_text()
    begin = renderer.index('    static HRESULT spellProjectedDraw(')
    end = renderer.index('    void pollProjectedAudit()', begin)
    (include/'particle_replay_callback.h').write_text(renderer[begin:end], encoding='utf-8')
    report['sources']['src/proxy/renderer.cpp'] = sha(fp.src('renderer.cpp'))
    for name in ('particle_light_proof.h', 'm2_shader_families.h', 'spell_layer.h'):
        report['sources'][str(fp.src(name).relative_to(fp.REPO))] = sha(fp.src(name))
    compiler = Compiler(allow_compatible_binary=True)
    report['compiler'] = compiler.provenance
    chunks = ['#pragma once\n#include <cstdint>\nstruct NativeShader {const char* name; const std::uint32_t* words; std::size_t count;};\n']

    def array(name, code):
        words = struct.unpack('<%dI' % (len(code)//4), code)
        chunks.append('static const std::uint32_t '+name+'[]={'+','.join(f'0x{x:08x}' for x in words)+'};\n')

    for major in (2, 3):
        report[f'transfer_sm{major}'] = json.loads((fp.SHADERS/'hdr-shader-build.json').read_text())['shaders'][f'HdrParticle-ps_{major}_0']
        code, _ = compiler.compile(fp.TESTS/'hdr_particle_transfer.hlsl', 'ParticleFixtureVS', f'vs_{major}_0', root/f'fixture-vs{major}.bin')
        array(f'kFixtureVs{major}', code)
    assets = Assets(archives='stock')
    entries = []
    try:
        for major in (2, 3):
            for combiner in ('Mod', 'Opaque', 'Mod2x'):
                path = f'shaders\\pixel\\ps_{major}_0\\Combiners_{combiner}.bls'
                for index, (_, _, code) in enumerate(variants(assets.read(path))):
                    symbol = f'native_{len(entries)}'
                    name = f'sm{major}_{combiner}_{index}'
                    array(symbol, code)
                    entries.append('{"'+name+'",'+symbol+',sizeof('+symbol+')/4}')
                    report['native_shaders'].append(dict(name=name, path=path, index=index, sha256=hashlib.sha256(code).hexdigest()))
        texture_path = 'particles\\genericglow5.blp'
        blp = assets.read(texture_path)
        w, h, rgba = decode_blp(blp, maximum=256)
        bgra = bytearray(rgba)
        for i in range(0, len(bgra), 4):
            bgra[i], bgra[i+2] = bgra[i+2], bgra[i]
        texture = root/'native-glow.fgr'
        texture.write_bytes(struct.pack('<5I', 0x31524746, w, h, 21, 4)+bgra)
        report['texture'] = dict(path=texture_path, source_sha256=hashlib.sha256(blp).hexdigest(), width=w, height=h, rgba8_max=max(rgba))
    finally:
        assets.close()
    chunks.append('static const NativeShader kNative[]={'+','.join(entries)+'};\n')
    (include/'particle_experiment_shaders.h').write_text(''.join(chunks), encoding='utf-8')
    binary = root/'hdr_particle_gpu.exe'
    environment = fp.zig_env()
    for key, folder in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
        (root/folder).mkdir(exist_ok=True)
        environment[key] = str(root/folder)
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti',
                    '-I', str(include), *fp.include_flags(), str(fp.TESTS/'hdr_particle_gpu.cpp'), '-luser32', '-o', str(binary)], env=environment, check=True)
    report['executable_sha256'] = sha(binary)
    for backend, dll in [('dxvk2', args.dxvk2 or fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),
                         ('dxvk3', args.dxvk3 or fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
        processes()
        folder = root/backend
        folder.mkdir(exist_ok=True)
        runtime = folder/'backend_d3d9.dll'
        shutil.copy2(dll, runtime)
        result = subprocess.run([str(binary), str(runtime), str(texture)], cwd=folder,
                                env=dict(os.environ, DXVK_LOG_PATH=str(folder)), capture_output=True, text=True, timeout=300)
        (folder/'stdout.log').write_text(result.stdout, encoding='utf-8')
        (folder/'stderr.log').write_text(result.stderr, encoding='utf-8')
        records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        report['runs'].append(dict(backend=backend, backend_sha256=sha(runtime), exit_code=result.returncode, records=records))
        (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(backend, 'exit', result.returncode, 'records', len(records), flush=True)
        if result.returncode or not records or records[-1]['kind'] != 'pass':
            raise SystemExit(result.stderr or result.stdout)
    analyze(root, report)
    print('PASS:', root/'validation.json')


if __name__ == '__main__':
    main()
