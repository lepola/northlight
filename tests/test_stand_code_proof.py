#!/usr/bin/env python3
# northlight-test: requires=cxx,client manual
"""Actual raw PE / authored observations: strict stand provider and C ABI admission, no game execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    args = parser.parse_args()
    if os.name == 'nt':
        raise SystemExit('Use the portable clang++/WSL CPU fixture; this does not load the DLL or create D3D.')
    args.output, args.client = args.output.resolve(), args.client.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    paths = [Path(__file__), fp.TESTS / 'test_stand_code_proof.cpp', fp.REPO / 'northlight_paths.py', args.client,
             *(fp.src(name) for name in ('stand_api.h', 'stand_inputs.h', 'client_code_observation.h', 'client_image.h',
                                       'snapshot_store.h', 'snapshot_archive.h', 'stream_hooks.h', 'quality_settings.h'))]
    before = {str(path.resolve()): sha(path) for path in paths}
    (args.output / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
    (args.output / 'tmp').mkdir()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(args.output / 'tmp'))
    cases = ('valid', 'before-open', 'raw-compatibility', 'unknown', 'subspan', 'repeat', 'wrong-thread', 'lifetime',
             'late-static', 'late-dynamic', 'late-failed-read', 'late-snapshot', 'session-bound', 'missing-scope',
             'late-api', 'late-worker', 'failed-observation', 'changed-observation', 'crc', 'correspondence',
             'truncated', 'magic', 'null', 'oversize')
    records, accepted = [], False
    try:
        for mode, flags in (('O2', ['-O2']), ('O0', ['-O0']),
                            ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])):
            executable = args.output / ('fixture-' + mode)
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
                       *fp.test_include_flags(), str(fp.TESTS / 'test_stand_code_proof.cpp'), '-o', str(executable)]
            for case, call in [('compile', command), *[(case, [str(executable), str(args.client), case]) for case in cases]]:
                row = dict(mode=mode, case=case, command=call)
                records.append(row)
                (args.output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
                result = subprocess.run(call, cwd=fp.REPO, env=env, capture_output=True, text=True, timeout=90)
                row['returncode'] = result.returncode
                (args.output / (mode + '-' + case + '.log')).write_text(result.stdout + result.stderr)
                print(result.stdout, result.stderr, flush=True)
                result.check_returncode()
                if case != 'compile':
                    assert 'PASS stand code proof ' + case + ':' in result.stdout
        accepted = True
    finally:
        unchanged = all(Path(name).is_file() and sha(Path(name)) == value for name, value in before.items())
        validation = dict(passed=accepted and unchanged and len(records) == 3 * (1 + len(cases)) and
                          all(row.get('returncode') == 0 for row in records), inputs=before, inputs_unchanged=unchanged,
                          cases_per_variant=len(cases), records=records, client_executed=False, game_launched=False,
                          gpu_used=False, dll_loaded=False, observation_source='authored matching raw-file samples',
                          authentic_loaded_code_capture=False, whole_scene_replay=False, fps_claimed=False)
        (args.output / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n')
    assert unchanged, 'A source or client input changed'
    print('PASS exact observed-key provider, immutable owned bytes, strict pre-use proof admission and authored raw compatibility; no game/GPU/FPS claim')


if __name__ == '__main__':
    main()
