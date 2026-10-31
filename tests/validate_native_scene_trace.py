#!/usr/bin/env python3
# northlight-test: requires=zig manual
"""Validate official x86 D3D9 trace plumbing in new private, hidden stand folders.

This explicitly executes only the authored stand and official tools. It neither
launches the game nor reproduces the game's producer CPU workload/full scene.
All cases are complete recorded sequences; no final-frame looping or fake delay.
"""
import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS))
from prepare_native_scene import digest, pe32, tool_runtime


class ProcessEntry(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD), ('pid', wintypes.DWORD),
                ('heap', ctypes.c_size_t), ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                ('parent', wintypes.DWORD), ('priority', wintypes.LONG), ('flags', wintypes.DWORD),
                ('name', wintypes.WCHAR * 260)]


class ModuleEntry(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('module', wintypes.DWORD), ('pid', wintypes.DWORD),
                ('global_usage', wintypes.DWORD), ('process_usage', wintypes.DWORD),
                ('base', ctypes.c_void_p), ('bytes', wintypes.DWORD), ('handle', wintypes.HMODULE),
                ('name', wintypes.WCHAR * 256), ('path', wintypes.WCHAR * 260)]


def kernel():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    for name, entry in [('Process32FirstW', ProcessEntry), ('Process32NextW', ProcessEntry),
                        ('Module32FirstW', ModuleEntry), ('Module32NextW', ModuleEntry)]:
        getattr(api, name).argtypes = [wintypes.HANDLE, ctypes.POINTER(entry)]
        getattr(api, name).restype = wintypes.BOOL
    return api


def processes():
    api = kernel(); handle = api.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    result = []
    try:
        entry = ProcessEntry(); entry.size = ctypes.sizeof(entry)
        more = api.Process32FirstW(handle, ctypes.byref(entry))
        if not more:
            raise ctypes.WinError(ctypes.get_last_error())
        while more:
            result.append({'pid': entry.pid, 'name': entry.name})
            more = api.Process32NextW(handle, ctypes.byref(entry))
    finally:
        api.CloseHandle(handle)
    if any(p['name'].lower() in ('wow.exe', 'wow64.exe') for p in result):
        raise RuntimeError('Game process exists; defer this stand without changing the game')
    return result


def loaded_modules(pid):
    api = kernel(); handle = api.CreateToolhelp32Snapshot(0x18, pid)
    if handle == ctypes.c_void_p(-1).value:
        return [] # A short process can exit before observation; never infer its modules.
    result = []
    try:
        entry = ModuleEntry(); entry.size = ctypes.sizeof(entry)
        more = api.Module32FirstW(handle, ctypes.byref(entry))
        while more:
            result.append(entry.path)
            more = api.Module32NextW(handle, ctypes.byref(entry))
    finally:
        api.CloseHandle(handle)
    return result


def run(command, folder, env, name, report, binary=False):
    before = processes(); start = time.monotonic_ns(); observed = set(); done = threading.Event(); timed_out = False
    child = subprocess.Popen([str(c) for c in command], cwd=folder, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             creationflags=subprocess.CREATE_NO_WINDOW)

    def observe():
        while not done.is_set():
            observed.update(loaded_modules(child.pid)); done.wait(.01)

    watcher = threading.Thread(target=observe); watcher.start()
    try:
        stdout, stderr = child.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        child.kill(); stdout, stderr = child.communicate()
        timed_out = True
    finally:
        done.set(); watcher.join()
        after = processes()
    (folder / (name + '.stdout')).write_bytes(stdout)
    (folder / (name + '.stderr')).write_bytes(stderr)
    record = {'name': name, 'command': [str(c) for c in command], 'cwd': str(folder),
              'exit_code': child.returncode, 'wall_ns_observation_only': time.monotonic_ns() - start,
              'timed_out': timed_out,
              'stdout': str(folder / (name + '.stdout')), 'stderr': str(folder / (name + '.stderr')),
              'processes_before': before, 'processes_after': after,
              'observed_module_sha256': {p: digest(p) for p in sorted(observed) if Path(p).is_file()},
              'module_observation_complete': False}
    report['commands'].append(record)
    (Path(report['output']) / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    if timed_out or child.returncode:
        raise RuntimeError(f'{name} failed ({child.returncode}); preserved stdout/stderr')
    return stdout if binary else stdout.decode('utf-8', errors='replace')


def pnm_images(data):
    images, at = [], 0

    def token():
        nonlocal at
        while at < len(data):
            if data[at] in b' \t\r\n':
                at += 1
            elif data[at] == 35:
                stop = data.find(b'\n', at)
                if stop < 0:
                    raise ValueError('Unterminated PNM comment')
                at = stop + 1
            else:
                break
        start = at
        while at < len(data) and data[at] not in b' \t\r\n':
            at += 1
        if start == at:
            raise ValueError('Truncated PNM header')
        return data[start:at]

    while at < len(data):
        if token() != b'P6':
            raise ValueError('Official replay output is not a P6 RGB image sequence')
        width, height, maximum = int(token()), int(token()), int(token())
        if not (0 < width <= 16384 and 0 < height <= 16384 and maximum == 255):
            raise ValueError('Invalid PNM dimensions/channel range')
        if data[at:at + 2] == b'\r\n':
            at += 2
        else:
            if data[at:at + 1] not in (b' ', b'\t', b'\r', b'\n'):
                raise ValueError('Missing PNM raster separator')
            at += 1
        size = width * height * 3
        if len(data) - at < size:
            raise ValueError('Truncated PNM raster')
        images.append((width, height, data[at:at + size])); at += size
        while at < len(data) and data[at] in b' \t\r\n':
            at += 1
    return images


def coverage(text):
    rows = []
    for line in text.splitlines():
        match = re.match(r'^(\d+)\s+@([0-9a-fA-F]+)\s+(\w+(?:::\w+)?)\((.*)$', line)
        if match:
            rows.append({'call': int(match[1]), 'thread': int(match[2], 16),
                         'method': match[3], 'arguments_and_result': match[4]})
        elif line.strip():
            # The official dumper renders shader-disassembly strings over
            # several lines. Keep their complete text on the owning call.
            if not rows or re.match(r'^\d+\s+@', line):
                raise ValueError('Unexpected official trace-dump record: ' + line[:160])
            rows[-1]['arguments_and_result'] += '\n' + line
    if not rows or len({r['call'] for r in rows}) != len(rows):
        raise ValueError('Empty or duplicate call-number trace')
    if any(' // incomplete' in r['arguments_and_result'] for r in rows):
        raise ValueError('Trace contains an incomplete call')
    counts = Counter(r['method'].split('::')[-1] for r in rows)
    required = {'CreateDevice': 1, 'CreateTexture': 2, 'CreateVertexBuffer': 2, 'CreateIndexBuffer': 2,
                'CreateVertexDeclaration': 2, 'CreateVertexShader': 2, 'CreatePixelShader': 2,
                'CreateRenderTarget': 2, 'CreateDepthStencilSurface': 2, 'CreateQuery': 4,
                'LockRect': 6, 'UnlockRect': 6, 'Lock': 4, 'Unlock': 4,
                'DrawPrimitive': 6, 'DrawIndexedPrimitive': 6, 'DrawPrimitiveUP': 6,
                'DrawIndexedPrimitiveUP': 6, 'Issue': 18, 'GetData': 12, 'StretchRect': 6,
                'GetRenderTargetData': 6, 'Reset': 1, 'Present': 6}
    for method, minimum in required.items():
        if counts[method] < minimum:
            raise ValueError(f'Missing actual positive call coverage: {method} {counts[method]} < {minimum}')
    if counts['Present'] != 6 or counts['Reset'] != 1:
        raise ValueError('The captured complete sequence differs from the authored source')
    if not any('D3DQUERYTYPE_EVENT' in r['arguments_and_result'] for r in rows if r['method'].endswith('::CreateQuery')):
        raise ValueError('No actual EVENT query creation')
    if not any('D3DQUERYTYPE_OCCLUSION' in r['arguments_and_result'] for r in rows if r['method'].endswith('::CreateQuery')):
        raise ValueError('No actual occlusion query creation')
    if not any(r['method'] == 'memcpy' and 'blob(' in r['arguments_and_result'] for r in rows):
        raise ValueError('Writable Lock uploads were not serialized into the trace')
    return {'counts': dict(sorted(counts.items())), 'calls': len(rows),
            'threads': sorted({r['thread'] for r in rows}),
            'present_calls': [r['call'] for r in rows if r['method'].endswith('::Present')],
            'all_call_ids_unique': True, 'actual_upload_blob': True,
            'raw_depth_equivalence_validated': False, 'rows': rows}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--tool-root', type=Path, required=True)
    parser.add_argument('--backend-dll', type=Path, required=True)
    parser.add_argument('--backend-kind', choices=('native', 'dxvk', 'dxvk2'), required=True)
    parser.add_argument('--northlight-dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        raise RuntimeError('Run the x86 hidden HAL stand on Windows; no Wine fallback is inferred')
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output must be new; preserve prior failed/successful evidence')
    tools = tool_runtime(args.tool_root)
    for path in (args.backend_dll, args.northlight_dll):
        pe32(path)
    source = fp.TESTS / 'native_scene_trace_smoke.cpp'
    inputs = {str(p.resolve()): digest(p) for p in (source, Path(__file__),
              fp.SCRIPTS / 'prepare_native_scene.py', args.backend_dll, args.northlight_dll)}
    inputs.update(tools['files'])
    if any(Path(p) == output or Path(p).is_relative_to(output) for p in inputs):
        raise ValueError('Private output must not replace inputs')
    output.mkdir(parents=True)
    report = {'format': 1, 'output': str(output), 'inputs': inputs, 'commands': [], 'cases': [],
              'tools': tools, 'game_launched': False, 'installed_files_modified': False,
              'authored_plumbing_only': True, 'full_scene_replay_ready': False,
              'northlight_world_context_activated': False, 'original_producer_workload_reproduced': False,
              'fps_acceptance_proven': False, 'timings_are_performance_evidence': False}

    def copy(source, target):
        source = Path(source).resolve(); target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if digest(target) != inputs[str(source)]:
            raise ValueError('Staged input hash mismatch')

    def northlight_config(folder, backend):
        (folder / 'northlight-renderer.ini').write_text(
            f'[Renderer]\nBackend={args.backend_kind}\nBackendPath={backend.as_posix()}\n')
        (folder / 'northlight-quality.ini').write_text(
            '[Quality]\nPreset=Performance\nCommandStream=1\nStreamFramesAhead=1\nStreamFrameSkip=0\n'
            'Diagnostics=0\nRenderProfile=0\nGI=0\nActorShadows=0\nContactAO=0\nWeather=0\n')

    try:
        report['processes_initial'] = processes()
        compiler = fp.zig(); inputs[str(compiler.resolve())] = digest(compiler)
        executable = output / 'native_scene_trace_smoke.exe'
        command = [compiler, 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                   source, '-luser32', '-o', executable]
        compiler_env = fp.zig_env()
        compiler_env['ZIG_GLOBAL_CACHE_DIR'] = str(output / 'zig-global-cache')
        compiler_env['ZIG_LOCAL_CACHE_DIR'] = str(output / 'zig-local-cache')
        run(command, output, compiler_env, 'compile', report)
        executable_hash = digest(executable)
        tracer = Path(tools['root']) / 'lib/wrappers/d3d9.dll'
        cli = Path(tools['root']) / 'bin/apitrace.exe'
        retracer = Path(tools['root']) / 'bin/d3dretrace.exe'
        for aa in (0, 2):
            for layer in ('backend-only', 'above-northlight', 'below-northlight'):
                folder = output / f'{layer}-aa{aa}'; folder.mkdir()
                capture, replay = folder / 'capture', folder / 'replay'; capture.mkdir(); replay.mkdir()
                shutil.copyfile(executable, capture / executable.name)
                if digest(capture / executable.name) != executable_hash:
                    raise ValueError('Stand executable changed during staging')
                downstream = capture / 'downstream'; downstream.mkdir()
                copy(args.backend_dll, downstream / 'd3d9.dll')
                if layer == 'below-northlight':
                    copy(args.northlight_dll, capture / 'd3d9.dll')
                    copy(tracer, capture / 'tracer/native_trace_d3d9.dll')
                    northlight_config(capture, Path('tracer/native_trace_d3d9.dll'))
                    forced = downstream
                else:
                    copy(tracer, capture / 'd3d9.dll')
                    forced = downstream
                    if layer == 'above-northlight':
                        copy(args.northlight_dll, downstream / 'd3d9.dll')
                        copy(args.backend_dll, capture / 'native_scene_backend.dll')
                        northlight_config(capture, Path('native_scene_backend.dll'))
                trace = folder / 'source.trace'
                env = dict(os.environ, TRACE_FILE=str(trace), APITRACE_FORCE_MODULE_PATH=str(forced),
                           DXVK_LOG_PATH=str(capture), DXVK_CONFIG_FILE=str(capture / 'dxvk.conf'),
                           DXVK_SHADER_CACHE_PATH=str(capture / 'shader-cache'),
                           DXVK_STATE_CACHE_PATH=str(capture / 'state-cache'))
                # The source requests IMMEDIATE. This affects only this private process.
                (capture / 'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
                run([capture / executable.name, capture, str(aa)], capture, env, 'capture', report)
                original = json.loads((capture / 'source.json').read_text())
                if original['frames'] != 6 or original['msaa'] != aa or original['nonzero_occlusions'] != 6:
                    raise ValueError('Authored source did not complete all positive controls')
                module_hashes = {p: digest(p) for p in original['modules']}
                if inputs[str(args.backend_dll.resolve())] not in module_hashes.values():
                    raise ValueError('Original process did not positively load the selected backend')
                if layer != 'backend-only' and inputs[str(args.northlight_dll.resolve())] not in module_hashes.values():
                    raise ValueError('Original process did not positively load the chosen Northlight build')
                if not trace.is_file() or not trace.stat().st_size:
                    raise ValueError('Official tracer did not produce a nonempty trace')
                text = run([cli, 'dump', '--color=never', '--thread-ids=1', trace], folder,
                           dict(os.environ), 'dump', report)
                proof = coverage(text)
                if aa and not any('D3DMULTISAMPLE_2_SAMPLES' in r['arguments_and_result']
                                  for r in proof['rows'] if r['method'].endswith('::CreateRenderTarget')):
                    raise ValueError('Trace lost the authored MSAA target')
                copy(retracer, replay / 'd3dretrace.exe')
                for dependency in tools['retrace_dependencies']:
                    copy(dependency, replay / Path(dependency).name)
                if layer == 'above-northlight':
                    copy(args.northlight_dll, replay / 'd3d9.dll')
                    copy(args.backend_dll, replay / 'native_scene_backend.dll')
                    northlight_config(replay, Path('native_scene_backend.dll'))
                else:
                    copy(args.backend_dll, replay / 'd3d9.dll')
                clean_env = dict(os.environ, DXVK_LOG_PATH=str(replay),
                                 DXVK_CONFIG_FILE=str(capture / 'dxvk.conf'),
                                 DXVK_SHADER_CACHE_PATH=str(replay / 'shader-cache'),
                                 DXVK_STATE_CACHE_PATH=str(replay / 'state-cache'))
                clean_env.pop('APITRACE_FORCE_MODULE_PATH', None); clean_env.pop('TRACE_FILE', None)
                pixels = run([replay / 'd3dretrace.exe', '--headless', '--query-handling=run',
                              '-s', '-', trace], replay, clean_env, 'retrace', report, binary=True)
                actual = pnm_images(pixels)
                expected = [pnm_images((capture / im['path']).read_bytes())[0] for im in original['images']]
                if len(actual) != 6 or actual != expected:
                    raise ValueError('Complete original/retrace image sequence differs (no relaxed image tolerance)')
                replay_modules = report['commands'][-1]['observed_module_sha256']
                backend_observed = inputs[str(args.backend_dll.resolve())] in replay_modules.values()
                northlight_observed = layer != 'above-northlight' or inputs[str(args.northlight_dll.resolve())] in replay_modules.values()
                if not backend_observed or not northlight_observed:
                    raise ValueError('Replay module observation did not prove the intended private chain')
                proof.pop('rows')
                record = {'layer': layer, 'replay_label': 'baked-backend-replay' if layer == 'below-northlight' else layer,
                          'msaa': aa, 'source_module_sha256': module_hashes,
                          'trace_sha256': digest(trace), 'trace_bytes': trace.stat().st_size,
                          'positive_coverage': proof, 'exact_rgb_frames': 6,
                          'depth_visibility_guard': True, 'raw_depth_equivalence_validated': False,
                          'complete_sequence': True, 'query_handling': 'run', 'looped': False,
                          'world_context_activated': False, 'full_scene_fps_claimed': False}
                report['cases'].append(record)
                (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
        report['passed'] = len(report['cases']) == 6
    except BaseException as failure:
        report['failure'] = str(failure)
        raise
    finally:
        report['inputs_unchanged'] = all(digest(p) == sha for p, sha in inputs.items())
        report['processes_final'] = processes()
        (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    if not report['inputs_unchanged']:
        raise RuntimeError('Frozen input changed during acceptance')
    print('PASS official x86 capture/retrace plumbing: 6 chains, 36 exact frames; full game-scene/FPS remains unproven')


if __name__ == '__main__':
    main()
