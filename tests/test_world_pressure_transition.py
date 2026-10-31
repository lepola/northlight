#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Production boundary ordering with actual populated shared snapshot storage.

Other consumers record their calls and hold empty terrain/GPU caches. This is a
CPU component proof, not a complete WorldRenderer, native working set or FPS run.
Optional --pairs runs alternating old/current full-boundary timings with setup
excluded; all cases retain identical ownership, caps and final cache contents.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def method(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def game_absent():
    if os.name == 'nt':
        command = ['powershell', '-NoProfile', '-Command',
                   'Get-Process -Name Wow,Wow64 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id']
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode in (0, 1) and not result.stdout.strip(), 'Game is active: refuse component jobs'
    elif Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists() or 'microsoft' in Path('/proc/version').read_text().lower():
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                                 'Get-Process -Name Wow,Wow64 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id'],
                                capture_output=True, text=True)
        assert result.returncode in (0, 1) and not result.stdout.strip(), 'Game is active: refuse component jobs'


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pairs', type=int, choices=range(1, 6), help='Optional alternating component timing pairs; five boundaries/case/process')
args = parser.parse_args()
out = fp.output_dir() / 'world-pressure-transition'
out.mkdir(parents=True, exist_ok=True)
source = fp.TESTS / 'test_world_pressure_transition.cpp'
stub_source = fp.TESTS / 'test_terrain_snapshot.py'
watched = [p for p in fp.SRC.rglob('*') if p.is_file()] + [source, stub_source, Path(__file__), fp.REPO / 'northlight_paths.py']
inputs = {str(p.resolve()): sha(p) for p in watched}
renderer = fp.src('renderer.cpp').read_text(encoding='utf-8')
world = fp.src('world_memory_guard.inl').read_text(encoding='utf-8')
stub = next(ast.literal_eval(node.value) for node in ast.parse(stub_source.read_text()).body
            if isinstance(node, ast.Assign) and any(getattr(target, 'id', '') == 'stub' for target in node.targets))
methods = '\n'.join([method(world, 'struct MemoryTrim') + ';', method(world, 'MemoryTrim trimMemory('),
                     method(world, 'void setMemoryPressure(')])
trim = method(renderer, 'void trimMemory(const NorthlightMemoryDiagnostics::Sample& trigger,')
boundary = method(renderer, 'if(memoryWorld>=0||memoryCaps>=0||memoryTrim)')
generated = trim + '\nvoid boundary(bool memoryTrim,int memoryWorld,int memoryCaps){const NorthlightMemoryDiagnostics::Sample memorySample{};\n' + boundary + '\n}\n'
preclear = 'if(memoryTrim&&world){worldTrim=world->trimMemory(memoryGuard.capsHalved(),retirementBatch);QueryPerformanceCounter(&worldTrimEnd);}'
assert generated.count(preclear) == 1 and generated.count('const auto& t=worldTrim;') == 1
# The old order performs its real world clear after the other late consumers.
# Only that ordering is changed: same methods, limits, shared payloads and holds.
old = generated.replace(preclear, '').replace('const auto& t=worldTrim;', 'WorldRenderer::MemoryTrim t;if(world)t=world->trimMemory();(void)worldTrim;')
assert old != generated
report = {'scope': __doc__, 'inputs': inputs, 'runs': [], 'pairs': [], 'gpu_or_game_launched': False,
          'actual_cache': 'NorthlightDrawSnapshot::Frame shared SourcePool; populated through existing friend access',
          'empty_recording_consumers': ['terrain cache', 'replay GPU cache', 'mesh pools', 'material pressure policy', 'surface/state/water'],
          'game_absence_checks': 0}


def run(exe, arguments, label, rejection=None):
    game_absent()
    report['game_absence_checks'] += 1
    result = subprocess.run([str(exe), *arguments], capture_output=True, text=True, timeout=120)
    game_absent()
    report['game_absence_checks'] += 1
    log = result.stdout + result.stderr
    (out / (label + '.log')).write_text(log, encoding='utf-8')
    report['runs'].append({'label': label, 'exe_sha256': sha(exe), 'arguments': arguments,
                           'returncode': result.returncode, 'expected_rejection': rejection})
    if rejection:
        assert result.returncode != 0 and rejection in log, (label, result.returncode, log)
        return []
    assert result.returncode == 0 and 'WORLD_PRESSURE_PASS ' in log, (label, result.returncode, log)
    rows = [dict(re.findall(r'(\w+)=([^\s]+)', line)) for line in log.splitlines() if line.startswith('WORLD_PRESSURE_ROW ')]
    assert len(rows) == (25 if '--measure' in arguments else 5), (label, rows)
    return rows


def compare(control, current):
    assert len(control) == len(current)
    exact = ['case', 'entries', 'payload', 'after', 'cap', 'live', 'releases', 'totalReleases', 'measured']
    for before, after in zip(control, current):
        assert all(before[key] == after[key] for key in exact), (before, after)
        if before['case'] == 'populated':
            assert int(before['evictions']) > 0 and after['evictions'] == '0', (before, after)
        else:
            assert before['evictions'] == after['evictions'], (before, after)


executables = {}
try:
    game_absent()
    report['game_absence_checks'] += 1
    for name, body in [('old-order', old), ('current', generated)]:
        folder = out / name
        folder.mkdir(exist_ok=True)
        (folder / 'd3d9.h').write_text(stub, encoding='utf-8')
        (folder / 'world_pressure_methods.h').write_text(methods, encoding='utf-8')
        (folder / 'world_pressure_renderer.h').write_text(body, encoding='utf-8')
        modes = [('O2', ['-O2'])]
        if name == 'current':
            modes += [('O0', ['-O0']), ('ASan-UBSan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'])]
        for mode, flags in modes:
            exe = folder / mode
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
                       '-I', str(folder), *fp.test_include_flags(), str(source), '-o', str(exe)]
            result = subprocess.run(command, capture_output=True, text=True)
            (out / (name + '-' + mode + '-build.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            report['runs'].append({'build': name + '-' + mode, 'command': command, 'exe_sha256': sha(exe)})
            executables[name, mode] = exe
    current = run(executables['current', 'O2'], [], 'current-O2')
    control = run(executables['old-order', 'O2'], ['--old-order'], 'old-order-O2')
    compare(control, current)
    run(executables['old-order', 'O2'], [], 'negative-old-order', 'WORLD_PRECLEAR_REDUNDANT_LRU')
    run(executables['current', 'O0'], [], 'current-O0')
    run(executables['current', 'ASan-UBSan'], [], 'current-ASan-UBSan')
    for pair in range(args.pairs or 0):
        cells = {}
        order = ['old-order', 'current'] if pair % 2 == 0 else ['current', 'old-order']
        for name in order:
            arguments = (['--old-order'] if name == 'old-order' else []) + ['--measure']
            cells[name] = run(executables[name, 'O2'], arguments, f'pair-{pair + 1}-{name}')
        compare(cells['old-order'], cells['current'])
        report['pairs'].append({'order': order, 'rows': cells})
    if report['pairs']:
        report['component_timings'] = {}
        for case in ['populated', 'fitting', 'empty', 'cap-only', 'recovery']:
            cells = {name: [int(row['ns']) for pair in report['pairs'] for row in pair['rows'][name] if row['case'] == case]
                     for name in ['old-order', 'current']}
            report['component_timings'][case] = {name: {'calls': len(values), 'median_ms': statistics.median(values) / 1e6,
                                                       'min_ms': min(values) / 1e6, 'max_ms': max(values) / 1e6}
                                                 for name, values in cells.items()}
    report['pass'] = True
finally:
    report['generated_inputs'] = {str(p): sha(p) for folder in ['old-order', 'current'] for p in (out / folder).glob('*.h')}
    report['inputs_unchanged'] = all(sha(Path(path)) == digest for path, digest in inputs.items())
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
assert report['inputs_unchanged'], 'Frozen source/helper changed during validation'
print('PASS world clear before mandatory cap changes: actual shared cache, owner-thread releases, exact holds/caps, old-order negative; no game FPS claim.')
