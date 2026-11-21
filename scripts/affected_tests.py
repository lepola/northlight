#!/usr/bin/env python3
"""Print the tests that cover the changed files, so a change runs only those (not the ~13 minute suite).

    python3 scripts/affected_tests.py                       # local changes, including commits since the branch baseline
    python3 scripts/affected_tests.py src/proxy/spell_layer.h shaders/effects.hlsl
    python3 scripts/affected_tests.py --why                 # also show how each test reaches the file
    python3 scripts/affected_tests.py --run                 # pass the list to scripts/run_tests.py

Heuristic (over-selects rather than misses):
  * A test covers a file whose basename (or any path component) appears in the test's .py text.
  * The reach is followed through what the test builds: the tests/*.cpp, *.h and helper .py it
    names (and the sibling tests/test_<stem>.cpp), everything those name, and the `#include "..."`
    chain of what it compiles (production .cpp, headers #included in a harness string or test file),
    so a test compiling a.h is affected by b.h when a.h includes b.h. A header a test only names
    (read as text) is covered itself but its includes are not followed.
    A shader build manifest (*-shader-build.json) reaches the files it names.
  * A test that scans a tree covers every file under it with a matching suffix: `fp.sources(...)`
    (all of src/), and a line that walks a directory (glob, rglob, iterdir, walk) over fp.SRC_DIRS,
    fp.SRC, fp.REPO / 'src' (/ 'group'), fp.SHADERS, fp.GENERATED, fp.GAMEDATA or
    fp.src('x.h').parent. Suffixes are the quoted '.ext' and '*.ext' of that line and the next; none
    means any file. A walk the patterns cannot place is missed: name the directory in the test.
  * A changed tests/test_*.py selects itself.
  * Shared infrastructure (northlight_paths.py, scripts/run_tests.py, scripts/check_layout.py,
    tests/support/**) is not mapped: the note on stderr recommends the full suite.
  * A changed file no test covers is listed on stderr: nothing guards it.
"""
import argparse
import os
import re
import subprocess
import sys
from functools import cached_property
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402

TOKEN = re.compile(r'[\w.\-]+')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
WALK = re.compile(r'\.(?:glob|rglob|iterdir)\(|os\.walk|SRC_DIRS')
SCOPES = [(re.compile(r'SRC_DIRS|fp\.sources\('), 'src/'), (re.compile(r'fp\.SRC\s*/\s*[\'"](\w+)'), 'src/{}/'),
          (re.compile(r'fp\.SRC\b'), 'src/'), (re.compile(r'[\'"]src[\'"]\s*/\s*[\'"](\w+)[\'"]'), 'src/{}/'),
          (re.compile(r'[\'"]src[\'"]'), 'src/'), (re.compile(r'fp\.SHADERS|SHADER_DIRS'), 'shaders/'),
          (re.compile(r'fp\.GENERATED'), 'src/generated/'), (re.compile(r'fp\.GAMEDATA'), 'src/gamedata/'),
          (re.compile(r'fp\.COMPILED'), 'shaders/compiled/')]
PARENT = re.compile(r'fp\.src\(\s*[\'"]([\w./-]+)[\'"]\s*\)\.parent')
SUFFIX = re.compile(r"""['"](\.\w+)['"]""")
TEXT = {'.h', '.inl', '.inc', '.def', '.cpp', '.hlsl', '.json', '.py'}
SHARED = ('northlight_paths.py', 'scripts/run_tests.py', 'scripts/check_layout.py', 'tests/support/')
QUIET = ('.md', '.txt')   # documentation: nothing to guard, nothing to report
QUIET_NAMES = {'tests/discovery.txt', '.gitignore', 'LICENSE'}


def tokens(text):
    return {t.strip('.-') for t in TOKEN.findall(text)} - {''}


def git(*args):
    r = subprocess.run(['git', *args], cwd=fp.REPO, capture_output=True, text=True)
    return r.stdout.split('\n') if r.returncode == 0 else []


def changed_paths():
    bases = ('origin/main', 'main', 'origin/tideglass')
    if 'tideglass' in git('branch', '--show-current'):
        bases = ('origin/tideglass', 'origin/main', 'main')
    since = next((ref for base in bases for ref in git('merge-base', 'HEAD', base) if ref), None)
    names = [*git('diff', '--name-only'), *git('diff', '--cached', '--name-only'),
             *git('ls-files', '--others', '--exclude-standard')]
    if since:
        names += git('diff', '--name-only', f'{since}..HEAD')
    return sorted({n for n in names if n})


def relative(arg):
    p = Path(arg).resolve()
    return p.relative_to(fp.REPO).as_posix() if fp.REPO in p.parents else Path(arg).as_posix()


class Graph:
    def __init__(self):
        self.index = fp._build_index()
        self.cache = {}

    @cached_property
    def coverage(self):
        return {test: self.reach(test) for test in sorted(fp.TESTS.glob('test_*.py'))}

    def children(self, name):
        """Names a file reaches in turn: production files by #include, test-side files and manifests by any mention."""
        if name not in self.cache:
            p = self.index.get(name)
            out = set()
            if p is not None and p.suffix in TEXT:
                text = p.read_text(encoding='utf-8', errors='replace')
                if fp.TESTS in p.parents or p.suffix == '.json':
                    out = tokens(text)
                elif p.suffix != '.py':
                    out = {Path(i).name for i in INCLUDE.findall(text)}
            self.cache[name] = out
        return self.cache[name]

    def builds(self, name, text):
        """Does a test that names this file build it: a test-side file other than another test, a manifest, or a production .cpp/.hlsl the test passes along (fp.src('x.cpp') not followed by .read_text())."""
        p = self.index.get(name)
        if p is None:
            return False
        if fp.TESTS in p.parents:
            return not name.startswith('test_') or p.suffix != '.py'
        return p.suffix == '.json' or (p.suffix in ('.cpp', '.hlsl') and re.search(
            r'src\(\s*[\'"](?:[\w/]+/)?' + re.escape(name) + r'[\'"]\s*\)\s*[,\])]', text) is not None)

    def reach(self, test):
        """({name: what led to it}, fp.sources suffix lists) for everything the test names, directly or through what it builds."""
        text = test.read_text(encoding='utf-8', errors='replace')
        found = {t: test.name for t in tokens(text)}
        sibling = test.with_suffix('.cpp').name
        if sibling in self.index:
            found.setdefault(sibling, test.name)
        # A header the test only names (fp.src('x.h').read_text()) is scanned, not built: its includes
        # are followed only when the test #includes it in a harness string or builds it (a production .cpp passed on as an argument, a test-side file, a manifest).
        built = {Path(i).name for i in INCLUDE.findall(text)}
        queue = [n for n in found if n in built or self.builds(n, text)]
        while queue:
            n = queue.pop()
            for c in self.children(n):
                if c not in found:
                    found[c] = n
                    queue.append(c)
        return found, self.scans(text)

    def scans(self, text):
        """[(repo-relative directory prefix, suffixes or None)] for the trees the test walks."""
        lines, out = text.split('\n'), []
        for i, line in enumerate(lines):
            if not (WALK.search(line) or 'fp.sources(' in line):
                continue
            window = line + ' ' + (lines[i + 1] if i + 1 < len(lines) else '')
            suffixes = set(SUFFIX.findall(window)) | set(re.findall(r'\*(\.\w+)', window))
            for m in PARENT.finditer(line):
                p = self.index.get(Path(m.group(1)).name)
                if p is not None:
                    out.append((p.parent.relative_to(fp.REPO).as_posix() + '/', suffixes or None))
            for pattern, prefix in SCOPES:
                for m in pattern.finditer(line):
                    out.append((prefix.format(*m.groups()), suffixes or None))
        return out

    def covering(self, path):
        """[(test name, reason)] for one changed repo-relative path."""
        name = Path(path).name
        parts = {name, Path(path).stem, *Path(path).parts} if path.startswith('tests/fixtures/') else {name}
        if name.endswith('.py'):
            parts.add(Path(path).stem)
        hits = []
        for t, (found, scans) in self.coverage.items():
            if path == f'tests/{t.name}':
                hits.append((t.name, 'is the changed test'))
                continue
            hit = next((p for p in parts if p in found), None)
            if hit:
                chain, n = [hit], found[hit]
                while n != t.name:
                    chain.append(n)
                    n = found[n]
                hits.append((t.name, ' <- '.join([*chain, t.name])))
            else:
                scan = next((d for d, suf in scans if path.startswith(d) and (suf is None or Path(path).suffix in suf)), None)
                if scan:
                    hits.append((t.name, f'scans {scan}'))
        return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('paths', nargs='*', help='changed files (default: from git)')
    ap.add_argument('--why', action='store_true', help='show the reason chain after each test')
    ap.add_argument('--run', action='store_true', help='run the tests with scripts/run_tests.py')
    args = ap.parse_args()

    paths = [relative(a) for a in args.paths] or changed_paths()
    graph = Graph()
    selected = {}
    for path in paths:
        if path.startswith(SHARED):
            print(f'{path}: shared infrastructure, not mapped; run the full suite (python3 scripts/run_tests.py)', file=sys.stderr)
        hits = graph.covering(path)
        for name, why in hits:
            selected.setdefault(name, f'{path}: {why}')
        if not hits and not path.endswith(QUIET) and path not in QUIET_NAMES and not path.startswith(SHARED):
            print(f'{path}: no test covers it', file=sys.stderr)
    names = sorted(selected)
    for n in names:
        print(f'{n}  ({selected[n]})' if args.why else n)
    if args.run:
        runnable = [n for n in names if not re.search(r'^#\s*northlight-test:.*\bmanual\b', (fp.TESTS / n).read_text(
            encoding='utf-8', errors='replace'), re.M)]
        if len(runnable) < len(names):
            print(f'skipping manual tests: {", ".join(sorted(set(names) - set(runnable)))}', file=sys.stderr)
        if not runnable:
            print('no affected tests to run', file=sys.stderr)
            return 0
        sys.stdout.flush()
        os.execv(sys.executable, [sys.executable, str(fp.SCRIPTS / 'run_tests.py'), *runnable])
    return 0


if __name__ == '__main__':
    sys.exit(main())
