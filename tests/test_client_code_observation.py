#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Bounded live/raw correspondence; optional original PE is data only."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--client', type=Path, help='Optional read-only Wow.exe; never mapped or executed')
    args = parser.parse_args()
    out = (args.output or fp.output_dir() / 'client-code-observation').resolve()
    assert not out.exists() or not any(out.iterdir()), 'Preserve previous test evidence'
    inputs = [Path(__file__).resolve(), fp.REPO / 'northlight_paths.py', fp.TESTS / 'test_client_code_observation.cpp',
              fp.src('client_code_observation.h'), fp.src('client_image.h'), fp.src('snapshot_archive.h'), fp.src('snapshot_store.h')]
    if args.client:
        inputs.append(args.client.resolve())
    assert all(not p.resolve().is_relative_to(out) for p in inputs), 'Output overlaps frozen input'
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {str(p.resolve()): sha(p) for p in inputs}
    out.mkdir(parents=True, exist_ok=True)
    (out / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
    records, results = [], []

    def run(mode, stage, command):
        row = {'mode': mode, 'stage': stage, 'command': command}
        records.append(row)
        (out / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
        result = subprocess.run(command, capture_output=True, text=True)
        row['exit_code'] = result.returncode
        (out / (mode + '-' + stage + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result

    compiler = subprocess.check_output(['clang++', '--version'], text=True)
    try:
        for mode, flags in [('O2', ['-O2']), ('O0', ['-O0']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
            exe = out / mode
            command = ['clang++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                       *fp.test_include_flags(), str(fp.TESTS / 'test_client_code_observation.cpp'), '-o', str(exe)]
            compiled = run(mode, 'compile', command)
            if compiled.returncode:
                print(compiled.stdout + compiled.stderr, file=sys.stderr)
            compiled.check_returncode()
            executed = run(mode, 'run', [str(exe), *([str(args.client.resolve())] if args.client else [])])
            if executed.returncode:
                print(executed.stdout + executed.stderr, file=sys.stderr)
            executed.check_returncode()
            print(executed.stdout, end='', flush=True)
            results.append({'mode': mode, 'executable_sha256': sha(exe), 'pass': True})
        # This recreates the unsafe first-observation-only shortcut. The same
        # independently authored changed live bytes must reach their own marker.
        mutant = out / 'negative-duplicate-patch'
        mutant.mkdir()
        source = fp.src('client_code_observation.h').read_text(encoding='utf-8')
        needle = '||std::memcmp(raw,bytes,size)'
        assert source.count(needle) == 1
        header = mutant / 'client_code_observation.h'
        header.write_text(source.replace(needle, '||(!proof_.find(address,size)&&std::memcmp(raw,bytes,size))'), encoding='utf-8', newline='\n')
        exe = mutant / 'check'
        compiled = run('negative', 'compile', ['clang++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-O2',
                       '-I' + str(mutant), *fp.test_include_flags(), str(fp.TESTS / 'test_client_code_observation.cpp'), '-o', str(exe)])
        if compiled.returncode:
            print(compiled.stdout + compiled.stderr, file=sys.stderr)
        compiled.check_returncode()
        executed = run('negative', 'run', [str(exe)])
        assert executed.returncode != 0 and 'LIVE_PATCH_ACCEPTED' in executed.stderr, 'Negative missed independent changed-live-byte marker'
        results.append({'mode': 'negative-duplicate-patch', 'header_sha256': sha(header), 'executable_sha256': sha(exe),
                        'expected_marker': 'LIVE_PATCH_ACCEPTED', 'pass': True})
    finally:
        unchanged = all(Path(path).is_file() and sha(Path(path)) == digest for path, digest in before.items())
        report = {'pass': len(results) == 4 and all(r['pass'] for r in results) and unchanged,
                  'inputs': before, 'inputs_unchanged': unchanged, 'compiler': compiler, 'commands': records, 'results': results,
                  'client_executed': False, 'gpu_used': False, 'game_launched': False,
                  'live_patch_inputs': 'independently authored changes, not observed game memory', 'fps_claimed': False}
        (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    assert unchanged, 'Source or private client input changed'


if __name__ == '__main__':
    main()
