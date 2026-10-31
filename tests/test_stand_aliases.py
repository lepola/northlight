#!/usr/bin/env python3
# northlight-test: requires=cxx,zig,client manual
"""Typed per-packet alias C ABI and actual queued replayer; fake D3D, no GPU or game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.TESTS))
import test_command_stream as stream


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    args = parser.parse_args()
    if os.name == 'nt':
        raise SystemExit('Use the portable clang++/WSL CPU fixture with the generated D3D stub.')
    args.output = args.output.resolve()
    args.client = args.client.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    initial = {str(path.resolve()): sha(path) for base in (fp.SRC, fp.TESTS, fp.REPO / 'scripts')
               for path in base.rglob('*') if path.is_file() and path.suffix in ('.h', '.inl', '.inc', '.cpp', '.py') and '__pycache__' not in path.parts}
    dependencies = {str(path.resolve()) for path in (Path(__file__), args.client, fp.TESTS / 'test_command_stream.py',
                    fp.REPO / 'northlight_paths.py', Path(stream.gf.__file__), stream.gf.SDK)}
    for name in dependencies:
        initial.setdefault(name, sha(Path(name)))
    (args.output / 'd3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n' + stream.stub())
    (args.output / 'cs_generated.inc').write_text(stream.generated_cpp())
    generated = {path.name: sha(path) for path in args.output.iterdir() if path.is_file()}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(args.output / 'tmp'))
    (args.output / 'tmp').mkdir()
    modes = ('ordered', 'retired-owner', 'zero', 'max10', 'wrong-thread', 'dead', 'reused', 'missing-id',
             'unsealed', 'no-stream', 'before-marker', 'duplicate', 'wrong-kind', 'dead-bind', 'missing-bind',
             'low-id', 'unaligned-id', 'wide-id', 'end-id', 'null-proxy', 'oversize', 'null-pairs',
             'wrong-draw', 'wrong-trigger', 'repeat', 'commit-order', 'missing-commit', 'provider-throws')
    records = []
    unchanged = False
    accepted = False
    try:
        for mode, flags in (('O2', ['-O2']), ('O0', ['-O0']),
                            ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize=vptr', '-fno-omit-frame-pointer'])):
            binary, depfile = args.output / ('fixture-' + mode), args.output / (mode + '.d')
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DNL_STUB',
                       '-MMD', '-MF', str(depfile), *flags, '-I', str(args.output), *fp.test_include_flags(),
                       str(fp.TESTS / 'test_stand_aliases.cpp'), '-o', str(binary)]
            calls = [('compile', command)] + [(case, [str(binary), str(args.client), case]) for case in modes]
            for case, call in calls:
                row = dict(mode=mode, case=case, command=call)
                records.append(row)
                (args.output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
                result = subprocess.run(call, cwd=fp.REPO, env=env, capture_output=True, text=True, timeout=180)
                row['returncode'] = result.returncode
                (args.output / (mode + '-' + case + '.log')).write_text(result.stdout + result.stderr)
                print(result.stdout, result.stderr, flush=True)
                if depfile.is_file():
                    text = depfile.read_text().replace('\\\n', '')
                    for name in shlex.split(text.split(':', 1)[1]):
                        path = Path(name)
                        if not path.is_absolute():
                            path = fp.REPO / path
                        path = path.resolve()
                        if not path.is_relative_to(args.output):
                            dependencies.add(str(path))
                result.check_returncode()
                if case != 'compile':
                    assert 'PASS stand aliases ' + case + ':' in result.stdout
        unchanged = all(name in initial and Path(name).is_file() and sha(Path(name)) == initial[name] for name in dependencies)
        assert unchanged, 'An actual source/client dependency changed'
        accepted = True
    finally:
        unchanged = all(name in initial and Path(name).is_file() and sha(Path(name)) == initial[name] for name in dependencies)
        validation = dict(passed=accepted and unchanged and len(records) == 3 * (1 + len(modes)) and all(row.get('returncode') == 0 for row in records),
                          inputs={name: initial.get(name) for name in sorted(dependencies)}, inputs_unchanged=unchanged,
                          generated=generated, records=records, cases_per_variant=len(modes),
                          actual_replayer=True, target='fake D3D CPU only', game_launched=False, gpu_used=False,
                          whole_scene_replay=False, fps_claimed=False)
        (args.output / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n')
    print('PASS per-snapshot typed aliases, C ABI sealing, queued producer-ahead/reset/lifetime and stale-generation refusal; no scene/FPS claim')


if __name__ == '__main__':
    main()
