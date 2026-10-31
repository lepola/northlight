#!/usr/bin/env python3
# northlight-test: requires=zig manual
"""Actual authored Windows RPM/observer lifecycle; no Device, GPU or game."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS))
from prepare_native_scene import digest, tool_runtime

spec = importlib.util.spec_from_file_location('observer_process_guard', fp.TESTS / 'validate_native_scene_trace.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
WRAPPER_SHA256 = 'fdb77d7b79418034a56310ad73035821928cdf12eff34a130f1bc315b5ea00cd'


def pe(path):
    data = path.read_bytes()
    assert data[:2] == b'MZ'
    nt = struct.unpack_from('<I', data, 60)[0]
    assert data[nt:nt + 4] == b'PE\0\0'
    machine, count = struct.unpack_from('<HH', data, nt + 4)
    optional, flags = struct.unpack_from('<HH', data, nt + 20)
    opt = nt + 24
    assert machine == 0x14c and struct.unpack_from('<H', data, opt)[0] == 0x10b
    assert struct.unpack_from('<I', data, opt + 28)[0] == 0x400000 and flags & 0x20 and not flags & 0x2000
    assert not struct.unpack_from('<H', data, opt + 70)[0] & 0x40, 'Owned helper must actually disable ASLR'
    return data, opt + optional, count


def raw_span(data, table, count, address, size):
    rva = address - 0x400000
    for i in range(count):
        at = table + 40 * i
        virtual, start, raw, offset = struct.unpack_from('<4I', data, at + 8)
        flags = struct.unpack_from('<I', data, at + 36)[0]
        if start <= rva and rva + size <= start + virtual:
            assert flags & 0x40000000 and not flags & 0x80000000
            assert rva - start + size <= raw and offset + rva - start + size <= len(data)
            return data[offset + rva - start:offset + rva - start + size]
    raise AssertionError('Observed data has no exact read-only file backing')


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--tool-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        raise ValueError('Run the authored Windows component only; no native/GPU substitute')
    out = args.output.resolve()
    assert not out.exists(), 'Preserve earlier evidence; output must be new'
    tools = tool_runtime(args.tool_root)
    wrapper = Path(tools['root']) / 'lib/wrappers/d3d9.dll'
    assert digest(wrapper) == WRAPPER_SHA256, 'Official 14.0 x86 wrapper differs from the admitted pin'
    compiler = fp.zig()
    source = fp.TESTS / 'native_code_observation_windows.cpp'
    watched = [Path(__file__).resolve(), source, fp.REPO / 'northlight_paths.py', compiler,
               fp.TESTS / 'validate_native_scene_trace.py', fp.SCRIPTS / 'prepare_native_scene.py']
    watched += [fp.src(name) for name in ('native_capture_windows.h', 'native_snapshot_capture.h',
                'snapshot_capture_sink.h', 'client_code_observation.h', 'client_image.h', 'snapshot_archive.h',
                'snapshot_store.h', 'world_context.h', 'backend_loader.h', 'backend_policy.h')]
    before = {str(p.resolve()): digest(p) for p in watched}
    before.update(tools['files'])
    assert not any(Path(p) == out or Path(p).is_relative_to(out) for p in before), 'Output overlaps input'
    out.mkdir(parents=True)
    report = {'format': 1, 'output': str(out), 'inputs': before, 'commands': [], 'cases': [], 'pass': False,
              'authored_component_only': True, 'original_game_scene': False, 'game_launched': False,
              'imported_client_executed': False, 'device_calls': 0, 'gpu_used': False, 'fps_eligible': False}

    def run(command, folder, env, name):
        row = {'name': name, 'command': [str(x) for x in command], 'cwd': str(folder),
               'processes_before': guard.processes()}
        report['commands'].append(row)
        try:
            result = subprocess.run(row['command'], cwd=folder, env=env, capture_output=True,
                                    timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
            row['exit_code'] = result.returncode
            (folder / (name + '.stdout')).write_bytes(result.stdout)
            (folder / (name + '.stderr')).write_bytes(result.stderr)
            result.check_returncode()
            return result.stdout.decode('utf-8', errors='replace')
        except subprocess.TimeoutExpired as error:
            row['timed_out'] = True
            (folder / (name + '.stdout')).write_bytes(error.stdout or b'')
            (folder / (name + '.stderr')).write_bytes(error.stderr or b'')
            raise
        finally:
            row['processes_after'] = guard.processes()
            (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def environment(folder):
        env = dict(os.environ)
        for key in ('NORTHLIGHT_NATIVE_CAPTURE_ROOT', 'NORTHLIGHT_RETRACE_PLAN', 'NORTHLIGHT_RETRACE_ALLOW_AUTHORED',
                    'TRACE_FILE', 'APITRACE_FORCE_MODULE_PATH', 'NORTHLIGHT_OBSERVER_ROOT',
                    'NORTHLIGHT_OBSERVER_RESULT', 'NORTHLIGHT_OBSERVER_WRAPPER'):
            env.pop(key, None)
        for key, name in [('TEMP', 'temp'), ('TMP', 'temp'), ('APPDATA', 'appdata'), ('LOCALAPPDATA', 'localappdata'),
                          ('DXVK_LOG_PATH', 'dxvk-log'), ('DXVK_SHADER_CACHE_PATH', 'shader-cache'),
                          ('DXVK_STATE_CACHE_PATH', 'state-cache')]:
            directory = folder / name
            directory.mkdir(exist_ok=True)
            env[key] = str(directory)
        env['DISABLE_VULKAN_OBS_CAPTURE'] = '1'
        return env

    try:
        report['processes_initial'] = guard.processes()
        build = out / 'build'
        build.mkdir()
        env = fp.zig_env()
        env.update(environment(build))
        for key, name in [('ZIG_LOCAL_CACHE_DIR', 'zig-local-cache'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global-cache')]:
            directory = build / name
            directory.mkdir()
            env[key] = str(directory)
        exe = build / 'native_code_observation_windows.exe'
        command = [compiler, 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                   '-Wall', '-Wextra', '-Werror', *fp.include_flags(), source, '-luser32',
                   '-Wl,--image-base=0x400000', '-o', exe]
        run(command, build, env, 'compile')
        # Zig's GNU driver rejects the PE LAA/disable-ASLR switches. Set these
        # two owned-fixture header flags explicitly, then verify the actual PE
        # and every child's loaded base. Never alter the imported client.
        image = bytearray(exe.read_bytes())
        nt = struct.unpack_from('<I', image, 60)[0]
        assert image[:2] == b'MZ' and image[nt:nt + 4] == b'PE\0\0'
        assert struct.unpack_from('<H', image, nt + 4)[0] == 0x14c
        flags = struct.unpack_from('<H', image, nt + 22)[0]
        dll_flags = struct.unpack_from('<H', image, nt + 94)[0]
        report['owned_pe_adjustment'] = {'before_sha256': digest(exe),
                                         'file_flags_before': flags, 'dll_flags_before': dll_flags}
        struct.pack_into('<H', image, nt + 22, flags | 0x20)
        struct.pack_into('<H', image, nt + 94, dll_flags & ~0x40)
        exe.write_bytes(image)
        data, table, count = pe(exe)
        executable_hash = digest(exe)
        report['executable_sha256'] = executable_hash
        report['helper_pe'] = {'fixed_base': 0x400000, 'aslr_disabled': True, 'large_address_aware': True}
        for mode in ('no-opt-in', 'prefix', 'match', 'duplicate-patch'):
            folder = out / mode
            folder.mkdir()
            capture, downstream = folder / 'capture', folder / 'downstream'
            capture.mkdir()
            downstream.mkdir()
            env = environment(folder)
            env.update(TRACE_FILE=str(folder / 'unused.trace'), APITRACE_FORCE_MODULE_PATH=str(downstream),
                       NORTHLIGHT_OBSERVER_ROOT=str(capture), NORTHLIGHT_OBSERVER_RESULT=str(folder / 'component.json'),
                       NORTHLIGHT_OBSERVER_WRAPPER=str(wrapper))
            if mode != 'no-opt-in':
                env['NORTHLIGHT_NATIVE_CAPTURE_ROOT'] = str(capture)
            stdout = run([exe, mode], folder, env, 'run')
            assert 'NORTHLIGHT_NATIVE_CODE_OBSERVER PASS mode=' + mode in stdout
            result = json.loads((folder / 'component.json').read_text(encoding='utf-8'))
            assert result['pass'] and result['authored_component'] and not result['fps_eligible']
            assert result['device_calls'] == 0 and not result['gpu_used'] and result['observer_disconnected']
            assert result['loaded_base'] == 0x400000 and result['self_sha256'] == executable_hash
            assert Path(result['self_path']).resolve() == exe
            if mode != 'no-opt-in':
                assert result['marker_owner_verified'] and result['wrapper_sha256'] == WRAPPER_SHA256
                assert Path(result['wrapper_path']).resolve() == wrapper
            entries = list(capture.iterdir())
            if mode in ('no-opt-in', 'prefix'):
                assert not entries and not result['proof_decoded']
                assert result['self_read_calls'] == (1 if mode == 'prefix' else 0)
            else:
                assert len(entries) == 1 and entries[0].is_dir()
                status = json.loads((entries[0] / 'client-code-status.json').read_text())
                host = json.loads((entries[0] / 'capture-host.json').read_text())
                assert Path(host['client_path']).resolve() == exe and host['client_sha256'] == executable_hash
                assert Path(host['renderer_path']).resolve() == exe and host['renderer_sha256'] == executable_hash
                assert Path(host['wrapper_path']).resolve() == wrapper and host['wrapper_sha256'] == WRAPPER_SHA256
                assert status['session'] == host['session'] and status['policy'] == 'strict-per-read'
                snapshots = json.loads((entries[0] / 'native-capture-status.json').read_text())
                assert snapshots['accepted'] == 0 and snapshots['retired'] == 0 and snapshots['marked'] == 0
                assert not list(entries[0].glob('*.nlsnap')), 'Never fabricate a scene snapshot'
                result['actual_snapshot_status'] = snapshots
                result['actual_capture_host'] = host
                result['code_status'] = status
                if mode == 'match':
                    proof = (entries[0] / 'client-code.nlcode').read_bytes()
                    assert status['complete'] and status['sha256'] == digest(entries[0] / 'client-code.nlcode')
                    assert status['bytes'] == len(proof) and result['proof_decoded']
                    assert struct.unpack_from('<8s4I2Q2I', proof) == (b'NLCODE1\0', 1, len(proof), 1, 64, 2, 0, 0, 0)
                    assert struct.unpack_from('<QII', proof, 48) == (result['observed_address'], 64, 2)
                    assert struct.unpack_from('<I', proof, len(proof) - 4)[0] == zlib.crc32(proof[:-4])
                    assert proof[64:-4] == bytes.fromhex(result['first_read_hex'])
                    assert proof[64:-4] == raw_span(data, table, count, result['observed_address'], 64)
                else:
                    assert not status['complete'] and not result['proof_decoded'] and result['data_restored']
                    assert not (entries[0] / 'client-code.nlcode').exists()
                    assert result['first_read_hex'] != result['second_read_hex'] and result['self_read_calls'] == 3
            report['cases'].append(result)
            assert digest(exe) == executable_hash, 'Owned helper file changed; data patch must be memory-only'
            assert all(Path(path).is_file() and digest(Path(path)) == sha for path, sha in before.items()), 'Frozen input changed'
        report['processes_final'] = guard.processes()
        report['inputs_unchanged'] = True
        report['pass'] = len(report['cases']) == 4
    finally:
        report['inputs_unchanged'] = all(Path(path).is_file() and digest(Path(path)) == sha for path, sha in before.items())
        (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert report['pass'] and report['inputs_unchanged']
    print('PASS four actual Windows observer/RPM modes; authored CPU component only, no scene/FPS acceptance')


if __name__ == '__main__':
    main()
