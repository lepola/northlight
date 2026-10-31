#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Compare the production lazy wake and first-publication wake without a game/GPU.

Timing output is descriptive evidence, never a performance pass/fail gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--pairs', type=int, default=11)
parser.add_argument('--checks-only', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = fp.src('command_queue.h').read_text()
old = 'if(lazyCmds_<kLazyWakeCommands&&lazyBytes_<kLazyWakeBytes)'
new = 'if(lazyCmds_<kLazyWakeCommands&&lazyBytes_<kLazyWakeBytes&&!sleeping_.load())'
assert source.count(old) + source.count(new) == 1
baseline = source.replace(new, old)
candidate = baseline.replace(old, new)
# Test control changes only the existing sleeper flag, not publication or wake code.
seam = 'unsigned wakeSignals()const{return wakeSignals_;}'
assert baseline.count(seam) == 1
environment = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(args.output / 'zig-cache'),
                   TEMP=str(args.output / 'temp'), TMP=str(args.output / 'temp'))
Path(environment['TEMP']).mkdir(exist_ok=True)
executables = {}
for label, text in [('baseline', baseline), ('first64', candidate)]:
    directory = args.output / label
    directory.mkdir(exist_ok=True)
    text = text.replace(seam, seam + '\n    void rotationArmSleep(){sleeping_.store(true);}')
    (directory / 'command_queue.h').write_text(text)
    for name in ['stream_stats.h', 'stream_wait.h', 'stream_idle_profile.h']:
        header = fp.src(name).read_text()
        if name == 'stream_wait.h':
            clock = 'inline std::uint64_t nowNs(){'
            assert header.count(clock) == 1
            header = header.replace(clock, 'inline thread_local std::uint64_t rotationClockReads=0;\n' + clock +
                                    '\n#ifdef NORTHLIGHT_ROTATION_CLOCK_CHECK\n++rotationClockReads;\n#endif\n')
        (directory / name).write_text(header)
    executable = directory / 'rotation_wake.exe'
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
               '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-I', str(directory),
               '-DNORTHLIGHT_ROTATION_FIRST_WAKE=' + str(int(label == 'first64')),
               str(Path(__file__).with_suffix('.cpp')), '-o', str(executable)]
    subprocess.run(command, env=environment, check=True)
    subprocess.run([str(executable), 'checks'], env=environment, check=True)
    debug = directory / 'rotation_wake_debug.exe'
    debug_command = ['-O0' if argument == '-O2' else argument for argument in command]
    debug_command[-1] = str(debug)
    debug_command.insert(2, '-DNORTHLIGHT_ROTATION_CLOCK_CHECK=1')
    subprocess.run(debug_command, env=environment, check=True)
    subprocess.run([str(debug), 'checks'], env=environment, check=True)
    executables[label] = executable
print('PASS deterministic production queue checks for baseline and first64; no game/GPU.', flush=True)
if args.checks_only:
    raise SystemExit(0)
rows = []
for block in range(args.pairs + 2):
    for case in ['sparse', 'dense', 'lagging', 'producer', 'publication', 'paced']:
        order = ['baseline', 'first64'] if block % 2 == 0 else ['first64', 'baseline']
        for label in order:
            result = subprocess.run([str(executables[label]), case], env=environment, check=True,
                                    capture_output=True, text=True)
            for line in result.stdout.splitlines():
                row = json.loads(line)
                row.update(variant=label, block=block - 2, warmup=block < 2)
                rows.append(row)
report = {'sourceSha256': hashlib.sha256(source.encode()).hexdigest(), 'pairs': args.pairs,
          'target': 'x86-windows-gnu', 'timingVerdict': 'descriptive-only', 'rows': rows, 'summary': []}
for case in sorted({row['case'] for row in rows}):
    samples = [row for row in rows if row['case'] == case and not row['warmup']]
    keys = ['firstNs', 'wallNs', 'producerWallNs', 'producerCpuNs', 'producerCycles',
            'consumerCpuNs', 'consumerCycles', 'meanNs', 'worstNs', 'latencyP50Ns', 'latencyP95Ns',
            'latencyMaxNs', 'wakes', 'sleeps', 'reservedBytes']
    summary = {'case': case, 'metrics': {}}
    for key in keys:
        if key not in samples[0]:
            continue
        left = {row['block']: row[key] for row in samples if row['variant'] == 'baseline'}
        right = {row['block']: row[key] for row in samples if row['variant'] == 'first64'}
        deltas = [right[block] - left[block] for block in sorted(left)]
        summary['metrics'][key] = {'baselineMedian': statistics.median(left.values()),
                                   'first64Median': statistics.median(right.values()),
                                   'baselineMax': max(left.values()), 'first64Max': max(right.values()),
                                   'pairedDeltaMedian': statistics.median(deltas),
                                   'pairedDeltaMin': min(deltas), 'pairedDeltaMax': max(deltas)}
    report['summary'].append(summary)
(args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
for summary in report['summary']:
    print(json.dumps(summary))
print('TIMING_REPORT: no automatic performance acceptance; inspect paired variability and CPU/throughput together.')
