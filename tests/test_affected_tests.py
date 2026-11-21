#!/usr/bin/env python3
# northlight-test:
"""scripts/affected_tests.py maps changed files to the tests that cover them (source only, no game,
compiler or GPU). Real repository cases: a header only some tests build selects those and not an
unrelated test; an #include chain is followed; a test selects itself; a test that scans every
source (fp.sources) is selected by any production header; docs select nothing."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS))
import affected_tests
import subprocess
import tempfile
from unittest.mock import patch

graph = affected_tests.Graph()


def covering(name):
    return {t for t, _ in graph.covering(fp.src(name).relative_to(fp.REPO).as_posix())}


spell = covering('spell_layer.h')
lock = covering('upload_lock.h')   # included by replay_copy_reader.h and draw_snapshot.h
checks = {
    'a header names its own test': 'test_spell_layer.py' in spell,
    'an unrelated test is not selected': 'test_ao_blur.py' not in spell and 'test_spell_layer.py' not in lock,
    # test_tracked_buffers.cpp includes replay_copy_reader.h, which includes upload_lock.h; the test names neither
    'a header included by a built header selects the test': 'test_tracked_buffers.py' in lock,
    'a header included through test-side native files selects the test': 'test_draw_snapshot.py' in lock,
    'a changed test selects itself': ('test_upload_locks.py', 'is the changed test') in graph.covering('tests/test_upload_locks.py'),
    'a source scanner is selected by any production header': 'test_upload_locks.py' in spell and 'test_upload_locks.py' in covering('world_gi.cpp'),
    'a shader selects the tests that read it': {'test_ao_blur.py', 'test_rain_mask.py'} <= covering('effects.hlsl'),
    'a test that walks src/stream selects on a header there': {'test_log_format_literals.py', 'test_gpu_budget_wiring.py'} <= covering('replay_thread.h'),
    'a test that walks the shaders folder selects on a shader': 'test_shadow_shimmer.py' in covering('effects.hlsl'),
    'a shader does not select a header-only test': 'test_spell_layer.py' not in covering('effects.hlsl'),
    'a file no test names selects nothing (outside the source scanners)': graph.covering('docs/unmapped' + '.xyz') == [],
    'the reason chain ends at the test': all(why.endswith(t) for t, why in graph.covering('src/proxy/upload_lock.h') if 'scans' not in why and 'is the changed' not in why),
}
with tempfile.TemporaryDirectory() as temp:
    repo = Path(temp)

    def git(*args):
        subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', *args],
                       cwd=repo, check=True, capture_output=True)

    git('init', '-b', 'tideglass')
    git('commit', '--allow-empty', '-m', 'published beta')
    git('update-ref', 'refs/remotes/origin/tideglass', 'HEAD')
    with patch.object(fp, 'REPO', repo):
        checks['a clean single-branch beta selects no changes'] = affected_tests.changed_paths() == []
        (repo / 'local.cpp').write_bytes(b'one')
        git('add', 'local.cpp')
        git('commit', '-m', 'local change')
        (repo / 'new.cpp').write_bytes(b'two')
        checks['the beta selects unpublished commits and untracked work'] = (
            affected_tests.changed_paths() == ['local.cpp', 'new.cpp'])
        git('update-ref', 'refs/remotes/origin/tideglass', 'HEAD')
        (repo / 'local.cpp').write_bytes(b'staged')
        git('add', 'local.cpp')
        (repo / 'local.cpp').write_bytes(b'one')
        checks['a staged change still counts when the working copy matches HEAD'] = (
            affected_tests.changed_paths() == ['local.cpp', 'new.cpp'])
for name, ok in checks.items():
    print(('ok   ' if ok else 'FAIL ') + name)
sys.exit(0 if all(checks.values()) else 1)
