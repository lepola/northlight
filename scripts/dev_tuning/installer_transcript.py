#!/usr/bin/env python3
"""Prints what a player reads and types in the installer, without a game or a real install.

    python3 scripts/dev_tuning/installer_transcript.py            # the Windows wording
    python3 scripts/dev_tuning/installer_transcript.py linux      # or mac

Runs the real installer (renderer/northlight_install.py) for the combined package against the stand-in game
folder and package of tests/test_northlight_install.py, in a scripted terminal: a first install with the folder
mistyped once, the same package again, an update over the last release with a changed dungeon and sky choice, an
uninstall, and an uninstall with nothing left. The package carries the versions in this checkout's sources and the
last release is the newest v<northlight>-tideglass-<tideglass> tag. Typed answers are shown in «». The long steps (outdoor lighting, skies, fog) are the
tests' stand-ins, so this shows wording, order and what each prompt offers; it does not show real timings or
progress lines. Use it to read a change to the installer's text before a release rehearsal.
"""
import builtins
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))
spec = importlib.util.spec_from_file_location('installer_tests', REPO / 'tests/test_northlight_install.py')
tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tests)
fi = tests.fi
PLATFORM = next((a for a in sys.argv[1:] if a in ('windows', 'linux', 'mac')), 'windows')
CURRENT = (re.search(r'Northlight renderer ([0-9.]+);', (REPO / 'src/proxy/renderer.cpp').read_text(errors='replace')).group(1),
           re.search(r'TIDEGLASS_VERSION "([0-9.]+)"', (REPO / 'tideglass/src/version.h').read_text()).group(1))
PREVIOUS = re.fullmatch(r'v([0-9.]+)-tideglass-([0-9.]+)', subprocess.run(
    ['git', '-C', str(REPO), 'describe', '--tags', '--match', 'v*-tideglass-*', '--abbrev=0'],
    capture_output=True, text=True, check=True).stdout.strip()).groups()


class Transcript(tests.Base):
    platform = PLATFORM
    make_water_package = tests.Install.make_water_package

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        shutil.rmtree(self.base, ignore_errors=True)

    def fake_run(self, script, args, show=True):
        code, last = super().fake_run(script, args, show)
        # Like the real builder: outdoor lighting that is already built reports that it is up to date.
        built = getattr(self, 'lighting_built', False)
        if Path(script).name == 'install_world_cache.py' and not {'--plan', '--instances', '--instances-plan'} & set(map(str, args)):
            self.lighting_built = True
            if built:
                last = {'event': 'up_to_date'}
        return code, last

    def package(self, northlight, tideglass):
        """Makes the stand-in package the combined package of these two versions."""
        dll = tests.PROXY.replace(b'0.3.162', northlight.encode())
        core = b'core ' + tideglass.encode()
        if self.platform in fi.PROXY_PLATFORMS:
            self.set_package_payload('d3d9.dll', dll)
        else:   # macOS: the renderer is preloaded from mods/, so it is not among the files copied into the game folder
            (self.pkg_root / 'payload/d3d9.dll').write_bytes(dll)
        self.set_package_payload('water-core.dll', core)
        info = self.pkg_root / 'BUILD-INFO.json'
        info.write_text(json.dumps({**json.loads(info.read_bytes()), 'version': f'{northlight}-tideglass-{tideglass}',
                                    'dll_version': northlight, 'dll_sha256': tests.sha(dll),
                                    'tideglass': {'version': tideglass, 'core_sha256': tests.sha(core)}}))

    def session(self, title, answers, action):
        """One run in a terminal that types `answers`; prints prompts, answers and output in the order they appear
        (nothing for a title of None: a step that only sets the scene)."""
        typed = list(answers)
        terminal = type('Terminal', (), {'isatty': lambda self: True})()
        installer = self.installer()
        installer.memory = self.base / 'user' / 'installer.json'
        installer.ask = fi.Installer.ask.__get__(installer)   # the real questions, not the tests' canned answers

        def fake_input(prompt=''):
            answer = typed.pop(0) if typed else ''
            installer.out.write(f'{prompt}«{answer}»\n')
            return answer
        with patch.object(sys, 'stdin', terminal), patch.object(builtins, 'input', fake_input):
            try:
                if action == 'install':
                    installer.say(f'{installer.title} installer')
                    installer.install(None)
                else:
                    installer.uninstall(None)
            except fi.Refusal as e:
                installer.say('STOPPED:', e)
            except (fi.Failed, ValueError, OSError) as e:
                installer.say('ERROR:', e)
        text = installer.out.getvalue().replace(str(self.base), '~')
        if title:
            print(f'\n{"=" * 100}\n{title}\n{"=" * 100}\n{text}')

    def test_transcript(self):
        (self.pkg_root / 'variants/stock.json').unlink()   # the combined package builds from the player's own files
        self.zip.unlink()
        self.make_water_package()
        client = str(self.client)
        self.package(*CURRENT)
        self.session('1. First install; the folder is mistyped once', ['/nowhere/WoW', client, '', '', ''], 'install')
        self.session('2. The same package again', ['', ''], 'install')
        self.session(None, ['', ''], 'uninstall')
        self.lighting_built = False
        self.package(*PREVIOUS)
        self.session(None, [client, '', '', ''], 'install')
        self.package(*CURRENT)
        self.session('3. An update over the last release; the dungeon and sky choices are changed', ['', 'c', '5', '2', ''], 'install')
        self.session('4. Uninstall', ['', ''], 'uninstall')
        self.session('5. Uninstall with nothing left', [client], 'uninstall')


if __name__ == '__main__':
    result = unittest.TextTestRunner(stream=open(Path('/dev/null') if Path('/dev/null').exists() else 'nul', 'w')).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(Transcript))
    sys.exit(0 if result.wasSuccessful() else 1)
