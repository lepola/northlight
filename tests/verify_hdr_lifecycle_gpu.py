"""Probe real DXVK monitor transitions and live-resource presentation, without installing a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--dxvk2', type=Path)
    parser.add_argument('--dxvk3', type=Path)
    parser.add_argument('--inventory', action='store_true')
    parser.add_argument('--cycle-hdr', action='store_true', help='Temporarily cycle Windows HDR on display 0; restore its original state in finally.')
    parser.add_argument('--proxy', type=Path, help='Use this Northlight DLL for the Windows HDR cycle, with an isolated config.')
    args = parser.parse_args()
    output = args.output.resolve()
    if fp.is_client(output):
        raise SystemExit('Use an isolated output directory.')
    processes()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / 'hdr_lifecycle_gpu.exe'
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti',
               *fp.include_flags(), str(fp.TESTS / 'hdr_lifecycle_gpu.cpp'), '-luser32', '-ldxgi', '-luuid', '-o', str(binary)]
    subprocess.run(command, check=True, env=fp.zig_env())
    inventory = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=30)
    print(inventory.stdout, end='', flush=True)
    if inventory.returncode:
        raise SystemExit(inventory.stderr or 'Display inventory failed.')
    events = [json.loads(line) for line in inventory.stdout.splitlines() if line.startswith('{')]
    report = dict(inventory=events, runs=[], game_launched=False, client_modified=False,
                  source_sha256=hashlib.sha256((fp.TESTS / 'hdr_lifecycle_gpu.cpp').read_bytes()).hexdigest())
    report_path = output / 'validation.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    if args.inventory:
        return
    def set_hdr(enabled):
        changed = subprocess.run([str(binary), '--set-hdr', '0', 'on' if enabled else 'off'],
                                 capture_output=True, text=True, encoding='utf-8', timeout=15)
        print(changed.stdout, end='', flush=True)
        changed.check_returncode()

    original_hdr = next((e['color_space'] == 12 for e in events if e.get('event') == 'display' and e.get('attached')), False)
    try:
      for version, source in [('dxvk2', args.dxvk2), ('dxvk3', args.dxvk3)]:
        if source is None:
            raise SystemExit('Both --dxvk2 and --dxvk3 are required for the lifecycle matrix.')
        folder = output / version
        folder.mkdir(exist_ok=True)
        case_binary = folder / binary.name
        shutil.copy2(binary, case_binary)
        dll = folder / 'backend_d3d9.dll'
        shutil.copy2(source.resolve(), dll)
        config = folder / 'dxvk.conf'
        config.write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
        if args.proxy:
            if not args.cycle_hdr:
                raise SystemExit('--proxy requires --cycle-hdr.')
            backend = 'dxvk2' if version == 'dxvk2' else 'dxvk'
            runtime = folder / 'renderer-backends' / backend / (backend + '_d3d9.dll')
            runtime.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source.resolve(), runtime)
            shutil.copy2(args.proxy.resolve(), folder / 'd3d9.dll')
            dll = folder / 'd3d9.dll'
            (folder / 'northlight-renderer.ini').write_text(f'[Renderer]\nBackend={backend}\nExperimentalHDR=1\nHDRPipelineVersion=2\nHDRExposureMode=Manual\nHDRBloomStrength=0\n')
            (folder / 'northlight-quality.ini').write_text('[Quality]\nPreset=Performance\nCommandStream=1\nStreamFramesAhead=1\nDiagnostics=0\nRenderProfile=0\nSpellLayer=0\nEquipmentLights=0\nGI=0\nActorShadows=0\nContactAO=0\nNameMask=0\nWeather=0\n')
        env = dict(os.environ, DXVK_LOG_PATH=str(folder), DXVK_CONFIG_FILE=str(config))
        env.pop('DXVK_HDR', None)
        count = sum(e.get('event') == 'display' and e.get('attached') for e in events)
        for start in ([0] if args.cycle_hdr else range(count)):
            for mode in (['proxy-cycle' if args.proxy else 'cycle'] if args.cycle_hdr else ['reset', 'additional']):
                if args.cycle_hdr:
                    set_hdr(False)
                result = subprocess.run([str(case_binary), str(dll), str(start), mode], cwd=folder,
                                        env=env, capture_output=True, text=True, encoding='utf-8', timeout=60)
                run = dict(backend=version, start=start, mode=mode, exit_code=result.returncode,
                           backend_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                           events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')],
                           stdout=result.stdout, stderr=result.stderr)
                log = folder / 'hdr_lifecycle_gpu_d3d9.log'
                if log.exists():
                    archived = folder / f'{start}-{mode}-dxvk.log'
                    shutil.copy2(log, archived)
                    run['dxvk_log'] = str(archived)
                    run['presentation_formats'] = [line for line in log.read_text(errors='replace').splitlines()
                                                   if 'Color space:' in line or 'Format:' in line]
                if args.proxy:
                    renderer_log = folder / 'northlight-renderer.log'
                    run['renderer_sha256'] = hashlib.sha256(dll.read_bytes()).hexdigest()
                    run['hdr_states'] = []
                    if renderer_log.exists():
                        run['hdr_states'] = [int(value) for value in re.findall(r'HDR experimental requested=1 active=(\d)', renderer_log.read_text(errors='replace'))]
                        run['hdr_transitions'] = [value for i, value in enumerate(run['hdr_states'])
                                                  if i == 0 or value != run['hdr_states'][i - 1]]
                        shutil.copy2(renderer_log, folder / f'{start}-{mode}-northlight.log')
                report['runs'].append(run)
                report_path.write_text(json.dumps(report, indent=2) + '\n')
                print(version, start, mode, 'exit=', result.returncode,
                      [e for e in run['events'] if e['event'] in ('reset-probe', 'capability', 'headroom', 'complete')], flush=True)
                if result.returncode:
                    raise SystemExit(result.stderr or result.stdout)
                if args.proxy and run.get('hdr_transitions') != [0, 1, 0, 1, 0]:
                    raise SystemExit(f'Unexpected Northlight HDR lifecycle: {run["hdr_states"]}')
    finally:
        if args.cycle_hdr:
            set_hdr(original_hdr)
            restored = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=30)
            report['restored_inventory'] = [json.loads(line) for line in restored.stdout.splitlines() if line.startswith('{')]
            report['windows_hdr_restored'] = next(e['color_space'] == 12 for e in report['restored_inventory']
                                                   if e.get('event') == 'display' and e.get('attached')) == original_hdr
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            if not report['windows_hdr_restored']:
                raise RuntimeError('Windows HDR state restoration did not match the original inventory.')
    print('Evidence:', report_path)


if __name__ == '__main__':
    main()
