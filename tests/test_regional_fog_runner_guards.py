#!/usr/bin/env python3
"""Portable negative controls for the manual regional fog runner; no GPU or compiler."""
import copy
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate_regional_fog_cache_gpu as runner


class Guards(unittest.TestCase):
    def test_argument_and_input_drift(self):
        expected = {'baseline_ref': 'original', 'source': 'candidate.hlsl', 'output': 'out',
                    'mpq': 'corpus', 'mpq_manifest_sha256': 'hash', 'fog_metadata': 'fog',
                    'shader_sources': {'main': 'hash'}, 'fog_inputs': {'tile': 'hash'}}
        runner.verify_identity(expected, copy.deepcopy(expected))
        for key in expected:
            with self.subTest(key=key):
                changed = copy.deepcopy(expected)
                changed[key] = 'changed'
                with self.assertRaises(RuntimeError):
                    runner.verify_identity(expected, changed)

    def test_source_executable_shader_and_fixture_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('source.cpp', 'fog-oracle.exe', 'build-fixture.exe', 'shader.hlsl', 'pose-0-depth.fgr'):
                with self.subTest(name=name):
                    target = root / name
                    target.write_bytes(b'original')
                    expected = runner.file_records(root, [name])
                    runner.verify_files(root, expected)
                    target.write_bytes(b'mutated!')
                    with self.assertRaises(RuntimeError):
                        runner.verify_files(root, expected)
                    target.unlink()
                    with self.assertRaises(RuntimeError):
                        runner.verify_files(root, expected)

    def test_source_superset_additions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            names = ('northlight_paths.py', 'scripts/build_environment.py',
                     'scripts/shaders/shader_compiler.py', 'scripts/shaders/compile_shaders.cpp',
                     'tests/regional_fog_cache_gpu.cpp', 'tests/build_regional_fog_fixture.cpp',
                     'tests/removal_distance_gpu.cpp', 'tests/validate_regional_fog_cache_gpu.py')
            for name in names:
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('fixture')
            original = runner.source_snapshot(root)
            (root / 'src/new-include.h').write_text('changed')
            self.assertNotEqual(original, runner.source_snapshot(root))

    def test_inventory_and_optional_water_injection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'pose-0-depth.fgr').write_bytes(b'original')
            expected = runner.file_records(root, ['pose-0-depth.fgr'])
            runner.verify_files(root, expected, exact=True)
            (root / 'pose-0-water.fgr').write_bytes(b'injected')
            with self.assertRaises(RuntimeError):
                runner.verify_files(root, expected, exact=True)

    def test_external_path_and_nonempty_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / 'out'
            runner.fresh_directory(output)
            (root / 'outside').write_text('external')
            with self.assertRaises(RuntimeError):
                runner.file_records(output, ['../outside'])
            (output / 'stale.exe').write_text('old')
            with self.assertRaises(RuntimeError):
                runner.fresh_directory(output)

    def test_build_phase_rejects_incomplete_source_and_executable_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('fog-oracle.exe', 'build-fixture.exe', 'zig.exe'):
                (root / name).write_bytes(b'original')
            report = {'compile_complete': True, 'source_snapshot': {'unit.cpp': 'frozen'},
                      'artifacts': runner.file_records(root, ['fog-oracle.exe', 'build-fixture.exe']),
                      'shader_inputs': {}, 'shaders': {},
                      'zig': {'path': str(root / 'zig.exe'), 'sha256': runner.sha(root / 'zig.exe')}}
            with patch.object(runner, 'source_snapshot', return_value={'unit.cpp': 'frozen'}):
                runner.verify_build(root, report)
                report['compile_complete'] = False
                with self.assertRaises(RuntimeError):
                    runner.verify_build(root, report)
                report['compile_complete'] = True
                for name in ('fog-oracle.exe', 'build-fixture.exe', 'zig.exe'):
                    (root / name).write_bytes(b'mutated!')
                    with self.assertRaises(RuntimeError):
                        runner.verify_build(root, report)
                    (root / name).write_bytes(b'original')
            with patch.object(runner, 'source_snapshot', return_value={'unit.cpp': 'changed'}):
                with self.assertRaises(RuntimeError):
                    runner.verify_build(root, report)

    def test_mpq_omitted_input_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('terrain.bin', 'camera.bin', 'camera.json'):
                (root / name).write_bytes(b'fixture')
            manifest = {'missing_tiles': [], 'missing_assets': [], 'unsupported': [],
                        'mpq_view': {'unreadable': []},
                        'outputs': runner.file_records(root, ['terrain.bin', 'camera.bin', 'camera.json'])}
            (root / 'manifest.json').write_text(runner.json.dumps(manifest))
            runner.verify_mpq(root)
            (root / 'world-cache').mkdir()
            (root / 'world-cache/injected.fg3').write_bytes(b'unlisted')
            with self.assertRaises(RuntimeError):
                runner.verify_mpq(root)

    def test_disabled_assertions_refused_before_platform_or_tools(self):
        result = subprocess.run([sys.executable, '-B', '-O', runner.__file__], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('do not run this oracle with python -O', result.stderr)


if __name__ == '__main__':
    unittest.main()
