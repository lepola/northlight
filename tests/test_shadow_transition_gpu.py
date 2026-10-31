# northlight-test: requires=windows-gpu,zig
"""Compile and replay directional-cache fade variants on a hidden D3D9 device."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import subprocess
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'scripts/shaders')]
import northlight_paths as fp


def entry_body(source, name):
    start = source.index(name + '(float2 uv:TEXCOORD0)')
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def check_shared_math(baseline, variant):
    """Keep the copied entry bodies synchronized with all unrelated baseline math."""
    for name in ('WorldLighting', 'WorldFog', 'FogClouds'):
        expected = entry_body(baseline, name).replace(name + '(', name + 'Fade(', 1)
        expected = expected.replace('directionalShadow(', 'directionalShadowFade(')
        if name != 'WorldLighting':
            line = '    float3 shadowRay=ray.x*FarMatrix[0].xyz+ray.y*FarMatrix[1].xyz+ray.z*FarMatrix[2].xyz;'
            expected = expected.replace(line, line + '\n    float3 previousOrigin=affinePoint(Camera.xyz,PreviousFarMatrix);\n    float3 previousRay=ray.x*PreviousFarMatrix[0].xyz+ray.y*PreviousFarMatrix[1].xyz+ray.z*PreviousFarMatrix[2].xyz;')
            expected = expected.replace('fogShadowProjected(shadowOrigin+shadowRay*((start+end)*.5))', 'fogShadowFade(shadowOrigin+shadowRay*((start+end)*.5),previousOrigin+previousRay*((start+end)*.5))')
            expected = expected.replace('dot(ray,SunDirection.xyz)', 'dot(ray,lerp(PreviousFarDirection.xyz,CurrentFarDirection.xyz,ShadowTransition.y))')
        assert entry_body(variant, name + 'Fade') == expected, name + ': unrelated entry math diverged'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, help='private artifact directory')
    parser.add_argument('--backend', type=Path, help='explicit 32-bit D3D9 DLL; default native system DLL')
    parser.add_argument('--compile-only', action='store_true', help='build and validate bytecode without creating a GPU device')
    parser.add_argument('--reuse-binaries', action='store_true', help='replay matching existing build on another backend')
    parser.add_argument('--benchmark', action='store_true', help='paired native half-resolution timestamps')
    parser.add_argument('--compatible-compiler', action='store_true')
    args = parser.parse_args()
    out = (args.out or fp.output_dir()).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Isolate native compiler temporaries and the Zig cache from parallel builds.
    tmp = out / 'tmp'
    tmp.mkdir(exist_ok=True)
    os.environ.update(TEMP=str(tmp), TMP=str(tmp), ZIG_GLOBAL_CACHE_DIR=str(out / 'zig-cache'), NORTHLIGHT_OUT=str(out))
    source = ROOT / 'shaders/shadow_transition.hlsl'
    baseline = ROOT / 'shaders/world_effects.hlsl'
    check_shared_math(baseline.read_text(), source.read_text())
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = {p.name: digest(p) for p in (source, baseline)}
    geometry_hashes = {name: digest(fp.src(name)) for name in ('world_math.h', 'shadow_transition_math.h', 'world_gi.cpp')}
    exe = out / 'shadow-transition-gpu.exe'
    if args.reuse_binaries:
        from shader_compiler import DLL_SHA256, COMPATIBLE_DLL_SHA256
        manifest = json.loads((out / 'shader-build.json').read_text())
        assert manifest['geometry_sources'] == geometry_hashes
        assert manifest['sources'] == hashes and manifest['oracle_cpp_sha256'] == digest(ROOT / 'tests/shadow_transition_gpu.cpp'), 'stale oracle build'
        allowed = {DLL_SHA256, COMPATIBLE_DLL_SHA256} if args.compatible_compiler else {DLL_SHA256}
        assert manifest['compiler_sha256'] in allowed, 'compiler hash requires explicit compatible allowance'
        assert all(digest(out / (name + '.bin')) == stats['sha256'] for name, stats in manifest['shaders'].items())
    else:
        from shader_compiler import Compiler, validate_assembly
        compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
        shim = out / 'shadow-transition-oracle.hlsl'
        shim.write_text(source.read_text().replace('#include "world_effects.hlsl"', '#include "' + baseline.as_posix() + '"') + '''
    float4 TestInfo : register(c102);
    float4 TestNormal : register(c103);
    float4 TestCenter : register(c104);
    float4 DirectVisibility(float2 uv:TEXCOORD0):COLOR0 {
        float3 p=float3((uv-.5)*TestInfo.x,TestInfo.y)+TestCenter.xyz;
        return directionalShadowFade(p,TestNormal.xyz,TestInfo.z).xxxx;
    }
    float4 BaselineVisibility(float2 uv:TEXCOORD0):COLOR0 {
        float3 p=float3((uv-.5)*TestInfo.x,TestInfo.y)+TestCenter.xyz;
        return directionalShadow(p,TestNormal.xyz,TestInfo.z).xxxx;
    }
    float4 FogVisibility(float2 uv:TEXCOORD0):COLOR0 {
        float3 p=float3((uv-.5)*TestInfo.x,TestInfo.y)+TestCenter.xyz;
        return fogShadowFade(affinePoint(p,FarMatrix),affinePoint(p,PreviousFarMatrix)).xxxx;
    }
    ''')
        stats = {}
        for name in ('WorldLighting', 'WorldFog', 'FogClouds', 'WorldLightingFade', 'WorldFogFade', 'FogCloudsFade', 'DirectVisibility', 'FogVisibility', 'BaselineVisibility', 'ShadowUnion'):
            src = shim if name.endswith('Visibility') else source if name.endswith('Fade') else baseline
            output = out / (name + '.bin')
            compiler.compile(src, name, 'ps_3_0', output)
            stats[name] = {**validate_assembly(Path(str(output) + '.asm'), 'ps_3_0'), 'sha256': digest(output)}
        assert hashes == {p.name: digest(p) for p in (source, baseline)}, 'shader source changed while compiling'
        manifest = {'sources': hashes, **compiler.provenance, 'shaders': stats, 'game_launched': False, 'oracle_cpp_sha256': digest(ROOT / 'tests/shadow_transition_gpu.cpp'), 'geometry_sources': geometry_hashes}
        (out / 'shader-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
        exe = out / 'shadow-transition-gpu.exe'
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        '-Wno-return-type', *fp.include_flags(), str(ROOT / 'tests/shadow_transition_gpu.cpp'), str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)],
                       env=fp.zig_env(), check=True)
    if args.compile_only:
        print('PASS compiled SM3 bytecode and oracle:', out)
        return
    selected = args.backend or os.environ.get('NORTHLIGHT_TEST_D3D9')
    if selected:
        backend = Path(selected).resolve()
    else:
        directory = ctypes.create_unicode_buffer(32768)
        assert ctypes.windll.kernel32.GetSystemDirectoryW(directory, len(directory))
        system = Path(directory.value)
        wow64 = system.parent / 'SysWOW64/d3d9.dll'
        backend = wow64 if wow64.is_file() else system / 'd3d9.dll'
    assert backend.is_absolute() and backend.is_file()
    command = [str(exe), str(out), str(backend), str(out / 'gpu.json')]
    if args.benchmark:
        command.append('--benchmark')
    subprocess.run(command, cwd=out, check=True, timeout=240)
    report = json.loads((out / 'gpu.json').read_text())
    assert report['reference_checks'] >= 884736 and report['whole_checks'] >= 36000
    assert report['thin_gap_max_change'] > .01 and report['moving_shadow_contrast'] > .01
    assert report['geometry_3d_checks'] >= 884736 and report['geometry_3d_endpoints'] >= 49152
    assert report['geometry_3d_min_margin'] >= .001 and report['gpu_tie_max_error'] <= 3e-5
    assert report['crop_checks'] == 7 * 1024 * 1024 and report['crop_max_error'] == 0
    assert report['failures'] == 0
    report['provenance'] = {**manifest, 'backend': str(backend), 'backend_sha256': digest(backend)}
    report['benchmark_measured'] = args.benchmark
    report['cold_measurement_scope'] = 'First use in this process with existing driver disk caches; first-submit and GPU-span include driver/JIT scheduling. Resource first-clear is a warmup probe, not an additional production pass.'
    if not args.benchmark:
        for key in report:
            if key.endswith('_ms'):
                report[key] = None
    visual_dir = out / ('visuals-' + backend.stem)
    visual_dir.mkdir(exist_ok=True)
    report['visuals'] = []
    for name in ('WorldLighting', 'WorldFog', 'FogClouds'):
        image = visual_dir / (name + '.panel.ppm')
        shutil.copyfile(out / image.name, image)
        report['visuals'].append({'pass': name, 'file': str(image), 'width': 1040, 'height': 96,
                                 'columns': ['baseline old', 'fade 0', 'fade .25', 'fade .5', 'fade .75', 'fade 1', 'baseline new', 'current moving caster'],
                                 'display': 'sqrt visibility alpha' if name == 'WorldLighting' else 'sqrt clamped RGB times4'})
    text = json.dumps(report, indent=2) + '\n'
    (out / 'gpu.json').write_text(text)
    (out / (backend.stem + '.gpu.json')).write_text(text)
    print('PASS independent scalar filters, production endpoints, moving caster, ambient and transmittance; report:', out / 'gpu.json')


if __name__ == '__main__':
    main()
