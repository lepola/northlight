#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Final-only exact snapshot transport and immutable native-scene preparation."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rejects(function):
    try:
        function()
    except ValueError:
        return
    raise AssertionError('Invalid archive/provenance was accepted')


def main():
    source = fp.TESTS / 'test_snapshot_archive.cpp'
    script = fp.SCRIPTS / 'prepare_native_scene.py'
    inputs = [source, Path(__file__), script, fp.src('snapshot_archive.h'), fp.src('snapshot_store.h')]
    before = {str(p): sha(p) for p in inputs}
    spec = importlib.util.spec_from_file_location('native_scene_preparation', script)
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    results = []
    with tempfile.TemporaryDirectory(prefix='snapshot-archive-final-') as directory:
        root = Path(directory)
        for name, flags in [('O2', ['-O2']), ('O0', ['-O0']),
                            ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            executable, archive = root / name, root / (name + '.nlsnap')
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                       *fp.test_include_flags(), str(source), '-o', str(executable)]
            print('COMPILE', command, flush=True)
            subprocess.run(command, check=True)
            run = subprocess.run([str(executable), str(archive)], capture_output=True, text=True, timeout=30)
            print(name, run.stdout, run.stderr, flush=True)
            run.check_returncode()
            assert 'SNAPSHOT_ARCHIVE_PASS groups=4' in run.stdout
            metadata = native.inspect_snapshot(archive)
            assert metadata['keys'] == 3 and metadata['copies'] == 5 and metadata['payload_bytes'] == 8
            assert metadata['recorded_failures'] == 2 and metadata['draw_ordinal'] == 9876
            results.append({'mode': name, 'command': command, 'stdout': run.stdout,
                            'stderr': run.stderr, 'exe_sha256': sha(executable), 'archive': metadata})
        assert len({r['archive']['sha256'] for r in results}) == 1
        archive = root / 'O2.nlsnap'
        bad = root / 'corrupt.nlsnap'
        data = bytearray(archive.read_bytes());data[-5] ^= 1;bad.write_bytes(data)
        rejects(lambda: native.inspect_snapshot(bad))
        # This negative-control input deliberately is NOT a real API trace.
        # Preparation must never promote unparsed bytes to a ready replay.
        trace = root / 'unparsed-negative-control.trace';trace.write_bytes(b'not a parsed D3D9 trace')
        index = root / 'index.json'
        payload = {'format': 1, 'capture_layer': 'above-northlight', 'trace_sha256': sha(trace),
                   'records': [{'trace_call': 50, 'draw_ordinal': 9876, 'path': archive.name, 'sha256': sha(archive)}]}
        index.write_text(json.dumps(payload))
        args = argparse.Namespace(output=root / 'prepared', capture_layer='above-northlight', scene_manifest=None,
                                  trace=trace, snapshot_index=index, retracer=None, backend_dll=None,
                                  backend_kind=None, northlight_dll=None, quality_ini=None)
        original = {str(p): sha(p) for p in (trace, index, archive)}
        manifest = native.prepare(args)
        assert not manifest['trace_parsed_by_official_tool'] and not manifest['full_scene_replay_ready']
        assert not manifest['fps_acceptance_proven'] and not manifest['game_launched'] and not manifest['replay_executed']
        assert manifest['snapshots'][0]['sha256'] == sha(archive) and not manifest['replay_plans']
        assert all(sha(Path(p)) == h for p, h in original.items())
        rejects(lambda: native.prepare(args)) # Existing evidence is immutable.
        args.output = root / 'mismatched'
        payload['trace_sha256'] = '0' * 64;index.write_text(json.dumps(payload));rejects(lambda: native.prepare(args))
        payload['trace_sha256'] = sha(trace);payload['records'][0]['draw_ordinal'] = 1
        index.write_text(json.dumps(payload));rejects(lambda: native.prepare(args))
        payload['records'][0]['draw_ordinal'] = 9876;payload['records'][0]['path'] = '../outside.nlsnap'
        index.write_text(json.dumps(payload));rejects(lambda: native.prepare(args))
        fake = root / 'not-a-tool.exe';fake.write_bytes(b'not PE32');rejects(lambda: native.pe32(fake))
        # Architecture-only positive control: no executable code, no execution
        # and no claim that this synthetic PE header is an actual retracer.
        pe = bytearray(90);pe[:2] = b'MZ';struct.pack_into('<I', pe, 60, 64)
        pe[64:68] = b'PE\0\0';struct.pack_into('<H', pe, 68, 0x14c);struct.pack_into('<H', pe, 88, 0x10b)
        fake.write_bytes(pe);native.pe32(fake)
        args.snapshot_index = None;args.output = root / 'chains';args.retracer = args.backend_dll = args.northlight_dll = fake
        args.backend_kind = 'dxvk'
        staged = native.prepare(args)
        assert [p['mode'] for p in staged['replay_plans']] == ['native-only', 'northlight']
        assert all(not p['execution_validated'] and not p['uncapped_replay_validated'] for p in staged['replay_plans'])
        assert 'Backend=dxvk' in (args.output / 'northlight/northlight-renderer.ini').read_text()
        args.output = root / 'double-effect';args.capture_layer = 'below-northlight'
        rejects(lambda: native.prepare(args))
        args.northlight_dll = None;args.output = root / 'baked';staged = native.prepare(args)
        assert [p['mode'] for p in staged['replay_plans']] == ['baked-backend-replay']
    assert all(sha(Path(p)) == h for p, h in before.items()), 'Source changed during final archive validation'
    output = fp.output_dir();output.mkdir(parents=True, exist_ok=True)
    (output / 'snapshot-archive-validation.json').write_text(json.dumps({'inputs': before, 'inputs_unchanged': True,
        'records': results, 'game_launched': False, 'gpu_used': False, 'full_scene_fps_claimed': False}, indent=2) + '\n')
    print('PASS exact bounded FIFO archive and fail-closed import; unchanged production fallback; private preparation cannot claim unparsed replay readiness.')


if __name__ == '__main__':
    main()
