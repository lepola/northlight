#!/usr/bin/env python3
# northlight-test:
"""The player installer (renderer/northlight_install.py) on fake clients and a fake package: every preflight
refusal writes nothing; a stock client gets the prebuilt cache (sha-checked, resumable after a kill), any
other client a local build; the art layer is installed unless a foreign patch-z is there; a rerun changes
nothing; uninstall restores the client file list and deletes only our cache; wow.exe is left
unchanged; an old full Windows package is restored first. The pipeline scripts are replaced by a fake
runner and client identification by a stub. No game, Wine or Windows binary runs."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import unittest
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('northlight_install', fp.src('northlight_install.py'))
fi = importlib.util.module_from_spec(spec); spec.loader.exec_module(fi)
INSTALL = fi.INSTALL
mig = fi.migrate()
sha = lambda b: hashlib.sha256(b).hexdigest()
PROXY = b'MZ Northlight renderer 0.3.162; PROXY module=%ls root=%ls'
DXVK = b'MZ DXVK: \0\0\0v1.10.3-20230507-async (macOS)\0'
CACHE_FILES = {'Azeroth/32_48.fg3': b'tile' * 50, 'models/0123.fgs': b'model' * 99, 'fog/Azeroth/32_48.frf': b'fog',
               'lights/Azeroth.fgl': b'lights', 'celestial/sun.fct': b'sun'}
ART = b'MPQ\x1a fake art layer'
BASE = ['common', 'common-2', 'expansion', 'lichking', 'patch', 'patch-2', 'patch-3']


def tree(root):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob('*')
            if p.is_file() and 'renderer-backups' not in p.relative_to(root).parts}


def variant_manifest(files):
    rows = ''.join(f'{n}\0{sha(d)}\0{len(d)}\n' for n, d in sorted(files.items()))   # build_cache_variant.cache_digest
    return {'format': fi.VARIANT_FORMAT, 'variant': 'stock', 'cache_digest': sha(rows.encode()),
            'identity': {'chain': []}, 'files': {n: {'sha256': sha(d), 'bytes': len(d)} for n, d in files.items()}}


class Base(unittest.TestCase):
    platform = 'mac'

    def setUp(self):
        self.base = fp.output_dir() / self.id().rsplit('.', 2)[-2] / self._testMethodName
        shutil.rmtree(self.base, ignore_errors=True)
        self.client = self.make_client(self.base / 'Games' / 'WoW 3.3.5a')
        self.pkg_root = self.make_package(self.base / 'Downloads' / 'Northlight-0.3.162-test')
        self.manifest = variant_manifest(CACHE_FILES)
        (self.pkg_root / 'variants/stock.json').write_text(json.dumps(self.manifest))
        self.zip = self.pkg_root.parent / f'Northlight-cache-stock-{self.manifest["cache_digest"][:12]}.zip'
        with zipfile.ZipFile(self.zip, 'w', zipfile.ZIP_DEFLATED) as z:
            for n, d in CACHE_FILES.items():
                z.writestr(n, d)
            z.writestr('northlight-cache.json', json.dumps(self.manifest))
        self.calls, self.answers, self.variant, self.identified = [], [], 'stock', []
        self.archive_warnings = []   # what the fake install_world_cache records as tolerated
        self.patches = [patch.object(mig, 'VERSIONS', self.base / 'no-versions.json'),
                        patch.object(mig, 'running', lambda: []),
                        # The scratch path is long; the Windows limit is kept relative to it.
                        patch.object(fi, 'WINDOWS_MAX_PATH', len(str(self.client)) + fi.LONGEST_CACHE_NAME + 40)]
        for p in self.patches:
            p.start()
        self.before = tree(self.client)

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()

    def make_client(self, client):
        data = client / 'Data'; (data / 'enUS').mkdir(parents=True)
        (client / 'wow.exe').write_bytes(b'any wow.exe')
        for n in BASE:
            (data / f'{n}.mpq').write_bytes(b'MPQ ' + n.encode())
        for n in ['locale-enUS', 'patch-enUS', 'patch-enUS-2', 'patch-enUS-3']:
            (data / 'enUS' / f'{n}.mpq').write_bytes(b'MPQ ' + n.encode())
        if self.platform == 'mac':
            for n, d in {'d3d9.dll': DXVK, 'libDllLdr.dll': b'ldr', 'DivxDecoder.dll': b'divx patched',
                         'DivxDecoder.dll.bak': b'divx', 'dlls.txt': b'mods/winerosetta.dll\n'}.items():
                (client / n).write_bytes(d)
            (client / 'mods').mkdir(); (client / 'mods/winerosetta.dll').write_bytes(b'wr')
        return client

    def make_package(self, root):
        (root / 'payload').mkdir(parents=True); (root / 'variants').mkdir()
        files = {'celestial-profiles.ini': b'[Sun]\n', 'shadow-range-profiles.ini': b'[Range]\n',
                 'northlight-quality.ini': b'[Quality]\nPreset=Quality\n'}
        if self.platform == 'windows':
            files.update({'d3d9.dll': PROXY, 'renderer-backends/dxvk/dxvk_d3d9.dll': b'MZ DXVK: \0v3.1.1\0',
                          'renderer-backends/dxvk2/dxvk2_d3d9.dll': b'MZ DXVK: \0v2.7.1\0'})
        (root / 'payload/d3d9.dll').write_bytes(PROXY)
        for n, d in files.items():
            p = root / 'payload' / n; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(d)
        (root / 'payload-manifest.json').write_text(json.dumps(
            [{'path': n, 'sha256': sha(d), **({'preserve': True} if n == 'northlight-quality.ini' else {})}
             for n, d in files.items()]))
        (root / 'BUILD-INFO.json').write_text(json.dumps({'version': '0.3.162-test', 'dll_sha256': sha(PROXY)}))
        return root

    @property
    def last_output(self):
        return self.outputs[-1].getvalue()

    def installer(self, **kw):
        self.outputs = getattr(self, 'outputs', []) + [io.StringIO()]
        inst = fi.Installer(fi.Package(self.pkg_root, self.platform), self.platform, out=self.outputs[-1], **kw)
        inst.running = lambda: []
        inst.free_bytes = lambda path: 1 << 50
        inst.ram = lambda: 16 << 30
        inst.search_dirs = [self.pkg_root.parent]
        inst.identify_chain = self.fake_identify
        inst.run = self.fake_run
        inst.ask = lambda question, default: self.answers.pop(0) if self.answers else default
        return inst

    def fake_identify(self, client, locale, without='z'):
        # Like client_identity: an archive left in the chain (a foreign z when without='') breaks the match.
        self.identified.append(without)
        if without == '' and (client / 'Data/patch-z.mpq').exists():
            return None, ['extra Data/patch-z.mpq']
        return self.variant, [] if self.variant else ['extra Data/patch-y.mpq']

    def fake_run(self, script, args, show=True):
        args = [str(a) for a in args]
        opt = lambda name: args[args.index(name) + 1]
        self.calls.append((Path(script).name, args))
        if Path(script).name == 'build_art_layer.py':
            out = Path(opt('--output'))
            targets = ['Data/patch-z.mpq', 'Data/enUS/patch-enus-z.mpq']
            for t in targets:
                (out / t).parent.mkdir(parents=True, exist_ok=True); (out / t).write_bytes(ART)
            (out / 'art-layer-manifest.json').write_text(json.dumps({'targets': targets, 'steps': [
                {'step': 'relighting'}, {'step': 'mulgore'}, {'step': 'outdoor_sun', 'skipped': 'no HD sky'}]}))
            return 0, {'targets': targets}
        if Path(script).name == 'install_world_cache.py':
            out = Path(opt('--output'))
            (out / 'fog').mkdir(parents=True, exist_ok=True)
            (out / 'install-manifest.json').write_text(json.dumps({'format': fi.LOCAL_FORMAT, 'fingerprint': 'f',
                                                                   'tolerated': {'archive_warnings': self.archive_warnings}}))
            return 0, {'event': 'done'}
        raise AssertionError(script)

    def assert_untouched(self):
        self.assertEqual(tree(self.client), self.before)
        self.assertFalse((self.client / 'renderer-backups').exists())

    def backups(self):
        return sorted((self.client / 'renderer-backups').rglob('transaction.json'))


class Preflight(Base):
    def refused(self, inst=None, **kw):
        with self.assertRaises(fi.Refusal) as e:
            (inst or self.installer()).install(self.client, **kw)
        self.assert_untouched()
        return str(e.exception)

    def test_package_inside_client(self):
        self.pkg_root = self.make_package(self.client / 'Northlight')
        (self.pkg_root / 'variants/stock.json').write_text(json.dumps(self.manifest))
        self.before = tree(self.client)
        self.assertIn('inside the game folder', self.refused())

    def test_missing_base_archive(self):
        (self.client / 'Data/patch-3.mpq').unlink(); self.before = tree(self.client)
        self.assertIn('patch-3.mpq', self.refused())

    def test_ambiguous_locale(self):
        (self.client / 'Data/deDE').mkdir(); (self.client / 'Data/deDE/locale-deDE.mpq').write_bytes(b'x')
        self.before = tree(self.client)
        self.assertIn('--locale', self.refused())

    def test_non_ascii_path(self):
        moved = self.base / 'Spiel-Äpfel' / 'WoW'
        moved.parent.mkdir(parents=True); self.client.rename(moved); self.client = moved
        self.assertIn('non-ASCII', self.refused())

    def test_game_running(self):
        inst = self.installer(); inst.running = lambda: ['wow.exe']
        self.assertIn('wow.exe', self.refused(inst))

    def test_low_disk(self):
        inst = self.installer(); inst.free_bytes = lambda path: 1 << 20
        self.assertIn('disk space', self.refused(inst))

    def test_low_memory_refuses_local_build(self):
        self.variant = None
        inst = self.installer(); inst.ram = lambda: 4 << 30
        self.assertIn('8 GB', self.refused(inst))

    def test_memory_rule_is_the_orchestrators(self):
        # installed RAM >= 7.0 GiB passes (an "8 GB" PC reporting 7.2 GiB), as install_world_cache decides
        self.variant = None
        inst = self.installer(); inst.ram = lambda: int(7.2 * (1 << 30))
        self.assertIsNone(inst.memory_refusal(self.client))
        # a cache install_world_cache built may need only an incremental rebuild: the front end does not refuse
        (self.client / 'world-cache').mkdir()
        (self.client / 'world-cache/install-manifest.json').write_text(json.dumps({'format': fi.LOCAL_FORMAT}))
        inst.ram = lambda: 4 << 30
        self.assertIsNone(inst.memory_refusal(self.client))

    def test_damaged_package(self):
        (self.pkg_root / 'payload/celestial-profiles.ini').write_bytes(b'changed')
        self.assertIn('damaged', self.refused())

    def test_mac_preload_missing(self):
        (self.client / 'libDllLdr.dll').unlink(); self.before = tree(self.client)
        self.assertIn('preload', self.refused())


class MacFileSets(unittest.TestCase):
    def test_mac_package_payload_and_migration_files_are_disjoint(self):
        """INSTALL.chain_problems skips migration records: safe only while no macOS package record holds a
        file migrate_mac_proxy writes (the mac payload is PAYLOAD_COMMON; d3d9.dll goes to mods/ by migration)."""
        import ast
        tree_ = ast.parse(fp.src('build_packages.py').read_text(encoding='utf-8'))
        common = next(ast.literal_eval(n.value) for n in tree_.body if isinstance(n, ast.Assign) and
                      any(getattr(t, 'id', None) == 'PAYLOAD_COMMON' for t in n.targets))
        self.assertTrue(common)
        self.assertFalse(set(common) & set(mig.MANAGED))


class RunningCheck(unittest.TestCase):
    """migrate_mac_proxy.running(): our own launcher chain never counts, whatever its paths contain."""
    LAUNCHER = [(1, 0, '/sbin/launchd', '/sbin/launchd'),
                (50, 1, '/Sys/Utilities/Terminal.app/Contents/MacOS/Terminal', 'Terminal'),
                (60, 50, '/bin/bash', 'bash /Volumes/Games/WoWSilicon stuff/wineserver/Northlight/Install Northlight.command '
                                      '--client /Volumes/Games/WoWSilicon games/WoW'),
                (61, 60, '/Volumes/Games/WoWSilicon stuff/Northlight/runtime/bin/python3',
                 'runtime/bin/python3 -I -B -X utf8 app/renderer/northlight_install.py --client "/Volumes/Games/WoWSilicon games/wow.exe"')]

    def test_launcher_with_wowsilicon_in_its_paths_is_not_busy(self):
        self.assertEqual(mig.busy(self.LAUNCHER, 61), [])

    def test_real_processes_are_busy(self):
        procs = self.LAUNCHER + [
            (70, 1, '/Apps/WoWSilicon.app/Contents/MacOS/WoWSilicon', '/Apps/WoWSilicon.app/Contents/MacOS/WoWSilicon'),
            (71, 70, '/Apps/WoWSilicon.app/Contents/Resources/Wine/bin/wineserver', 'wineserver'),
            (72, 70, '/Apps/WoWSilicon.app/Contents/Resources/Wine/bin/wine64-preloader', 'C:\\Games\\WoW\\Wow.exe')]
        self.assertEqual(mig.busy(procs, 61), ['WoWSilicon', 'wineserver', 'wow.exe'])
        # a tool (not our ancestor) that only mentions WoWSilicon in its arguments is not WoWSilicon
        self.assertEqual(mig.busy(self.LAUNCHER + [(80, 1, '/usr/bin/less', 'less /Volumes/Games/WoWSilicon.log')], 61), [])

    def test_this_host_runs(self):
        self.assertIsInstance(mig.running(), list)


class WindowsPreflight(Preflight):
    platform = 'windows'
    test_mac_preload_missing = None

    def test_path_length(self):
        deep = self.base / ('x' * 60) / ('y' * 60) / 'WoW'
        deep.parent.mkdir(parents=True); self.client.rename(deep); self.client = deep
        self.assertIn('too long', self.refused())


class Install(Base):
    def test_stock_match_extracts_and_rerun_is_noop(self):
        report = self.installer().install(self.client)
        self.assertTrue(report['world_cache'].startswith('extracted'))
        self.assertEqual(report['art_layer'], f'installed {sha(ART)[:8]} Data/patch-z.mpq; '
                                              f'installed {sha(ART)[:8]} Data/enUS/patch-enus-z.mpq')
        self.assertTrue(report['payload'].startswith('installed'))
        self.assertIn('Art layer: built from this client\'s Light tables (2 steps applied, 1 skipped',
                      self.last_output)
        cache = self.client / 'world-cache'
        self.assertEqual({n: (cache / n).read_bytes() for n in CACHE_FILES}, CACHE_FILES)
        self.assertEqual(json.loads((cache / 'northlight-cache.json').read_text())['cache_digest'], self.manifest['cache_digest'])
        self.assertFalse((self.client / 'world-cache.staging').exists())
        self.assertEqual((self.client / 'Data/patch-z.mpq').read_bytes(), ART)
        self.assertEqual((self.client / 'Data/enUS/patch-enus-z.mpq').read_bytes(), ART)
        self.assertEqual((self.client / ('mods/d3d9.dll' if self.platform == 'mac' else 'd3d9.dll')).read_bytes(), PROXY)
        self.assertEqual((self.client / 'wow.exe').read_bytes(), b'any wow.exe')
        self.assertEqual([c[0] for c in self.calls], ['build_art_layer.py'])
        art = [a for n, a in self.calls if n == 'build_art_layer.py'][0]
        self.assertFalse(Path(art[art.index('--output') + 1]).is_relative_to(self.client))
        backups, after = self.backups(), tree(self.client)
        inst = self.installer(); report = inst.install(self.client)
        self.assertTrue(report['world_cache'].startswith('kept'))
        self.assertEqual((report['backups'], report['payload']), ([], 'kept (unchanged)'))
        self.assertIn(f'kept-identical {sha(ART)[:8]}', report['art_layer'])
        self.assertIn('already installed; nothing changed', inst.out.getvalue())
        self.assertEqual((self.backups(), tree(self.client)), (backups, after))

    def test_no_match_builds_locally(self):
        self.variant = None
        report = self.installer().install(self.client)
        name, args = self.calls[0]
        self.assertEqual(name, 'install_world_cache.py')
        self.assertEqual(args[args.index('--without') + 1], 'z')
        self.assertEqual(args[args.index('--progress') + 1], 'human')
        self.assertEqual(Path(args[args.index('--output') + 1]), self.client / 'world-cache')
        self.assertEqual(report['world_cache'], 'built from this client')
        self.assertEqual((self.client / 'Data/patch-z.mpq').read_bytes(), ART)

    def test_unlisted_archive_warning_is_repeated_in_the_summary(self):
        # A private-server patch without a (listfile): the build passes, the summary names the archive.
        self.variant = None
        self.archive_warnings = [{'archive': 'Data/patch-R.mpq', 'problem': 'unlisted'}]
        inst = self.installer()
        with (self.base / 'install.log').open('w') as inst.log:
            report = inst.install(self.client)
        self.assertEqual(report['archive_warnings'], self.archive_warnings)
        warning = 'Warning: Data/patch-R.mpq has no (listfile), so its files could not be listed.'
        console = inst.out.getvalue()
        self.assertIn('    ' + warning, console)
        self.assertIn(warning, (self.base / 'install.log').read_text())
        self.assertLess(console.index(warning), console.index('Start WoW yourself'))
        self.assertEqual((self.client / 'Data/patch-z.mpq').read_bytes(), ART)   # the installation went on
        self.archive_warnings = []
        self.assertEqual(self.installer().install(self.client)['archive_warnings'], [])

    def test_summary_reports_a_new_cache_although_the_renderer_was_current(self):
        self.installer().install(self.client)
        shutil.rmtree(self.client / 'world-cache')
        inst = self.installer(); report = inst.install(self.client)
        self.assertEqual(report['backups'], [])
        self.assertIn('0.3.162-test: installed.', inst.out.getvalue())

    def test_child_reports_go_to_the_log_only(self):
        inst = self.installer()
        del inst.run   # the real runner, on a tiny script
        script = self.base / 'child.py'
        script.write_text('print("Azeroth: 5% (1/20 tiles)")\nprint("Azeroth: 10% (2/20 tiles)")\n'
                          'print("{\\"steps\\": 1}")\nprint("  \\"profile\\": 3")\n')
        log = self.base / 'child.log'
        with log.open('w') as inst.log:
            code, last = inst.run(script, [])
        self.assertEqual((code, last), (0, {'steps': 1}))
        self.assertEqual(inst.out.getvalue(), '  Azeroth: 10% (2/20 tiles)\n')
        self.assertIn('"profile": 3', log.read_text())
        inst.log = None
        code, _ = inst.run(script, [], show=False)
        self.assertEqual(inst.out.getvalue(), '  Azeroth: 10% (2/20 tiles)\n')

    def test_stock_without_zip_builds_locally(self):
        self.zip.unlink()
        self.installer().install(self.client)
        self.assertEqual(self.calls[0][0], 'install_world_cache.py')

    def test_expanded_folder_and_cache_argument(self):
        folder = self.base / 'elsewhere' / 'expanded'
        with zipfile.ZipFile(self.zip) as z:
            z.extractall(folder)
        self.zip.unlink()
        self.installer().install(self.client, cache=folder)
        self.assertEqual((self.client / 'world-cache/models/0123.fgs').read_bytes(), CACHE_FILES['models/0123.fgs'])

    def test_foreign_patch_z_is_part_of_the_client(self):
        (self.client / 'Data/patch-z.mpq').write_bytes(b'someone else'); self.before = tree(self.client)
        report = self.installer().install(self.client)
        self.assertTrue(report['art_layer'].startswith('skipped'))
        # identified and built WITH the foreign z (no stock cache for a client that loads it); no art layer build
        self.assertEqual(self.identified, [''])
        self.assertEqual([n for n, _ in self.calls], ['install_world_cache.py'])
        args = self.calls[0][1]
        self.assertEqual(args[args.index('--without') + 1], '')
        self.assertEqual((self.client / 'Data/patch-z.mpq').read_bytes(), b'someone else')
        self.assertFalse((self.client / 'Data/enUS/patch-enus-z.mpq').exists())
        paths = [e['path'] for b in self.backups() for e in json.loads(b.read_text())['files']]
        self.assertFalse([p for p in paths if p.endswith('.mpq')])

    def test_killed_extraction_resumes(self):
        real, opened = fi.Source.open, []

        def dies(source, name):
            opened.append(name)
            if len(opened) == 3:
                raise KeyboardInterrupt
            return real(source, name)
        with patch.object(fi.Source, 'open', dies):
            with self.assertRaises(KeyboardInterrupt):
                self.installer().install(self.client)
        self.assertTrue((self.client / 'world-cache.extract/.northlight-extract.json').exists())
        self.assertFalse((self.client / 'world-cache').exists())
        # install_world_cache's resumable staging is not ours to delete
        build = self.client / 'world-cache.staging'; build.mkdir(); (build / 'install-state.json').write_text('{}')
        self.installer().install(self.client)
        cache = self.client / 'world-cache'
        self.assertEqual({n: (cache / n).read_bytes() for n in CACHE_FILES}, CACHE_FILES)
        self.assertFalse((self.client / 'world-cache.extract').exists())
        self.assertTrue((build / 'install-state.json').exists())
        self.installer().uninstall(self.client)   # uninstall removes our leftovers, including that staging
        self.assertFalse(build.exists())

    def test_damaged_zip_member_fails(self):
        with zipfile.ZipFile(self.zip, 'w') as z:
            for n, d in CACHE_FILES.items():
                z.writestr(n, d if n != 'models/0123.fgs' else b'bad')
            z.writestr('northlight-cache.json', json.dumps(self.manifest))
        with self.assertRaises(fi.Failed):
            self.installer().install(self.client)
        self.assertFalse((self.client / 'world-cache').exists())
        self.assertFalse(list((self.client / 'world-cache.extract').rglob('*.part')))

    def test_uninstall_restores_file_list(self):
        self.installer().install(self.client)
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)
        self.assertFalse((self.client / 'world-cache').exists())

    def version_package(self, n):
        """A copy of the package whose renderer DLL and celestial-profiles.ini differ (on macOS the package
        records hold only the ini files, so the DLL alone would not stack)."""
        root = self.base / 'Downloads' / f'Northlight-v{n}'
        shutil.copytree(self.pkg_root, root)
        dll, ini = PROXY + b' v%d' % n, b'[Sun]\nversion=%d\n' % n
        (root / 'payload/d3d9.dll').write_bytes(dll); (root / 'payload/celestial-profiles.ini').write_bytes(ini)
        manifest = json.loads((root / 'payload-manifest.json').read_text())
        for e in manifest:
            data = {'d3d9.dll': dll, 'celestial-profiles.ini': ini}.get(e['path'])
            if data is not None:
                e['sha256'] = sha(data)
        (root / 'payload-manifest.json').write_text(json.dumps(manifest))
        (root / 'BUILD-INFO.json').write_text(json.dumps({'version': f'0.3.{n}', 'dll_sha256': sha(dll)}))
        return root

    def stack(self, n=3):
        for i in range(1, n + 1):
            self.pkg_root = self.version_package(160 + i)
            self.installer().install(self.client, world_cache=False)

    def test_uninstall_after_three_stacked_installs(self):
        self.stack()
        self.assertGreaterEqual(len([b for b, r in self.installer().all_transactions(self.client) if r.get('kind', 'package') == 'package']), 3)
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)

    def stacked_state(self):
        return tree(self.client), [json.loads(p.read_text())['status'] for p in sorted(self.client.glob('renderer-backups/**/transaction.json'))]

    def test_uninstall_refused_after_later_edit(self):
        self.stack()
        (self.client / 'celestial-profiles.ini').write_bytes(b'user edit')
        state = self.stacked_state()
        with self.assertRaises(fi.Refusal) as cm:
            self.installer().uninstall(self.client)
        self.assertIn('Nothing was changed', str(cm.exception))
        self.assertEqual(self.stacked_state(), state)
        self.assertNotIn('restored', state[1])

    def test_uninstall_refused_with_damaged_backup(self):
        self.stack()
        # The first install had nothing to back up; the second one's before/ holds the first one's files.
        damaged = [p for b, r in reversed(self.installer().all_transactions(self.client)) if r.get('kind', 'package') == 'package'
                   for p in (b / 'before').rglob('*') if p.is_file()]
        self.assertTrue(damaged); damaged[0].write_bytes(b'bad')
        state = self.stacked_state()
        with self.assertRaises(fi.Refusal) as cm:
            self.installer().uninstall(self.client)
        self.assertIn('Backup damaged', str(cm.exception))
        self.assertEqual(self.stacked_state(), state)

    def test_uninstall_keeps_foreign_cache(self):
        (self.client / 'world-cache/dev').mkdir(parents=True); (self.client / 'world-cache/dev/x.fg3').write_bytes(b'dev')
        self.before = tree(self.client)
        self.answers = [False]
        with self.assertRaises(fi.Refusal):   # replacing a cache we did not make needs a yes
            self.installer().install(self.client)
        self.assert_untouched()
        self.installer().install(self.client, world_cache=False)
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)

    def test_game_started_during_the_install_stops_before_writing(self):
        calls = []

        def running():   # closed at the preflight, running from the second check on
            calls.append(1)
            return ['wow.exe'] if len(calls) > 2 else []
        inst = self.installer(); inst.running = running
        with self.assertRaises(fi.Failed):
            inst.install(self.client)
        self.assertTrue((self.client / 'world-cache.extract/northlight-cache.json').exists())   # complete, kept for resume
        self.assertFalse((self.client / 'world-cache').exists())
        self.assertFalse((self.client / 'renderer-backups').exists())
        self.assertFalse((self.client / fi.LOCK).exists())
        real = fi.Source.open
        with patch.object(fi.Source, 'open', lambda src, name: real(src, name) if name == 'northlight-cache.json'
                          else self.fail('re-extracted ' + name)):
            self.installer().install(self.client)   # the rerun only swaps
        self.assertEqual((self.client / 'world-cache/models/0123.fgs').read_bytes(), CACHE_FILES['models/0123.fgs'])

    def test_game_started_during_the_art_layer_stops_before_the_renderer(self):
        inst = self.installer(); state = {'n': 0}

        def running():
            return ['wow.exe'] if state['n'] else []
        inst.running = running
        real_run = inst.run

        def run(script, args, show=True):
            state['n'] = 1   # the player starts the game while the art layer builds
            return real_run(script, args, show)
        inst.run = run
        with self.assertRaises(fi.Failed):
            inst.install(self.client)
        self.assertFalse((self.client / 'renderer-backups').exists())
        self.assertFalse((self.client / 'Data/patch-z.mpq').exists())

    def test_leftover_previous_is_removed_on_a_keep_run(self):
        self.installer().install(self.client)
        previous = self.client / 'world-cache.previous'
        previous.mkdir(); (previous / 'northlight-cache.json').write_text('{}')
        report = self.installer().install(self.client)
        self.assertTrue(report['world_cache'].startswith('kept'))
        self.assertFalse(previous.exists())

    def test_incomplete_cache_is_extracted_again(self):
        self.installer().install(self.client)
        (self.client / 'world-cache/models/0123.fgs').unlink()
        self.assertTrue(self.installer().install(self.client)['world_cache'].startswith('extracted'))
        self.assertEqual((self.client / 'world-cache/models/0123.fgs').read_bytes(), CACHE_FILES['models/0123.fgs'])

    def test_lock_refuses_a_live_run_and_takes_over_a_stale_one(self):
        lock = self.client / fi.LOCK
        lock.write_text(str(os.getppid()))   # a live process
        with self.assertRaises(fi.Refusal):
            self.installer().install(self.client)
        self.assertEqual(lock.read_text(), str(os.getppid()))
        self.assertFalse((self.client / 'renderer-backups').exists())
        lock.write_text('999999')   # no such process: stale
        self.installer().install(self.client)
        self.assertFalse(lock.exists())
        self.assertTrue((self.client / 'world-cache/northlight-cache.json').exists())

    def test_launcher_action_passes_through(self):
        out = io.StringIO()
        with patch('sys.stdout', out), patch.object(fi, 'host_platform', lambda: self.platform):
            code = fi.main(['--default-action', 'install', 'status', '--client', str(self.client),
                            '--package', str(self.pkg_root)])
        self.assertEqual(code, 0)
        self.assertIn('"game_started_by_this_tool": false', out.getvalue())
        self.assertFalse((self.client / 'renderer-backups').exists())

    def test_developer_proxy_is_reported_not_removed(self):
        # migrated earlier by the developer tools: proxy, backend copy, ini and dlls.txt line, no record here
        for name, data in {'mods/d3d9.dll': PROXY, 'renderer-backends/dxvk/dxvk_d3d9.dll': DXVK,
                           'northlight-renderer.ini': mig.CONFIG, 'dlls.txt': b'mods/winerosetta.dll\nmods/d3d9.dll\n'}.items():
            (self.client / name).parent.mkdir(parents=True, exist_ok=True); (self.client / name).write_bytes(data)
        self.before = tree(self.client)
        self.installer().install(self.client)
        self.assertFalse((self.client / 'renderer-backups/mac-proxy').exists())   # already migrated: no record
        inst = self.installer()
        result = inst.uninstall(self.client)
        self.assertTrue(result['proxy_left'])
        self.assertIn('migrate_mac_proxy.py --client', inst.out.getvalue())
        self.assertEqual(tree(self.client), self.before)

    def test_entry_points_run_isolated(self):
        for name in ['northlight_install.py', 'windows-package/install.py', 'migrate_mac_proxy.py']:
            r = subprocess.run([sys.executable, '-I', '-X', 'utf8', str(fp.src(name)), '--help'], capture_output=True,
                               text=True, timeout=60)
            self.assertEqual(r.returncode, 0, name + r.stderr)
        self.assertEqual(self.installer().child_command('s.py', ['--a'])[:5], [sys.executable, '-I', '-B', '-X', 'utf8'])


class WindowsInstall(Install):
    platform = 'windows'
    test_launcher_action_passes_through = None   # main() on this host checks processes with macOS tools
    test_developer_proxy_is_reported_not_removed = None   # macOS migration

    def test_backend_and_legacy_package_restored_first(self):
        old = self.base / 'old-package'
        (old / 'payload/world-cache').mkdir(parents=True); (old / 'payload/world-cache/a.fgm').write_bytes(b'old')
        (old / 'payload/d3d9.dll').write_bytes(b'MZ Northlight renderer 0.3.144; old')
        (old / 'payload/Data').mkdir(); (old / 'payload/Data/patch-y.mpq').write_bytes(b'MPQ old foliage')
        (old / 'payload-manifest.json').write_text(json.dumps([{'path': p, 'sha256': sha((old / 'payload' / p).read_bytes())}
                                                               for p in ['world-cache/a.fgm', 'Data/patch-y.mpq', 'd3d9.dll']]))
        legacy = INSTALL.install(self.client, old)
        self.installer().install(self.client, backend='native')
        self.assertEqual(self.identified, ['zy'])   # the old patch-y goes away first: the stock match still holds
        self.assertEqual(json.loads((legacy / 'transaction.json').read_text())['status'], 'restored')
        self.assertFalse((self.client / 'Data/patch-y.mpq').exists())
        self.assertEqual((self.client / 'd3d9.dll').read_bytes(), PROXY)
        self.assertEqual((self.client / 'northlight-renderer.ini').read_bytes(), b'[Renderer]\r\nBackend=native\r\n')
        self.assertFalse((self.client / 'world-cache/a.fgm').exists())
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)

    def test_old_package_with_update_on_top_restored_first(self):
        old = self.base / 'old-package'
        (old / 'payload/world-cache').mkdir(parents=True); (old / 'payload/world-cache/a.fgm').write_bytes(b'old')
        (old / 'payload/d3d9.dll').write_bytes(b'MZ Northlight renderer 0.3.144; old')
        (old / 'payload-manifest.json').write_text(json.dumps([{'path': p, 'sha256': sha((old / 'payload' / p).read_bytes())}
                                                               for p in ['world-cache/a.fgm', 'd3d9.dll']]))
        first = INSTALL.install(self.client, old)
        small = self.base / 'small-update'
        (small / 'payload').mkdir(parents=True); (small / 'payload/d3d9.dll').write_bytes(b'MZ Northlight renderer 0.3.150; update')
        (small / 'payload-manifest.json').write_text(json.dumps([{'path': 'd3d9.dll', 'sha256': sha(b'MZ Northlight renderer 0.3.150; update')}]))
        second = INSTALL.install(self.client, small)
        self.assertEqual([b for b, _ in INSTALL.transactions(self.client)], [first, second])
        self.installer().install(self.client, backend='native')
        for b in (first, second):
            self.assertEqual(json.loads((b / 'transaction.json').read_text())['status'], 'restored')
        self.assertEqual((self.client / 'd3d9.dll').read_bytes(), PROXY)
        self.assertFalse((self.client / 'world-cache/a.fgm').exists())
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)

    def ini(self):
        return (self.client / 'northlight-renderer.ini').read_bytes()

    def test_fresh_install_writes_dxvk_and_dxvk2_choice_is_kept(self):
        self.installer().install(self.client)
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=dxvk\r\n')
        self.installer().install(self.client, backend='dxvk2')
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=dxvk2\r\n')
        self.installer().install(self.client)   # no --backend: the installed choice stays
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=dxvk2\r\n')
        self.installer().install(self.client, backend='dxvk')
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=dxvk\r\n')
        self.installer().install(self.client)
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=dxvk\r\n')

    def test_main_backend_defaults_to_none(self):
        seen = []
        with patch.object(fi.Installer, 'install', lambda self, *a: seen.append(a)), patch.object(fi, 'host_platform', lambda: 'windows'):
            self.assertEqual(fi.main(['install', '--client', str(self.client), '--package', str(self.pkg_root)]), 0)
            self.assertEqual(fi.main(['install', '--backend', 'dxvk2', '--client', str(self.client), '--package', str(self.pkg_root)]), 0)
        self.assertEqual([a[3] for a in seen], [None, 'dxvk2'])

    DXVK2 = 'renderer-backends/dxvk2/dxvk2_d3d9.dll'

    def test_unselected_dxvk_file_missing_does_not_block_but_selected_does(self):
        (self.pkg_root / 'payload' / self.DXVK2).unlink()
        inst = self.installer()
        inst.install(self.client)
        self.assertIn('backend dxvk2 will not be available', inst.out.getvalue())
        self.assertFalse((self.client / 'renderer-backends/dxvk2').exists())
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)
        with self.assertRaises(fi.Refusal) as e:
            self.installer().install(self.client, backend='dxvk2')
        self.assertIn('antivirus product may have removed it', str(e.exception))
        self.assertIn('--backend dxvk / --backend native', str(e.exception))

    def test_modified_dxvk_file_is_refused_even_when_unselected(self):
        (self.pkg_root / 'payload' / self.DXVK2).write_bytes(b'modified by antivirus')
        for backend in (None, 'dxvk', 'native', 'dxvk2'):
            with self.assertRaises(fi.Refusal) as e:
                self.installer().install(self.client, backend=backend)
            self.assertIn('damaged or modified; download and unzip the package again', str(e.exception))
        self.assertFalse((self.client / 'd3d9.dll').exists())

    def test_stale_client_dxvk2_is_removed_when_the_package_lacks_it(self):
        self.installer().install(self.client, backend='dxvk2')
        stale = self.client / self.DXVK2
        stale.write_bytes(b'older unverified dxvk2')
        (self.pkg_root / 'payload' / self.DXVK2).unlink()
        report = self.installer().install(self.client, backend='dxvk')
        self.assertFalse(stale.exists())
        fi.INSTALL.restore(self.client, Path(report['backups'][-1]))
        self.assertEqual(stale.read_bytes(), b'older unverified dxvk2')

    def test_native_choice_is_kept_on_a_plain_reinstall(self):
        self.installer().install(self.client, backend='native')
        self.installer().install(self.client)
        self.assertEqual(self.ini(), b'[Renderer]\r\nBackend=native\r\n')

    def test_plan_install_resolves_the_backend_once(self):
        calls = []
        real = fi.INSTALL.resolve_backend
        with patch.object(fi.INSTALL, 'resolve_backend', lambda *a: calls.append(a) or real(*a)):
            self.installer().install(self.client)
        self.assertEqual(len(calls), 1)

    def test_uninstall_removes_the_empty_dxvk2_folder(self):
        self.installer().install(self.client, backend='dxvk2')
        self.assertTrue((self.client / 'renderer-backends/dxvk2/dxvk2_d3d9.dll').is_file())
        self.installer().uninstall(self.client)
        self.assertFalse((self.client / 'renderer-backends').exists())
        self.assertEqual(tree(self.client), self.before)

    MARKER = 'renderer-backends/dxvk/northlight-dxvk3-init.pending'

    def marker(self):
        """A leftover of the 0.3.189-0.3.194 proxies; a plain file."""
        m = self.client / self.MARKER
        m.parent.mkdir(parents=True, exist_ok=True); m.write_bytes(b'pending')
        return m

    def test_backend_path_survives_a_plain_reinstall_and_explicit_switch_rewrites(self):
        self.installer().install(self.client)
        ini = self.client / 'northlight-renderer.ini'
        custom = b'[Renderer]\r\nBackend=dxvk\r\nBackendPath=my_d3d9.dll\r\n'
        ini.write_bytes(custom)
        self.installer().install(self.client)
        self.assertEqual(ini.read_bytes(), custom)
        self.installer().install(self.client, backend='dxvk2')
        self.assertEqual(ini.read_bytes(), b'[Renderer]\r\nBackend=dxvk2\r\n')
        custom2 = b'[Renderer]\r\nBackend=native\r\nBackendPath=x.dll\r\n'
        ini.write_bytes(custom2)
        self.installer().install(self.client)
        self.assertEqual(ini.read_bytes(), custom2)

    def test_leftover_marker_is_removed_by_every_install_with_a_note(self):
        self.installer().install(self.client)
        for backend in (None, 'dxvk2', 'native', 'dxvk'):
            m = self.marker()
            inst = self.installer()
            inst.install(self.client, backend=backend)
            self.assertFalse(m.exists(), backend)
            out = inst.out.getvalue()
            self.assertIn('no longer switches to DXVK 2.7.1 by itself', out)
            self.assertNotIn('failed to start earlier', out)
            self.assertNotIn('tried again', out)

    def test_leftover_marker_is_unlinked_only_after_a_successful_commit(self):
        self.installer().install(self.client, backend='dxvk2')
        m = self.marker()
        with patch.object(fi.INSTALL, 'commit', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                self.installer().install(self.client, backend='native')
        self.assertTrue(m.is_file())
        self.installer().install(self.client, backend='native')
        self.assertFalse(m.exists())

    def test_leftover_marker_is_unlinked_when_nothing_else_changes(self):
        self.installer().install(self.client, backend='dxvk')
        m = self.marker()
        self.installer().install(self.client, backend='dxvk')
        self.assertFalse(m.exists())

    def test_linked_dxvk_folder_is_refused_for_install_and_uninstall(self):
        elsewhere = self.client.parent / 'elsewhere-dxvk'
        elsewhere.mkdir()
        (elsewhere / 'northlight-dxvk3-init.pending').write_bytes(b'pending')
        (self.client / 'renderer-backends').mkdir()
        (self.client / 'renderer-backends/dxvk').symlink_to(elsewhere, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.installer().install(self.client)
        with self.assertRaises(fi.Refusal):
            self.installer().uninstall(self.client)
        self.assertTrue((elsewhere / 'northlight-dxvk3-init.pending').is_file())

    def test_uninstall_removes_the_leftover_marker_and_the_folder(self):
        self.installer().install(self.client)
        self.marker()
        self.installer().uninstall(self.client)
        self.assertEqual(tree(self.client), self.before)
        self.assertFalse((self.client / 'renderer-backends').exists())


if __name__ == '__main__':
    unittest.main()
