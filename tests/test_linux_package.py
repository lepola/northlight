#!/usr/bin/env python3
# northlight-test:
"""The Linux player package rules (renderer/build_packages.py, tests/verify_packages.py) without building one:
--platform all builds macOS, Windows and Linux; the Linux launchers are POSIX sh (sh -n and bash -n), use only
the package's Python and restore its executable bit; the runtime prune keeps bin/python3 and the stdlib and
drops libpython (linked into bin/python3), Tcl/Tk, terminfo, headers and _dbm; the licences are the macOS
ones shared by the same python-build-standalone release plus zlib, ncurses and libedit; the Linux payload is
the Windows one (d3d9.dll and both DXVK builds). When out/packages holds a Linux zip, verify_packages
accepts it and refuses a copy with a CRLF launcher, an unpruned runtime or a game archive. Nothing runs on
Linux here."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
import json
import shutil
import subprocess
import tempfile
import unittest
import zipfile

import build_packages as bp  # noqa: E402
import verify_packages as vp  # noqa: E402


class LinuxPackageRules(unittest.TestCase):
    def test_all_means_three_platforms(self):
        self.assertEqual(bp.PLATFORMS, {'mac': 'macOS', 'windows': 'Windows', 'linux': 'Linux'})
        self.assertEqual(bp.PROXY_PLATFORMS, ('windows', 'linux'))
        self.assertTrue((fp.RENDERER / 'linux-package/README.txt').is_file())

    def test_launchers(self):
        launchers = bp.launchers('linux', '0.3.202')
        self.assertEqual(sorted(launchers), ['install.sh', 'uninstall.sh'])
        for name, data in launchers.items():
            with self.subTest(name=name):
                self.assertTrue(data.startswith(b'#!/bin/sh\n') and b'\r' not in data)
                self.assertIn(b'--default-action ' + name[:-3].encode() + b' "$@"', data)
                self.assertIn(b'chmod u+x "$PY"', data)
                self.assertNotIn(b'BASH_SOURCE', data)   # `sh install.sh` runs dash on Debian/Ubuntu
                self.assertNotIn(b'read -r -p', data)
                for shell in ['sh', 'bash'] + (['dash'] if shutil.which('dash') else []):   # macOS ships /bin/dash
                    self.assertTrue(vp.shell_syntax(shell, data), shell)
                self.assertIn(b'uname -m', data)   # an ARM computer gets its own message
                self.assertTrue(vp.allowed(name, 'linux', launchers, {}))

    def test_runtime_prune(self):
        keep = ['bin/python3.13', 'lib/python3.13/os.py', 'lib/python3.13/ctypes/__init__.py',
                'lib/python3.13/lib-dynload/.empty', 'lib/python3.13/LICENSE.txt']
        drop = ['bin/pip3', 'bin/python3.13-config', 'include/python3.13/Python.h', 'share/terminfo/x/xterm',
                'lib/libpython3.13.so.1.0', 'lib/libpython3.so', 'lib/libtcl9.0.so', 'lib/libtcl9tk9.0.so',
                'lib/tcl9.0/init.tcl', 'lib/tk9.0/tk.tcl', 'lib/python3.13/tkinter/__init__.py',
                'lib/python3.13/lib-dynload/_tkinter.cpython-313-x86_64-linux-gnu.so',
                'lib/python3.13/lib-dynload/_dbm.cpython-313-x86_64-linux-gnu.so',
                'lib/python3.13/config-3.13-x86_64-linux-gnu/Makefile', 'lib/python3.13/site-packages/pip/x.py']
        self.assertEqual([p for p in keep if bp.LINUX_RUNTIME_DROP.match(p)], [])
        self.assertEqual([p for p in drop if not bp.LINUX_RUNTIME_DROP.match(p)], [])

    def test_licences(self):
        linux, mac = bp.python_licences('linux'), bp.python_licences('mac')
        self.assertEqual(set(linux), set(mac) | {'zlib', 'ncurses', 'libedit'})
        for name in ('zlib', 'ncurses', 'libedit'):
            pin = linux[name]
            self.assertEqual(pin['archive_sha256'], 'b806ebe660a0f7936f05fbcbc0ea4d98ec2fa3142e7b5c0805ffee06f18aad25')
            self.assertEqual(pin['member'], f'python/licenses/LICENSE.{name}.txt')
        runtime = bp.PINS['python_linux']
        self.assertEqual((runtime['version'], runtime['release']),
                         (bp.PINS['python_mac']['version'], bp.PINS['python_mac']['release']))   # one release, shared texts
        self.assertIn('x86_64-unknown-linux-gnu-install_only_stripped', runtime['file'])
        self.assertEqual(len(bp.PINS['stormlib']['linux_sha256']), 64)


def built_linux_zip():
    listed = fp.out() / 'packages/packages.json'
    try:
        packages = json.loads(listed.read_text())['packages']
    except (OSError, ValueError, KeyError):
        return None
    name = next((p['file'] for p in packages if p.get('platform') == 'linux'), None)
    path = listed.parent / name if name else None
    return path if path and path.is_file() else None


@unittest.skipUnless(built_linux_zip(), 'no Linux package in out/packages (renderer/build_packages.py --platform linux)')
class BuiltLinuxPackage(unittest.TestCase):
    def setUp(self):
        self.zip = built_linux_zip()

    def tampered(self, change):
        """A copy of the built zip with change(name, data) -> (name, data) or None applied to every member."""
        temp = Path(tempfile.mkdtemp(dir=fp.output_dir())) / self.zip.name
        with zipfile.ZipFile(self.zip) as src, zipfile.ZipFile(temp, 'w', zipfile.ZIP_DEFLATED) as dst:
            for info in src.infolist():
                result = change(info.filename.split('/', 1)[1], src.read(info))
                if result:
                    new = zipfile.ZipInfo(info.filename.split('/', 1)[0] + '/' + result[0], info.date_time)
                    new.external_attr = info.external_attr
                    dst.writestr(new, result[1])
        return vp.verify_installer(temp)['problems']

    def test_built_package_verifies(self):
        report = vp.verify_installer(self.zip)
        self.assertEqual(report['problems'], [])
        self.assertEqual(report['platform'], 'linux')

    def test_crlf_launcher_is_refused(self):
        problems = self.tampered(lambda n, d: (n, d.replace(b'\n', b'\r\n') if n == 'install.sh' else d))
        self.assertTrue(any(p.startswith('install.sh') for p in problems), problems)

    def test_unpruned_runtime_is_refused(self):
        problems = self.tampered(lambda n, d: ('runtime/lib/libpython3.13.so.1.0', d) if n == 'runtime/lib/python3.13/os.py' else (n, d))
        self.assertTrue(any('libpython' in p for p in problems), problems)
        problems = self.tampered(lambda n, d: ('runtime/lib/libtcl9.0.so', d) if n == 'runtime/lib/python3.13/abc.py' else (n, d))
        self.assertIn('Linux runtime not pruned', problems)

    def test_game_data_is_refused(self):
        problems = self.tampered(lambda n, d: ('payload/Data/patch-z.mpq', b'MPQ\x1a') if n == 'payload/celestial-profiles.ini' else (n, d))
        self.assertTrue(any('patch-z.mpq' in p for p in problems), problems)


if __name__ == '__main__':
    unittest.main()
