#!/usr/bin/env python3
"""Prints what a player reads and types in the installer, without a game or a real install.

    python3 scripts/dev_tuning/installer_transcript.py            # the Windows wording
    python3 scripts/dev_tuning/installer_transcript.py linux      # or mac

Runs the real installer (renderer/northlight_install.py) for the combined package against the stand-in game
folder and package of tests/test_northlight_install.py, in a scripted terminal: a first install with the folder
mistyped once, the same package again, a newer package with a changed dungeon and sky choice, an uninstall, and an
uninstall with nothing left. Typed answers are shown in «». The long steps (outdoor lighting, skies, fog) are the
tests' stand-ins, so this shows wording, order and what each prompt offers; it does not show real timings or
progress lines. Use it to read a change to the installer's text before a release rehearsal.
"""
import builtins
import importlib.util
import json
import shutil
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

    def session(self, title, answers, action):
        """One run in a terminal that types `answers`; prints prompts, answers and output in the order they appear."""
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
        print(f'\n{"=" * 100}\n{title}\n{"=" * 100}\n{text}')

    def test_transcript(self):
        (self.pkg_root / 'variants/stock.json').unlink()   # the combined package builds from the player's own files
        self.zip.unlink()
        self.make_water_package()
        client = str(self.client)
        self.session('1. First install; the folder is mistyped once', ['/nowhere/WoW', client, '', '', ''], 'install')
        self.session('2. The same package again', ['', ''], 'install')
        dll = tests.PROXY.replace(b'0.3.162', b'0.3.218')
        self.set_package_payload('d3d9.dll', dll)
        self.set_package_payload('water-core.dll', b'core 0.1.1')
        info = self.pkg_root / 'BUILD-INFO.json'
        info.write_text(json.dumps({**json.loads(info.read_bytes()), 'version': '0.3.218-tideglass-0.1.1', 'dll_version': '0.3.218',
                                    'dll_sha256': tests.sha(dll), 'tideglass': {'version': '0.1.1', 'core_sha256': tests.sha(b'core 0.1.1')}}))
        self.session('3. A newer package over the installed one; the dungeon and sky choices are changed', ['', 'c', '5', '2', ''], 'install')
        self.session('4. Uninstall', ['', ''], 'uninstall')
        self.session('5. Uninstall with nothing left', [client], 'uninstall')


if __name__ == '__main__':
    result = unittest.TextTestRunner(stream=open(Path('/dev/null') if Path('/dev/null').exists() else 'nul', 'w')).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(Transcript))
    sys.exit(0 if result.wasSuccessful() else 1)
