#!/usr/bin/env python3
# northlight-test:
"""Verify the player packages built by renderer/build_packages.py (never runs a Windows binary or the game).

    python3 tests/verify_packages.py [ZIP ...]     # default: every package in <out>/packages/packages.json

Per installer zip: one top folder; only allowlisted paths (app/ = build_packages.APP_FILES, byte-equal to
this repository; runtime/, payload/, variants/, LICENSES/, the launchers); no game data (suffixes and
magic bytes), no records, logs, caches or machine paths (check_layout.MACHINE) outside runtime/; the pins
(runtime, DXVK, StormLib) and BUILD-INFO.json agree with the bytes; the payload manifest is exact; the
StormLib binary verifies (build_stormlib.verify); the Windows runtime has _ctypes, libffi, _hashlib,
_bz2 and _uuid; launchers: .cmd with CRLF and chcp 65001, .command with LF that passes `bash -n`; neither
uses the system Python. On a macOS host the macOS package is unpacked and its own Python runs every
shipped entry point with -I -X utf8 --help (that loads the packaged StormLib). A cache zip in
packages.json is checked against its manifest (allowlist, every sha256).
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
import zipfile

import build_packages as bp  # noqa: E402
import build_stormlib  # noqa: E402
import check_layout  # noqa: E402

FORBIDDEN_SUFFIX = {'.mpq', '.fg3', '.fgs', '.fgl', '.fct', '.fcm', '.frf', '.dbc', '.blp', '.m2', '.skin', '.wmo',
                    '.adt', '.log', '.pdb', '.dmp', '.pyc'}
FORBIDDEN_PART = {'world-cache', 'records', 'wtf', 'cache', 'logs', 'screenshots', '.git', 'tools', 'out', 'backups',
                  'inspection', '__pycache__', 'docs', 'releases'}
GAME_MAGIC = (b'MPQ\x1a', b'MPQ\x1b', b'WDBC', b'BLP2', b'MD20', b'MD21', b'REVM', b'FGS2', b'FGS3', b'FGL1', b'FCT1',
              b'FCM1')
TEXT_SUFFIX = {'.py', '.json', '.txt', '.ini', '.inc', '.cmd', '.command', '.md'}
WINDOWS_RUNTIME = ['python.exe', 'python313.dll', 'python313.zip', 'python313._pth', '_ctypes.pyd', 'libffi-8.dll',
                   '_hashlib.pyd', '_bz2.pyd', '_uuid.pyd', 'StormLib.dll']
ENTRY_POINTS = ['renderer/northlight_install.py', 'renderer/windows-package/install.py', 'renderer/migrate_mac_proxy.py',
                'scripts/install_world_cache.py', 'scripts/client_identity.py', 'build_art_layer.py']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1 << 20), b''):
            h.update(data)
    return h.hexdigest()


def check(problems, ok, message):
    if not ok:
        problems.append(message)


def allowed(rel, platform_name, launchers, payload):
    if rel.startswith('runtime/'):
        return True
    if rel.startswith('app/'):
        return rel[4:] in bp.APP_FILES
    if rel.startswith('payload/'):
        return rel[8:] in payload or (platform_name == 'mac' and rel == 'payload/d3d9.dll')
    return rel in launchers or rel in {'BUILD-INFO.json', 'payload-manifest.json', 'README.txt'} or \
        re.fullmatch(r'variants/[a-z0-9-]+\.json|LICENSES/(python-third-party/)?[A-Za-z0-9.-]+\.txt', rel) is not None


def verify_installer(path):
    problems = []
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if not n.endswith('/')]
        tops = {n.split('/', 1)[0] for n in names}
        check(problems, len(tops) == 1 and len(names) == len(set(names)), f'top folders {sorted(tops)} or duplicates')
        top = sorted(tops)[0]
        files = {n.split('/', 1)[1]: z.read(n) for n in names}
        modes = {n.split('/', 1)[1]: z.getinfo(n).external_attr >> 16 for n in names}
    info = json.loads(files['BUILD-INFO.json'])
    plat = info['platform']
    check(problems, top == f'Northlight-{info["version"]}-{bp.PLATFORMS[plat]}', f'top folder {top}')
    launchers = bp.launchers(plat, info['version'])
    manifest = json.loads(files['payload-manifest.json'])
    payload = {e['path']: e for e in manifest}
    for rel, data in sorted(files.items()):
        check(problems, allowed(rel, plat, launchers, payload), f'not allowlisted: {rel}')
        parts = rel.lower().split('/')
        check(problems, Path(rel).suffix.lower() not in FORBIDDEN_SUFFIX, f'forbidden suffix: {rel}')
        check(problems, rel.startswith('runtime/') or not FORBIDDEN_PART & set(parts[:-1]), f'forbidden folder: {rel}')
        check(problems, not data.startswith(GAME_MAGIC), f'game data magic in {rel}')
        if not rel.startswith('runtime/') and Path(rel).suffix.lower() in TEXT_SUFFIX:
            text = data.decode('utf-8', 'replace')
            hits = [line for line in text.splitlines() if check_layout.MACHINE.search(line) and check_layout.ALLOW not in line]
            check(problems, not hits, f'machine path in {rel}: {hits[:1]}')
    # app/: exactly the allowlist, byte-equal to this repository and to BUILD-INFO
    check(problems, sorted(r[4:] for r in files if r.startswith('app/')) == sorted(bp.APP_FILES), 'app/ file list differs')
    for name in bp.APP_FILES:
        data = files.get('app/' + name, b'')
        check(problems, sha(data) == info['app_files'].get(name), f'app/{name} differs from BUILD-INFO')
        check(problems, (fp.REPO / name).is_file() and sha(data) == sha((fp.REPO / name).read_bytes()),
              f'app/{name} differs from this repository')
    # payload
    dll = files.get('payload/d3d9.dll', b'')
    check(problems, sha(dll) == info['dll_sha256'] and bp.MARKER.search(dll) is not None, 'payload/d3d9.dll')
    for p, e in payload.items():
        check(problems, sha(files.get('payload/' + p, b'')) == e['sha256'], f'payload/{p} does not match its manifest')
    check(problems, [p for p, e in payload.items() if e.get('preserve')] == ['northlight-quality.ini'], 'preserve flags')
    d3d9 = [r for r in files if r.lower().endswith('/d3d9.dll') and not r.startswith('runtime/')]
    check(problems, d3d9 == ['payload/d3d9.dll'], f'd3d9.dll copies: {d3d9}')
    # pins
    runtime_pin = bp.PINS['python_' + plat]
    check(problems, info['runtime']['sha256'] == runtime_pin['sha256'], 'runtime pin differs from BUILD-INFO')
    lib = 'runtime/StormLib.dll' if plat == 'windows' else 'runtime/lib/libstorm.dylib'
    check(problems, sha(files.get(lib, b'')) == bp.PINS['stormlib'][('windows' if plat == 'windows' else 'mac') + '_sha256']
          == info['stormlib_sha256'], 'StormLib differs from its pin')
    with tempfile.TemporaryDirectory() as temp:
        library = Path(temp) / Path(lib).name
        library.write_bytes(files.get(lib, b''))
        storm_problems, _ = build_stormlib.verify(library)
    check(problems, not storm_problems, f'StormLib: {storm_problems}')
    if plat == 'windows':
        missing = [n for n in WINDOWS_RUNTIME if 'runtime/' + n not in files]
        check(problems, not missing, f'Windows runtime lacks {missing}')
        check(problems, sha(files.get('payload/' + bp.DXVK_BACKEND, b'')) == bp.PINS['dxvk']['member_sha256'], 'DXVK pin')
        check(problems, info['dxvk']['sha256'] == bp.PINS['dxvk']['sha256'], 'DXVK archive pin')
        check(problems, sha(files.get('payload/' + bp.DXVK2_BACKEND, b'')) == bp.PINS['dxvk2']['member_sha256'], 'DXVK fallback (dxvk2) pin')
        check(problems, info['dxvk_fallback']['sha256'] == bp.PINS['dxvk2']['sha256'], 'DXVK fallback archive pin')
        pinned = runtime_zip()
        if pinned:
            with zipfile.ZipFile(pinned) as z:
                check(problems, all(files.get('runtime/' + n) == z.read(n) for n in z.namelist() if not n.endswith('/')),
                      'runtime/ differs from the pinned embeddable zip')
    else:
        licences = {f'LICENSES/python-third-party/{n}.txt': e['sha256'] for n, e in bp.PINS['python_mac_licenses']['files'].items()}
        check(problems, {r: sha(d) for r, d in files.items() if r.startswith('LICENSES/python-third-party/')} == licences,
              'LICENSES/python-third-party/ differs from python_mac_licenses')
        check(problems, modes.get('runtime/bin/python3', 0) & 0o111 and 'runtime/lib/python3.13/os.py' in files,
              'macOS runtime lacks an executable bin/python3 or its stdlib')
        check(problems, not [r for r in files if re.match(r'runtime/lib/(tk|tcl|python3\.13/(site-packages|idlelib|tkinter)/)', r)],
              'macOS runtime not pruned')
    # launchers
    for name, expected in launchers.items():
        data = files.get(name, b'')
        check(problems, data == expected, f'{name} differs from build_packages.launchers')
        check(problems, b'/usr/bin/python' not in data and b'python3 ' not in data.replace(b'bin/python3', b''),
              f'{name} may use a system Python')
        if name.endswith('.cmd'):
            check(problems, data.count(b'\r\n') == data.count(b'\n') and b'chcp 65001' in data and
                  b'runtime\\python.exe" -I -B -X utf8' in data, f'{name}: CRLF/chcp/runtime')
        else:
            check(problems, b'\r' not in data and modes.get(name, 0) & 0o111, f'{name}: LF and executable')
            check(problems, b'"$PKG/runtime/bin/python3" -I -B -X utf8' in data and
                  b'xattr -dr com.apple.quarantine "$PKG"' in data, f'{name}: runtime or quarantine scope')
            with tempfile.NamedTemporaryFile(suffix='.command', delete=False) as f:
                f.write(data)
            try:
                check(problems, subprocess.run(['bash', '-n', f.name]).returncode == 0, f'{name}: bash -n')
            finally:
                os.unlink(f.name)
    readme = files.get('README.txt', b'').decode('utf-8', 'replace')
    check(problems, '__RELEASE_VERSION__' not in readme and info['version'] in readme, 'README.txt version')
    for rel, data in files.items():
        if rel.startswith('variants/'):
            m = json.loads(data)
            check(problems, m.get('format') == 'northlight-cache-variant/1' and bp.variant_digest(m['files']) == m['cache_digest'],
                  f'{rel}: not a consistent variant manifest')
    smoke = run_entry_points(path, top) if plat == 'mac' and sys.platform == 'darwin' and platform.machine() == 'arm64' else None
    if smoke:
        problems += smoke
    return {'file': path.name, 'platform': plat, 'files': len(files), 'bytes': path.stat().st_size,
            'sha256': file_sha(path), 'entry_points_run': smoke is not None, 'problems': problems}


def runtime_zip():
    pin = bp.PINS['python_windows']
    for folder in [fp.setting('downloads'), fp.tools()]:
        if folder and (Path(folder) / pin['file']).is_file():
            return Path(folder) / pin['file']
    return None


def run_entry_points(path, top):
    """Unpack the macOS package and run each shipped entry point with its own Python (-I -X utf8 --help)."""
    problems = []
    work = Path(tempfile.mkdtemp(prefix='verify-mac-', dir=fp.output_dir()))
    try:
        subprocess.run(['/usr/bin/ditto', '-x', '-k', str(path), str(work)], check=True)   # keeps modes, like Finder
        root = work / top
        python = root / 'runtime/bin/python3'
        env = dict(os.environ, NORTHLIGHT_STORMLIB=str(root / 'runtime/lib/libstorm.dylib'))
        r = subprocess.run([str(python), '-I', '-X', 'utf8', '-c', 'import sys, ctypes, hashlib, bz2, zlib, lzma, uuid, '
                            'zipfile, json, sqlite3; print(sys.version.split()[0], sys.flags.isolated, sys.flags.utf8_mode)'],
                           capture_output=True, text=True, env=env, timeout=120)
        check(problems, r.returncode == 0 and r.stdout.split()[1:] == ['1', '1'], f'runtime modules: {r.stderr[-300:]}')
        for script in ENTRY_POINTS:
            r = subprocess.run([str(python), '-I', '-X', 'utf8', str(root / 'app' / script), '--help'],
                               capture_output=True, text=True, env=env, timeout=120, cwd=work)
            check(problems, r.returncode == 0, f'{script} --help under the packaged Python: {r.stderr[-400:]}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return problems


def verify_cache(path, entry):
    with zipfile.ZipFile(path) as z:
        manifest = json.loads(z.read('northlight-cache.json'))
    try:
        bp.check_cache_zip(path, manifest)
        problems = [] if manifest['cache_digest'] == entry.get('cache_digest') else ['cache digest differs from packages.json']
    except SystemExit as e:
        problems = [str(e)]
    return {'file': path.name, 'files': len(manifest['files']), 'bytes': path.stat().st_size, 'problems': problems}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('zips', nargs='*', type=Path, help='zips to check (default: <out>/packages/packages.json)')
    out = fp.out() / 'packages'
    given = ap.parse_args().zips
    listed = json.loads((out / 'packages.json').read_text())['packages'] if not given else []
    reports = []
    for entry in listed or [{'file': p.name, 'path': p} for p in given]:
        path = Path(entry.get('path') or out / entry['file'])
        if not given and file_sha(path) != entry['sha256']:
            reports.append({'file': path.name, 'problems': ['sha256 differs from packages.json']})
            continue
        reports.append(verify_cache(path, entry) if path.name.startswith('Northlight-cache-') else verify_installer(path))
        print(('OK   ' if not reports[-1]['problems'] else 'FAIL ') + path.name, flush=True)
        for p in reports[-1]['problems']:
            print('     ' + p)
    (fp.output_dir() / 'verification.json').write_text(json.dumps({'packages': reports, 'game_launched': False},
                                                                   indent=2) + '\n')
    return 1 if not reports or any(r['problems'] for r in reports) else 0


if __name__ == '__main__':
    sys.exit(main())
