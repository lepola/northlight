#!/usr/bin/env python3
# northlight-test:
"""The developer tuning build never reaches a player package: scripts/build_renderer.py adds -DNORTHLIGHT_DEV_TUNING=1
only with --dev-tuning (the default zig command is unchanged), and renderer/build_packages.py check_dll refuses a DLL
carrying the NORTHLIGHT DEV TUNING BUILD marker (src/sky/zone_tuning.h Marker, logged at startup by the dev DLL).
Nothing is compiled or run here: the zig call is replaced by a recorder."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
import re
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import build_packages as bp  # noqa: E402
import build_renderer as br  # noqa: E402


def fake_dll(extra=b''):
    pe = 0x80
    data = bytearray(0x200)
    data[0:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, pe)
    data[pe:pe + 4] = b'PE\0\0'
    struct.pack_into('<H', data, pe + 4, 0x14c)
    return bytes(data) + b'Northlight renderer 0.3.215;\0PROXY module=%ls root=%ls\0' + extra


class DevTuningBuild(unittest.TestCase):
    def zig_command(self, argv):
        fp.src('renderer.cpp')  # build the source index before subprocess.run is replaced
        calls = []
        with mock.patch.object(br.subprocess, 'run', side_effect=lambda cmd, **kw: calls.append(cmd)), \
                mock.patch.object(br, 'check_compiler'), \
                mock.patch.object(br.check_layout, 'layout_problems', return_value=[]), \
                mock.patch.object(br.northlight_paths, 'zig', return_value=Path('zig')), \
                mock.patch.object(br.northlight_paths, 'zig_env', return_value={}), \
                mock.patch.object(br, 'datetime') as build_clock, \
                mock.patch('builtins.print'):
            build_clock.now.return_value.strftime.return_value = '2026-01-01 00:00:00 UTC'
            br.main(argv)
        zig = [c for c in calls if c and c[0] == 'zig']
        self.assertEqual(len(zig), 1)
        return zig[0]

    def test_define_only_with_the_flag(self):
        default = self.zig_command([])
        dev = self.zig_command(['--dev-tuning'])
        self.assertFalse([a for a in default if 'DEV_TUNING' in a], 'the default build gets no tuning define')
        self.assertIn('-DNORTHLIGHT_DEV_TUNING=1', dev)
        dev.remove('-DNORTHLIGHT_DEV_TUNING=1')
        self.assertEqual(dev[:-1], default[:-1], 'the flag changes nothing else but the output')
        self.assertEqual(Path(default[-1]).name, 'frd9.dll')
        self.assertEqual(Path(dev[-1]).name, 'frd9-dev-tuning.dll', 'the dev build never overwrites frd9.dll')

    def test_marker_matches_the_source_constant(self):
        header = fp.src('zone_tuning.h').read_text()
        self.assertEqual(re.search(r'Marker\[\]="([^"]+)"', header).group(1).encode(), bp.DEV_MARKER)

    def test_windows_preflight_reports_sandbox_failure_before_build(self):
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(br.check_layout, 'layout_problems', return_value=[]), \
                mock.patch.object(br.northlight_paths, 'out', return_value=Path(folder)), \
                mock.patch.object(br.northlight_paths, 'zig', return_value=Path('zig')), \
                mock.patch.object(br.northlight_paths, 'zig_env', return_value={}), \
                mock.patch.object(br.sys, 'platform', 'win32'), \
                mock.patch.object(br.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 1, '', 'probe.cpp:1:1: error: Unexpected\n')) as run:
            with self.assertRaisesRegex(SystemExit, 'normal approval mechanism'):
                br.main([])
            run.assert_called_once()
            self.assertIn('error: Unexpected', (Path(folder) / 'build-preflight.log').read_text())
            self.assertIn('-c', run.call_args.args[0])
            run.return_value = subprocess.CompletedProcess([], 0, '', '')
            br.check_compiler(Path('zig'), {'TEST': 'value'})
            self.assertEqual(run.call_args.kwargs['env'], {'TEST': 'value'})

    def test_package_builder_refuses_the_dev_dll(self):
        self.assertEqual(bp.check_dll(fake_dll()), '0.3.215')
        with self.assertRaises(SystemExit) as refused:
            bp.check_dll(fake_dll(b'TUNING ' + bp.DEV_MARKER + b': x\0'))
        self.assertIn('DEV TUNING BUILD', str(refused.exception))


if __name__ == '__main__':
    unittest.main()
