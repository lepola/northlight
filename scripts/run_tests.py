#!/usr/bin/env python3
"""Run the renderer test suite in parallel at low CPU priority.

    python3 scripts/run_tests.py                     # every tests/test_*.py
    python3 scripts/run_tests.py test_replay_*       # by name pattern (validate_*/verify_* only when named)
    python3 scripts/run_tests.py --require client    # a missing client FAILs instead of SKIPping
    python3 scripts/run_tests.py --record 0.3.158    # reports go to renderer/records/validation-0.3.158

Each test declares what it needs on one line near the top:
    # northlight-test: requires=client,cxx timing slow
A test whose requirement is not available is reported SKIP with the reason; see tests/README.md.
`known-fail=<reason>` (last on the line) marks a test that fails at HEAD for a known reason, listed
in tests/KNOWN_FAILURES.md: it still runs, a failure is reported XFAIL and does not fail the run,
a pass is reported XPASS (remove the tag).
Tests run under `nice -n 15`, 4 at a time, slow ones first; `timing` tests (CPU-time budgets)
run alone at the end. Each run writes results.tsv. Failures also keep a bounded diagnostic log and any declared reproducer.
"""
import argparse
import concurrent.futures as cf
import fnmatch
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402
import check_layout  # noqa: E402  (scripts/ is this script's directory)

TAG_LINE = re.compile(r'^#\s*northlight-test:(.*)$', re.M)
known_fail = {}   # test name -> reason, from 'known-fail=<reason>'
DISCOVERY = fp.TESTS / 'discovery.txt'


def probe(name):
    """None if the requirement is available, else the reason it is not."""
    def found(fn):
        try:
            fn()
            return None
        except fp.Missing as e:
            return str(e)
    if name == 'cxx':
        missing = [c for c in ('clang++', 'c++') if not shutil.which(c)]
        return f'host C++ compiler not on PATH: {", ".join(missing)}' if missing else None
    if name == 'libcxx':
        if sys.platform == 'darwin':
            return None
        if not shutil.which('clang++'):
            return 'clang++ with LLVM libc++ is not on PATH'
        output = fp.out() / 'requirements'
        output.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(['clang++', '-stdlib=libc++', '-x', 'c++', '-', '-o', str(output / 'libcxx-probe')],
                                input='#include <vector>\n#ifndef _LIBCPP_VERSION\n#error LLVM libc++ required\n#endif\nint main(){std::vector<int> v;return v.size();}\n',
                                capture_output=True, text=True)
        return None if result.returncode == 0 else 'LLVM libc++ headers/runtime missing (install libc++-dev and libc++abi-dev)'
    if name == 'lua51':
        env = os.environ.copy()
        if env.get('NORTHLIGHT_LUA_PYTHONPATH'):
            env['PYTHONPATH'] = env['NORTHLIGHT_LUA_PYTHONPATH'] + os.pathsep + env.get('PYTHONPATH', '')
        r = subprocess.run([sys.executable, '-c', 'from lupa.lua51 import LuaRuntime; assert LuaRuntime().eval("_VERSION") == "Lua 5.1"'], env=env, capture_output=True)
        return None if r.returncode == 0 else 'Lua 5.1 test runtime missing (lupa.lua51)'
    if name == 'windows-exe':
        return None if os.name == 'nt' else 'The host cannot execute Windows test programs'
    if name in ('lldb', 'objdump'):
        return None if shutil.which(name) else f'{name} is not on PATH'
    if name == 'windows-gpu':
        if os.name != 'nt':
            return 'The offscreen D3D9 oracle requires Windows'
        try:
            zig = fp.zig()
        except fp.Missing as e:
            return str(e)
        source = fp.TESTS / 'windows_gpu_probe.cpp'
        output = fp.out() / 'requirements'
        output.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256(source.read_bytes() + str(zig).encode()).hexdigest()[:16]
        executable = output / f'windows-gpu-{digest}.exe'
        if not executable.is_file():
            subprocess.run([str(zig), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17',
                            str(source), '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        return None if result.returncode == 0 else result.stdout.strip() or 'System D3D9 hidden device unavailable'
    if name == 'base':
        base = os.environ.get('BASE')
        return None if base and (Path(base) / 'renderer.cpp').is_file() else \
            'BASE is not set to a pristine older renderer tree (the release gate compares against it)'
    probes = {'client': fp.client_root, 'zig': fp.zig, 'wine': fp.wine_root, 'stormlib': fp.stormlib,
              'archive': fp.archive, 'backups': fp.backups, 'dll': fp.dll,
              'stormlib-src': fp.stormlib_source, 'world-cache': fp.world_cache}
    if name not in probes:
        return f'unknown requirement {name!r}'
    return found(probes[name])


def tags(path):
    """(requires, flags) from the '# northlight-test:' line in the first 30 lines."""
    head = ''.join(path.open(encoding='utf-8', errors='replace').readlines()[:30])
    m = TAG_LINE.search(head)
    requires, flags = [], set()
    line = m.group(1) if m else ''
    if 'known-fail=' in line:   # the reason runs to the end of the line
        line, reason = line.split('known-fail=', 1)
        flags.add('known-fail')
        known_fail[path.name] = reason.strip()
    for word in line.split('(')[0].split():   # '(...)' is a comment
        if word.startswith('requires='):
            requires += [r for r in word[len('requires='):].split(',') if r]
        else:
            flags.add(word)
    return requires, flags


def discover(patterns):
    everything = sorted(p for g in ('test_*.py', 'validate_*.py', 'verify_*.py') for p in fp.TESTS.glob(g))
    if not patterns:
        return [p for p in everything if p.name.startswith('test_')]
    names = [n if n.endswith('.py') or '*' in n else n + '.py' for n in patterns]
    return [p for p in everything if any(fnmatch.fnmatch(p.name, n) or fnmatch.fnmatch(p.stem, n) for n in names)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('patterns', nargs='*', help='test names or fnmatch patterns (default: tests/test_*.py)')
    ap.add_argument('-j', '--jobs', type=int, default=4)
    ap.add_argument('--require', default=os.environ.get('NORTHLIGHT_REQUIRE', ''),
                    help='comma-separated requirements that must be available (FAIL instead of SKIP)')
    ap.add_argument('--record', metavar='VERSION', help='write reports to renderer/records/validation-VERSION')
    ap.add_argument('--timeout', type=int, default=1800, help='per-test timeout in seconds')
    ap.add_argument('--no-nice', action='store_true', help='do not lower the CPU priority')
    ap.add_argument('--update-discovery', action='store_true', help=f'rewrite {DISCOVERY.relative_to(fp.REPO)}')
    args = ap.parse_args()

    problems = check_layout.check()
    if problems:
        print('\n'.join(['check_layout failed:', *problems]), file=sys.stderr)
        return 2
    tests = discover(args.patterns)
    if not args.patterns:
        names = [t.name for t in tests]
        if args.update_discovery:
            DISCOVERY.write_text(''.join(n + '\n' for n in names))
        listed = DISCOVERY.read_text().split() if DISCOVERY.is_file() else []
        if names != listed:
            print(f'Discovered tests differ from {DISCOVERY.relative_to(fp.REPO)}: '
                  f'new {sorted(set(names) - set(listed))}, missing {sorted(set(listed) - set(names))}. '
                  'Rerun with --update-discovery if intended.', file=sys.stderr)
            return 2
    if not tests:
        print('No tests match.', file=sys.stderr)
        return 2

    required = {r for r in args.require.split(',') if r}
    run_id = time.strftime('%Y%m%d-%H%M%S')
    output = fp.out() / 'test-output'
    output.mkdir(parents=True, exist_ok=True)
    out_root = Path(tempfile.mkdtemp(prefix=run_id + '-', dir=output))
    record = fp.RECORDS / f'validation-{args.record}' if args.record else None
    prefix = [] if args.no_nice or not shutil.which('nice') else ['nice', '-n', '15']
    probes = {}
    plan, results = [], {}
    for t in tests:
        requires, flags = tags(t)
        for requirement in requires:
            if requirement not in probes:
                probes[requirement] = probe(requirement)
        missing = [(r, probes[r]) for r in requires]
        missing = [(r, why) for r, why in missing if why]
        if 'manual' in flags and not args.patterns:
            results[t.name] = ('SKIP', 0.0, 'manual: run it by name')
        elif missing:
            status = 'FAIL' if any(r in required for r, _ in missing) else 'SKIP'
            results[t.name] = (status, 0.0, '; '.join(f'requires {r}: {why}' for r, why in missing))
        else:
            plan.append((t, flags))

    def run(item):
        t, flags = item
        start = time.time()
        with tempfile.TemporaryDirectory(dir=out_root, prefix=t.stem + '-') as scratch:
            work = Path(scratch)
            env = dict(os.environ, NORTHLIGHT_TEST_OUTPUT_DIR=str(record or work))
            try:
                r = subprocess.run(prefix + [sys.executable, str(t)], cwd=fp.TESTS, env=env,
                                   capture_output=True, text=True, timeout=args.timeout)
                status = 'PASS' if r.returncode == 0 else 'FAIL'
                tail = (r.stdout + r.stderr).strip()
            except subprocess.TimeoutExpired as exc:
                status = 'TIMEOUT'
                tail = ((exc.stdout or b'') + (exc.stderr or b'')).decode(errors='replace')
            if 'known-fail' in flags and status != 'TIMEOUT':
                status = {'FAIL': 'XFAIL', 'PASS': 'XPASS'}[status]
            if record or status in ('FAIL', 'TIMEOUT', 'XFAIL'):
                log = out_root / 'logs' / (t.stem + '.log')
                log.parent.mkdir(parents=True, exist_ok=True)
                log.write_bytes((tail[-16384:] + '\n').encode())
                if (work / 'repro').is_dir():
                    (work / 'repro').rename(out_root / t.stem)
        last = tail.splitlines()[-1] if tail and status not in ('PASS', 'XPASS') else ''
        if status in ('XFAIL', 'XPASS'):
            last = f'known-fail: {known_fail.get(t.name, "")} | {last}' if status == 'XFAIL' else 'passes now: remove known-fail'
        return t.name, (status, time.time() - start, last[:300])

    parallel = [i for i in plan if 'timing' not in i[1]]
    parallel.sort(key=lambda i: ('slow' not in i[1], i[0].name))
    serial = [i for i in plan if 'timing' in i[1]]
    print(f'{len(plan)} to run ({len(serial)} timing tests serially at the end), '
          f'{len(results)} not run; results in {out_root.relative_to(fp.REPO) if fp.REPO in out_root.parents else out_root}', flush=True)
    with cf.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        for name, res in ex.map(run, parallel):
            results[name] = res
            print(f'{res[0]:7} {name}', flush=True)
    for item in serial:
        name, res = run(item)
        results[name] = res
        print(f'{res[0]:7} {name}', flush=True)

    out_root.mkdir(parents=True, exist_ok=True)
    with open(out_root / 'results.tsv', 'w') as f:
        for name in sorted(results):
            status, seconds, note = results[name]
            f.write(f'{name}\t{status}\t{seconds:.0f}\t{note}\n')
    counts = {s: sum(1 for v in results.values() if v[0] == s) for s in ('PASS', 'FAIL', 'TIMEOUT', 'SKIP', 'XFAIL', 'XPASS')}
    for name in sorted(results):
        status, _, note = results[name]
        if status != 'PASS':
            print(f'{status:7} {name}: {note}')
    print(' '.join(f'{k}={v}' for k, v in counts.items()) + f'  ({out_root / "results.tsv"})')
    return 1 if counts['FAIL'] or counts['TIMEOUT'] else 0


if __name__ == '__main__':
    sys.exit(main())
