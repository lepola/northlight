#!/usr/bin/env python3
# northlight-test: requires=zig manual
"""Actual authored producer -> official trace -> dedicated Northlight import host.

This manual gate compiles only the transparent test proxy and authored EXE.
It never runs the data-only client PE, records original game quality or measures
scene FPS. The existing six-case Reset gate remains separate and unchanged.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS))
import compile_native_replay_plan as plan

spec = importlib.util.spec_from_file_location('official_trace_fixture', fp.TESTS / 'validate_native_scene_trace.py')
trace_fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace_fixture)

BACKEND_SHA256 = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'


def main():
    parser = argparse.ArgumentParser(__doc__)
    for option in ('tool-root', 'backend-dll', 'stand-dll', 'host', 'client', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--reset-resize', action='store_true',
                        help='Separate six-frame client-resize-before-Reset discriminator; original failure remains preserved')
    args = parser.parse_args()
    if os.name != 'nt':
        raise ValueError('The positive transport requires actual Windows D3D9; no substitute backend')
    out = args.output.resolve()
    if out.exists():
        raise ValueError('Output must be new; preserve every prior failure')
    tools = plan.tool_runtime(args.tool_root)
    source, original = fp.TESTS / 'native_retrace_transport_smoke.cpp', fp.TESTS / 'native_scene_trace_smoke.cpp'
    host, client, backend, stand = (p.resolve() for p in (args.host, args.client, args.backend_dll, args.stand_dll))
    if host.name.lower() != 'northlight-native-retrace.exe':
        raise ValueError('A distinctly named real import host is required')
    for path in (host, client, backend, stand):
        plan.pe32(path)
    if plan.digest(backend) != BACKEND_SHA256:
        raise ValueError('This authored gate is pinned to the exact private DXVK3.1.1 member')
    if b'NORTHLIGHT STAND REPLAY BUILD:' not in stand.read_bytes():
        raise ValueError('The actual dedicated import-only Northlight DLL is required')
    compiler = fp.zig()
    watched = [Path(__file__).resolve(), source, original, fp.TESTS / 'validate_native_scene_trace.py',
               fp.SCRIPTS / 'compile_native_replay_plan.py', fp.SCRIPTS / 'prepare_native_scene.py',
               fp.SCRIPTS / 'prepare_native_retrace_host.py', host, client, backend, stand, compiler]
    watched += [fp.src(name) for name in ('native_snapshot_capture.h', 'snapshot_capture_sink.h', 'snapshot_archive.h',
                'snapshot_store.h', 'game_snapshot.h', 'forwarders.h', 'backend_loader.h', 'stand_inputs.h',
                'native_retrace_bridge.h', 'native_retrace_bridge.cpp', 'celestial_disc_native.h', 'celestial_glare_native.h')]
    runtime = [p.resolve() for p in host.parent.glob('*.dll')]
    watched += runtime
    inputs = {str(p.resolve()): plan.digest(p) for p in watched}
    inputs.update(tools['files'])
    if any(Path(p) == out or Path(p).is_relative_to(out) for p in inputs):
        raise ValueError('Output must not contain any frozen input')
    out.mkdir(parents=True)
    report = {'format': 1, 'output': str(out), 'inputs': inputs, 'commands': [], 'pass': False,
              'authored_transport_only': True, 'original_game_producer': False, 'game_launched': False,
              'full_scene_ready': False, 'original_quality_parity_validated': False,
              'raw_depth_equivalence_validated': False, 'msaa': 0, 'fps_eligible': False,
              'timings_are_performance_evidence': False, 'original_reset_gate_still_failed': True,
              'reset_client_resize_discriminator': args.reset_resize}

    def copy(source, target):
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if plan.digest(source) != plan.digest(target):
            raise ValueError('Private staged artifact differs')

    def private_env(folder):
        (folder / 'temp').mkdir(exist_ok=True)
        (folder / 'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
        env = dict(os.environ)
        for name in ('TRACE_FILE', 'APITRACE_FORCE_MODULE_PATH', 'NORTHLIGHT_RETRACE_PLAN', 'NORTHLIGHT_RETRACE_ALLOW_AUTHORED'):
            env.pop(name, None)
        env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(folder),
                   DXVK_CONFIG_FILE=str(folder / 'dxvk.conf'), DXVK_SHADER_CACHE_PATH=str(folder / 'shader-cache'),
                   DXVK_STATE_CACHE_PATH=str(folder / 'state-cache'), TEMP=str(folder / 'temp'), TMP=str(folder / 'temp'))
        return env

    try:
        report['processes_initial'] = trace_fixture.processes()
        build, capture, replay = (out / name for name in ('build', 'capture', 'replay'))
        for directory in (build, capture, replay):
            directory.mkdir()
        compile_env = fp.zig_env()
        (build / 'temp').mkdir()
        compile_env.update(ZIG_GLOBAL_CACHE_DIR=str(build / 'zig-global-cache'),
                           ZIG_LOCAL_CACHE_DIR=str(build / 'zig-local-cache'), TEMP=str(build / 'temp'), TMP=str(build / 'temp'))
        common = [compiler, 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', *fp.include_flags()]
        exe, proxy = build / 'native_retrace_transport_smoke.exe', build / 'transport-proxy.dll'
        definition = build / 'transport-proxy.def'
        definition.write_text('LIBRARY transport_proxy\nEXPORTS\n Direct3DCreate9=Direct3DCreate9@4\n D3DPERF_SetMarker=D3DPERF_SetMarker@8\n')
        trace_fixture.run([*common, source, '-luser32', '-o', exe], out, compile_env, 'compile-producer', report)
        trace_fixture.run([*common, '-shared', '-DNORTHLIGHT_TRANSPORT_PROXY_DLL=1', source, definition,
                           '-luser32', '-luuid', '-o', proxy], out, compile_env, 'compile-proxy', report)
        report['built_artifacts'] = {str(p): plan.digest(p) for p in (exe, proxy, definition)}
        downstream = capture / 'downstream'
        copy(exe, capture / exe.name)
        copy(proxy, downstream / 'd3d9.dll')
        copy(Path(tools['root']) / 'lib/wrappers/d3d9.dll', capture / 'd3d9.dll')
        staged_backend = capture / 'native_scene_backend.dll'
        copy(backend, staged_backend)
        trace = capture / 'source.trace'
        capture_env = private_env(capture)
        capture_env.update(TRACE_FILE=str(trace), APITRACE_FORCE_MODULE_PATH=str(downstream),
                           NORTHLIGHT_TRANSPORT_BACKEND=str(staged_backend),
                           NORTHLIGHT_TRANSPORT_CLIENT_PE=str(client),
                           NORTHLIGHT_TRANSPORT_SNAPSHOT_DIR=str(capture / 'snapshots'))
        source_command = [capture / exe.name, capture]
        if args.reset_resize:
            source_command.append('reset-resize')
        trace_fixture.run(source_command, out, capture_env, 'producer', report)
        source_report = json.loads((capture / 'authored-source.json').read_text())
        frames = 6 if args.reset_resize else 3
        if (not source_report.get('authored_transport') or source_report.get('frames') != frames or
                source_report.get('draws') != frames * 4 or source_report.get('nonzero_occlusions') != frames or
                source_report.get('event_completions') != frames or source_report.get('fps_eligible') is not False):
            raise ValueError('Actual authored producer did not complete all positive controls')
        producer_modules = report['commands'][-1]['observed_module_sha256']
        required = (inputs[str(backend)], report['built_artifacts'][str(proxy)],
                    plan.digest(Path(tools['root']) / 'lib/wrappers/d3d9.dll'))
        if not all(value in producer_modules.values() for value in required):
            raise ValueError('Actual producer module observation is incomplete')
        if any('graphics-hook32.dll' in p.lower() for p in producer_modules):
            raise ValueError('OBS hook was observed despite the explicit private disable')
        source_images = [trace_fixture.pnm_images((capture / f'frame{i:06d}.ppm').read_bytes())[0] for i in range(frames)]
        stand_dir = replay / 'stand'
        staged_stand = stand_dir / 'northlight-stand.dll'
        copy(stand, staged_stand)
        copy(backend, stand_dir / 'native_scene_backend.dll')
        fixture_renderer = '[Renderer]\nBackend=dxvk\nBackendPath=native_scene_backend.dll\nExperimentalHDR=0\n'
        (stand_dir / 'northlight-renderer.ini').write_text(fixture_renderer)
        # Explicit authored-transport isolation, never a copied or edited player
        # INI and never a claim about original rendering quality or workload.
        fixture_quality = ('[Quality]\nCommandStream=1\nStreamFramesAhead=1\nStreamFrameSkip=0\nStreamOcclusionLocal=0\n'
                           'Diagnostics=0\nRenderProfile=0\nGI=0\nActorShadows=0\nContactAO=0\nWeather=0\nAOQuality=High\n')
        (stand_dir / 'northlight-quality.ini').write_text(fixture_quality)
        report['private_fixture_quality'] = fixture_quality
        report['private_fixture_renderer'] = fixture_renderer
        plan_dir = out / 'plan'
        trace_fixture.run([sys.executable, fp.SCRIPTS / 'compile_native_replay_plan.py', '--trace', trace,
                           '--snapshot-dir', capture / 'snapshots', '--client', client, '--stand-dll', staged_stand,
                           '--tool-root', args.tool_root.resolve(), '--output', plan_dir, '--allow-authored-transport'],
                          out, private_env(replay), 'compile-plan', report)
        compiled = json.loads((plan_dir / 'validation.json').read_text())
        if (compiled.get('pass') is not True or compiled.get('inputs_unchanged') is not True or
                not compiled.get('authored_transport_only') or not compiled.get('typed_resource_aliases_required') or
                compiled['summary']['draws'] != frames * 4 or compiled['summary']['presents'] != frames):
            raise ValueError('Plan did not prove actual markers, packets, source draws and nonzero typed aliases')
        report['compiled_plan'] = compiled
        records = list(plan.calls(plan_dir / 'trace.dump'))
        for api in plan.DRAW_APIS:
            if sum(row['name'] == 'IDirect3DDevice9::' + api for row in records) != frames:
                raise ValueError('All four actual Draw APIs must be present once per frame')
        presents = [row['call'] for row in records if row['name'] == 'IDirect3DDevice9::Present']
        # The upper sentinel selects no API call: it prevents official -S from
        # exiting at the final snapshot before the real final Release/cleanup.
        selection = ','.join(map(str, [*presents, 0xffffffff]))
        copy(host, replay / host.name)
        for dependency in runtime:
            copy(dependency, replay / dependency.name)
        for dependency in tools['retrace_dependencies']:
            target = replay / Path(dependency).name
            if target.exists() and plan.digest(target) != plan.digest(Path(dependency)):
                raise ValueError('Private host and official runtime dependency collide')
            copy(Path(dependency), target)
        replay_env = private_env(replay)
        replay_env.update(NORTHLIGHT_RETRACE_PLAN=str(plan_dir / 'scene.nljoin'), NORTHLIGHT_RETRACE_ALLOW_AUTHORED='1')
        data = trace_fixture.run([replay / host.name, '--headless', '--query-handling=run', '-s', '-', '-S', selection, trace],
                                 out, replay_env, 'replay', report, binary=True)
        stderr = (out / 'replay.stderr').read_text(encoding='utf-8', errors='replace')
        passed = re.search(r'NORTHLIGHT_NATIVE_BRIDGE PASS records=(\d+) draws=(\d+) presents=(\d+) resets=(\d+) queryCalls=(\d+) queryPolls=(\d+) queryEarlyReady=(\d+) authored_transport=1 fps_eligible=0', stderr)
        if not passed or tuple(map(int, passed.groups()[:4])) != (frames, frames * 4, frames, int(args.reset_resize)) or not all(int(v) > 0 for v in passed.groups()[4:6]):
            raise ValueError('Actual host failed marker/query/stand/final-release transport acceptance')
        actual = trace_fixture.pnm_images(data)
        if actual != source_images:
            raise ValueError('Complete authored capture/replay RGB sequence differs; no tolerance relaxation')
        replay_modules = report['commands'][-1]['observed_module_sha256']
        if not all(value in replay_modules.values() for value in (inputs[str(host)], inputs[str(backend)], inputs[str(stand)])):
            raise ValueError('Actual host/stand/backend module observation is incomplete')
        if any('graphics-hook32.dll' in p.lower() for p in replay_modules):
            raise ValueError('OBS hook was observed in the actual replay')
        for i, (width, height, rgb) in enumerate(actual):
            (replay / f'frame{i:06d}.ppm').write_bytes(f'P6\n{width} {height}\n255\n'.encode() + rgb)
        report.update(pass_=True, frames=frames, actual_nonzero_typed_aliases=True,
                      exact_rgb_sequence=True, real_query_callbacks=True,
                      snapshot_selection=selection, sentinel_is_observer_only=True,
                      bridge_counts=dict(zip(('records', 'draws', 'presents', 'resets', 'queryCalls', 'queryPolls', 'queryEarlyReady'), map(int, passed.groups()))))
        report['pass'] = report.pop('pass_')
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        report['inputs_unchanged'] = all(Path(path).is_file() and plan.digest(Path(path)) == value for path, value in inputs.items())
        try:
            report['processes_final'] = trace_fixture.processes()
        except Exception as error:
            report['process_guard_failure'] = str(error)
            report['pass'] = False
        if not report['inputs_unchanged']:
            report['pass'] = False
        report['artifacts'] = {str(p.relative_to(out)): plan.digest(p) for p in out.rglob('*')
                               if p.is_file() and p != out / 'validation.json'}
        (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    if not report['pass']:
        raise ValueError('The authored transport gate did not pass')
    print('PASS actual authored transport; original game scene, quality, raw-depth/MSAA and FPS remain unproven')


if __name__ == '__main__':
    main()
