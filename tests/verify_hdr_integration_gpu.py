"""Build and run an isolated x86 GPU oracle; never launches or installs a game."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'scripts/shaders')]
import northlight_paths as fp
from compile_hdr_shaders import hdr_compiler


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ('dxvk2', 'dxvk3', 'captured-ui', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--prebuilt', type=Path, help='Use the supplied x86 oracle and existing compiled shaders, without a compiler.')
    parser.add_argument('--modes', nargs='+', default=['raw', 'hdr', 'hdr-small', 'hdr-triple', 'hdr-graded', 'sdr', 'stream-off', 'hdr-msaa2', 'hdr-msaa4', 'hdr-msaa2-triple', 'hdr-msaa4-graded', 'hdr-msaa2-4k'])
    args = parser.parse_args()
    output = args.output.resolve()
    # Keep every DLL/config/log in dedicated test directories, including reruns.
    for folder in (output, *(output / name for name in ('dxvk2', 'dxvk3'))):
        if fp.is_client(folder):
            raise SystemExit('Refusing to use a game client as the GPU test directory.')
    output.mkdir(parents=True, exist_ok=True)
    env = fp.zig_env()
    harness = output / 'hdr_integration_gpu.exe'
    shaders=output/'shaders';shaders.mkdir(exist_ok=True)
    for name in ('HdrWorld','HdrUi-ps_2_0','HdrUi-ps_3_0'):
        shutil.copy2(fp.COMPILED/(name+'.bin'),shaders/(name+'.bin'))
    if args.prebuilt:
        shutil.copy2(args.prebuilt.resolve(), harness)
        for major in (2,3):
            for stage in ('PS','VS'):
                name=f'HdrFixture{stage}-{major}.bin'
                if not (shaders/name).is_file():shutil.copy2(fp.COMPILED/name,shaders/name)
    else:
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                        '-O2', '-fno-rtti', *fp.include_flags(), str(fp.TESTS / 'hdr_integration_gpu.cpp'),
                        '-luser32', '-o', str(harness)], env=env, check=True)
        compiler = hdr_compiler(compatible=True)
        for major in (2, 3):
            for stage in ('PS', 'VS'):
                compiler.compile(fp.TESTS / 'hdr_fixture.hlsl', 'HdrFixture' + stage,
                                 f'{stage.lower()}_{major}_0', shaders / f'HdrFixture{stage}-{major}.bin')
    # Match the game's large-address-aware process for full-resolution readbacks.
    data = bytearray(harness.read_bytes()); flags = struct.unpack_from('<I', data, 0x3c)[0]+22
    struct.pack_into('<H', data, flags, struct.unpack_from('<H', data, flags)[0] | 0x20); harness.write_bytes(data)
    wine = None if os.name == 'nt' else str(fp.wine_root() / 'bin/wine')
    def winpath(path):
        return str(path) if os.name == 'nt' else 'Z:' + str(path)
    report = {'host': platform.platform(), 'wine': wine, 'renderer_sha256': sha(fp.RENDERER / 'frd9.dll'),
              'captured_ui_sha256': sha(args.captured_ui), 'game_launched': False,
              'client_modified': False, 'runs': []}
    for version, dll in (('dxvk2', args.dxvk2.resolve()), ('dxvk3', args.dxvk3.resolve())):
        folder = output / version
        folder.mkdir(exist_ok=True)
        shutil.copy2(harness, folder / harness.name)
        shutil.copy2(fp.RENDERER / 'frd9.dll', folder / 'd3d9.dll')
        backend = 'dxvk2' if version == 'dxvk2' else 'dxvk'
        runtime = folder / 'renderer-backends' / backend / (backend + '_d3d9.dll')
        runtime.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dll, runtime)
        (folder / 'dxvk.conf').write_text('dxvk.allowFse = False\n')
        for mode in args.modes:
            (folder / 'northlight-renderer.ini').write_text(
                f'[Renderer]\nBackend={backend}\nExperimentalHDR={int(mode != "sdr")}\nHDRPaperWhite=203\nHDRUIWhite=160\n'
                + ('HDRContrast=1.35\nHDRExposure=-1.25\n' if mode in ('hdr-graded','hdr-msaa4-graded') else ''))
            (folder / 'northlight-quality.ini').write_text(
                f'[Quality]\nPreset=Performance\nCommandStream={int(mode != "stream-off")}\nStreamFramesAhead=1\nStreamFrameSkip=0\nDiagnostics=0\nRenderProfile=0\n'
                'SpellLayer=0\nEquipmentLights=0\nGI=0\nActorShadows=0\nContactAO=0\nNameMask=0\nWeather=0\n')
            run_env = dict(env, DXVK_LOG_PATH=winpath(folder), DXVK_CONFIG_FILE=winpath(folder / 'dxvk.conf'))
            run_env.pop('DXVK_HDR', None)
            if mode == 'raw':
                run_env['DXVK_HDR'] = '1'
            command = [winpath(folder / harness.name), winpath(dll if mode == 'raw' else folder / 'd3d9.dll'),
                       winpath(shaders), winpath(args.captured_ui.resolve())]
            if wine:
                command.insert(0, wine)
            if mode != 'raw':
                command.append('--'+mode.removeprefix('hdr-')+'-proxy' if mode.startswith('hdr-msaa') else '--proxy' if mode == 'hdr' else '--small-proxy' if mode == 'hdr-small' else '--triple-proxy' if mode == 'hdr-triple' else '--graded-proxy' if mode == 'hdr-graded' else '--sdr-proxy')
            result = subprocess.run(command, cwd=folder, env=run_env, text=True, capture_output=True, timeout=60)
            evidence = {'backend': version, 'mode': mode, 'backend_sha256': sha(dll),
                        'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
            if mode != 'raw':
                log = (folder / 'northlight-renderer.log').read_text(errors='replace')
                evidence['hdr_status'] = [line for line in log.splitlines() if line.startswith('HDR experimental')]
                evidence['reset_status'] = [line for line in log.splitlines() if line.startswith('Reset HRESULT=')]
                shutil.copy2(folder / 'northlight-renderer.log', folder / f'{mode}-northlight.log')
            dxvk_log = (folder / 'hdr_integration_gpu_d3d9.log').read_text(errors='replace')
            evidence['scRGB_presented'] = 'Color space:  VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT' in dxvk_log
            evidence['hdr_presented'] = evidence['scRGB_presented'] or 'Color space:  VK_COLOR_SPACE_HDR10_ST2084_EXT' in dxvk_log
            shutil.copy2(folder / 'hdr_integration_gpu_d3d9.log', folder / f'{mode}-dxvk.log')
            report['runs'].append(evidence)
            (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
            print(version, mode, result.stdout.strip(), flush=True)
            if result.returncode:
                raise SystemExit(result.stderr or result.stdout or f'GPU oracle failed: {result.returncode}')
            if evidence['hdr_presented'] != (mode == 'raw' or mode.startswith('hdr')):
                raise SystemExit('DXVK presentation color space differs from the requested mode.')
            if mode.startswith('hdr') and (not evidence['reset_status'] or any('HDR=1' not in line for line in evidence['reset_status'])):
                raise SystemExit('HDR did not remain active across Reset.')
    print('PASS: ' + str(output / 'validation.json'))


if __name__ == '__main__':
    main()
