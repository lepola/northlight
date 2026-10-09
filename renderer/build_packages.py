#!/usr/bin/env python3
"""Build the player installer packages (macOS, Windows and Linux). Never runs a Windows binary or the game.

    python3 renderer/build_packages.py --platform all --version 0.3.162 [--dll renderer/frd9.dll]
        [--variant-manifest out/variants/stock/northlight-cache.json] [--cache-zip Northlight-cache-stock-<d12>.zip]
        [--out out/packages]

Northlight-<v>-<macOS|Windows|Linux>.zip holds one folder of the same name:
  app/          the allowlisted repository subset (APP_FILES) in its repository layout: the installer
                front end, install.py, migrate_mac_proxy.py and the local world-cache and art-layer
                pipeline. Every repository module a packaged module imports must be packaged.
  runtime/      Python 3.13 (macOS: python-build-standalone arm64, pruned; Windows: python.org
                embed-amd64; Linux: python-build-standalone x86_64-linux-gnu, pruned) and StormLib
                (scripts/build_stormlib.py; the bytes must equal the pin)
  payload/      d3d9.dll (the renderer), the profile .ini files, northlight-quality.ini (the
                player's own is kept; only settings it lacks are appended, commented out); Windows and Linux (the game
                runs under Wine there, so the payload is the Windows one): DXVK 3.1.1 as renderer-backends/dxvk/dxvk_d3d9.dll
                and DXVK 2.7.1 (the alternative backend dxvk2) as renderer-backends/dxvk2/dxvk2_d3d9.dll
  variants/     the prebuilt cache manifests the installer matches (from --variant-manifest)
  LICENSES/     third-party licences; macOS and Linux also python-third-party/ (the libraries linked into their Python)
  BUILD-INFO.json, payload-manifest.json, README.txt and the launchers
                (Install Northlight.command / Uninstall Northlight.command, Install.cmd / Uninstall.cmd, or
                install.sh / uninstall.sh)
No world cache, MPQ or other game data, no records, logs or machine paths: tests/verify_packages.py
checks the zips. Third-party downloads come from NORTHLIGHT_DOWNLOADS, then tools/, and must match
renderer/package-pins.json. --cache-zip verifies a variant cache zip (allowlist, every file's sha256)
and copies it next to the packages.
"""
import argparse
import ast
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402
fp.use_source_modules()
import build_stormlib  # noqa: E402

PINS = json.loads((fp.RENDERER / 'package-pins.json').read_text(encoding='utf-8'))
PLATFORMS = {'mac': 'macOS', 'windows': 'Windows', 'linux': 'Linux'}
PROXY_PLATFORMS = ('windows', 'linux')   # the game-folder d3d9.dll proxy with the bundled DXVK backends
# Repository files the package needs, in their repository layout (northlight_paths finds them there).
APP_FILES = [
    'northlight_paths.py', 'mpq.py', 'client_archives.py',
    'build_art_layer.py', 'build_lighting.py', 'build_mulgore_lighting.py', 'build_stormwind_single_sun.py',
    'build_outdoor_single_sun.py', 'build_outdoor_single_moon.py', 'build_weather_textures.py',  # 0.3.198 (rain): the art layer's procedural weather textures
    'scripts/install_world_cache.py', 'scripts/client_identity.py',
    'renderer/northlight_install.py', 'renderer/migrate_mac_proxy.py', 'renderer/windows-package/install.py',
    'renderer/world_scene_builder.py', 'renderer/m2_visibility.py', 'renderer/world_lights_builder.py',
    'renderer/world_light_placements.py', 'renderer/outdoor_light_profiles.py', 'renderer/outdoor-light-profiles.json',
    'renderer/regional_fog_builder.py', 'renderer/build_celestial_disc_assets.py',
    'tests/validate_world_cache.py', 'src/sky/forest_regions.inc',
]
PAYLOAD_COMMON = {'celestial-profiles.ini': 'client-config/celestial-profiles.ini',
                  'shadow-range-profiles.ini': 'client-config/shadow-range-profiles.ini',
                  'northlight-quality.ini': 'renderer/windows-package/northlight-quality.ini'}
PRESERVE = {'northlight-quality.ini'}
DXVK_BACKEND = 'renderer-backends/dxvk/dxvk_d3d9.dll'
DXVK2_BACKEND = 'renderer-backends/dxvk2/dxvk2_d3d9.dll'


def runtime_drop(config, extra=''):
    """What is pruned from a python-build-standalone install_only tree (paths under python/, or under runtime/ in a
    package, where bin/python3.13 is bin/python3): pip, Tcl/Tk, IDLE, docs, headers, terminfo (share/) and the
    embedding library (bin/python3.13 links libpython statically), plus the platform's extra modules."""
    return re.compile(r'^(bin/(?!python3(\.13)?$)|include/|share/|lib/(libpython|libtcl|libtk|itcl|tcl|tk|thread|pkgconfig)|'
                      r'lib/python3\.13/(site-packages/|idlelib/|tkinter/|turtledemo/|ensurepip/|pydoc_data/|'
                      rf'config-3\.13-{config}/|lib-dynload/_tkinter{extra})|.*/__pycache__/)')


# Linux also drops _dbm with its Berkeley DB (a shared module there; on macOS it is part of the runtime).
RUNTIME_DROP = {'mac': runtime_drop('darwin'), 'linux': runtime_drop('x86_64-linux-gnu', '|lib-dynload/_dbm')}
CACHE_MEMBER = re.compile(r'^([A-Za-z0-9]+/\d+_\d+\.fg3|models/[0-9a-f]+\.fgs|lights/[A-Za-z0-9]+\.fgl|'
                          r'fog/[A-Za-z0-9]+/\d+_\d+\.frf|celestial/(sun|moon)\.fct|northlight-cache\.json)$')
MARKER = re.compile(rb'Northlight renderer (\d+\.\d+\.\d+);')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for data in iter(lambda: f.read(1 << 20), b''):
            h.update(data)
    return h.hexdigest()


def download(pin):
    """A pinned third-party file from NORTHLIGHT_DOWNLOADS or tools/; refuses a sha256 mismatch."""
    for folder in [fp.setting('downloads'), fp.tools()]:
        path = Path(folder) / pin['file'] if folder else None
        if path and path.is_file():
            if sha(path) != pin['sha256']:
                raise SystemExit(f'{path.name}: sha256 does not match renderer/package-pins.json')
            return path
    raise SystemExit(f'{pin["file"]} not found in NORTHLIGHT_DOWNLOADS or tools/; download it from {pin["url"]}')


def repo_modules():
    """Python module name -> repository path (relative) for every module a packaged file could import."""
    found = {}
    for folder in [fp.REPO, *fp.SCRIPT_DIRS, fp.RENDERER, fp.TESTS]:
        for p in folder.glob('*.py'):
            found.setdefault(p.stem, p.relative_to(fp.REPO).as_posix())
    return found


def check_closure(files):
    """Every repository module imported by a packaged module is packaged."""
    modules, packaged, missing = repo_modules(), set(files), []
    for name in files:
        if not name.endswith('.py'):
            continue
        tree = ast.parse((fp.REPO / name).read_text(encoding='utf-8'))
        for node in ast.walk(tree):
            names = [a.name for a in node.names] if isinstance(node, ast.Import) else \
                [node.module] if isinstance(node, ast.ImportFrom) and node.module and not node.level else []
            for n in names:
                top = n.split('.')[0]
                if top in modules and modules[top] not in packaged:
                    missing.append(f'{name} imports {top} ({modules[top]})')
    if missing:
        raise SystemExit('Package app/ is missing modules:\n  ' + '\n  '.join(sorted(set(missing))))


def pe_machine(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    return struct.unpack_from('<H', data, pe + 4)[0] if data[pe:pe + 4] == b'PE\0\0' else None


def check_dll(data):
    if pe_machine(data) != 0x14c:
        raise SystemExit('the renderer DLL is not a 32-bit PE')
    if not MARKER.search(data) or b'PROXY module=%ls root=%ls' not in data:
        raise SystemExit('the renderer DLL is not a proxy-capable Northlight renderer build')
    return MARKER.search(data).group(1).decode()


def commit_time():
    """The last commit's time, used as the zip entry date (None outside a repository)."""
    try:
        stamp = subprocess.run(['git', 'log', '-1', '--format=%ct'], cwd=fp.REPO, capture_output=True, text=True, timeout=30)
        return int(stamp.stdout.strip() or 0) or None
    except (OSError, ValueError):
        return None


def variant_digest(files):
    """The cache digest as scripts/build_cache_variant.py records it (recomputed here, not trusted)."""
    rows = ''.join(f"{name}\0{f['sha256']}\0{f['bytes']}\n" for name, f in sorted(files.items()))
    return hashlib.sha256(rows.encode()).hexdigest()


def check_cache_zip(path, manifest):
    """A variant cache zip: only allowlisted members, northlight-cache.json equal to the manifest, every sha256."""
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        bad = [n for n in names if not CACHE_MEMBER.match(n)]
        if bad:
            raise SystemExit(f'{path.name}: members outside the cache allowlist: {bad[:5]}')
        if json.loads(z.read('northlight-cache.json')) != manifest or variant_digest(manifest['files']) != manifest['cache_digest']:
            raise SystemExit(f'{path.name}: northlight-cache.json differs from the variant manifest')
        if sorted(set(names) - {'northlight-cache.json'}) != sorted(manifest['files']):
            raise SystemExit(f'{path.name}: members differ from the manifest file list')
        for n, meta in manifest['files'].items():
            h = hashlib.sha256()
            with z.open(n) as f:
                for data in iter(lambda: f.read(1 << 20), b''):
                    h.update(data)
            if h.hexdigest() != meta['sha256']:
                raise SystemExit(f'{path.name}: {n} sha256 mismatch')


class Tree:
    """The package contents in memory order: {path: (bytes or source Path, executable)}."""

    def __init__(self, top):
        self.top, self.files = top, {}

    def add(self, name, data, executable=False):
        if name in self.files:
            raise SystemExit('duplicate package path ' + name)
        self.files[name] = (data, executable)

    def data(self, name):
        data, _ = self.files[name]
        return data.read_bytes() if isinstance(data, Path) else data

    def write_zip(self, path, stamp):
        date = time.gmtime(max(stamp or 0, 315532800))[:6]
        temp = path.with_name(path.name + '.tmp')
        with zipfile.ZipFile(temp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name in sorted(self.files):
                data, executable = self.files[name]
                info = zipfile.ZipInfo(f'{self.top}/{name}', date)
                info.external_attr = (0o100755 if executable else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, data.read_bytes() if isinstance(data, Path) else data, compresslevel=9)
        temp.replace(path)


def standalone_runtime(tree, pin, drop):
    """A pruned python-build-standalone install_only tree as runtime/ (bin/python3.13 becomes bin/python3)."""
    with tarfile.open(download(pin)) as tar:
        for member in tar.getmembers():
            if not member.isfile() or not member.name.startswith('python/'):
                continue
            rel = member.name[len('python/'):]
            if drop.match(rel):
                continue
            data = tar.extractfile(member).read()
            if rel == 'bin/python3.13':
                rel = 'bin/python3'
            tree.add('runtime/' + rel, data, executable=bool(member.mode & 0o111) and rel.startswith('bin/'))
    tree.add('LICENSES/Python-LICENSE.txt', tree.data('runtime/lib/python3.13/LICENSE.txt'))


def python_licences(platform):
    """{name: pin} of the third-party libraries linked into the platform's Python."""
    if platform == 'mac':
        return dict(PINS['python_mac_licenses']['files'])
    pins = PINS['python_linux_licenses']
    return {**{n: PINS['python_mac_licenses']['files'][n] for n in pins['shared']}, **pins['files']}


def mac_runtime(tree, pin, stormlib):
    standalone_runtime(tree, pin, RUNTIME_DROP['mac'])
    tree.add('runtime/lib/libstorm.dylib', stormlib.read_bytes(), executable=True)
    for name, licence in python_licences('mac').items():   # statically linked into bin/python3
        tree.add(f'LICENSES/python-third-party/{name}.txt', download(licence).read_bytes())


def linux_runtime(tree, pin, stormlib):
    standalone_runtime(tree, pin, RUNTIME_DROP['linux'])
    tree.add('runtime/lib/libstorm.so', stormlib.read_bytes(), executable=True)
    for name, licence in sorted(python_licences('linux').items()):   # statically linked into bin/python3
        tree.add(f'LICENSES/python-third-party/{name}.txt', download(licence).read_bytes())


def windows_runtime(tree, pin, stormlib):
    with zipfile.ZipFile(download(pin)) as z:
        for name in z.namelist():
            if not name.endswith('/'):
                tree.add('runtime/' + name, z.read(name))
    tree.add('runtime/StormLib.dll', stormlib.read_bytes())
    tree.add('LICENSES/Python-LICENSE.txt', tree.data('runtime/LICENSE.txt'))


def dxvk_files(pin_name='dxvk'):
    pin = PINS[pin_name]
    with tarfile.open(download(pin)) as tar:
        dll = tar.extractfile(pin['member']).read()
    if hashlib.sha256(dll).hexdigest() != pin['member_sha256']:
        raise SystemExit('DXVK d3d9.dll sha256 does not match the pin')
    license_path = download({**pin, 'file': pin['license_file'], 'sha256': pin['license_sha256']})
    return dll, license_path.read_bytes()


def launchers(platform, version):
    if platform == 'linux':
        def script(action):
            # POSIX sh: `sh install.sh` (dash) works as well as `bash install.sh` or ./install.sh.
            return (f'#!/bin/sh\n# Northlight renderer {version}: {action} (Linux; the game runs under Wine or Proton).\n'
                    f'# Start it in a terminal in this folder:  bash {action}.sh\n'
                    '# A first argument install, uninstall or status replaces the default action.\n'
                    'PKG="$(cd "$(dirname "$0")" && pwd -P)"\n'
                    'PY="$PKG/runtime/bin/python3"\n'
                    '# Some archive tools drop the executable bit; this restores it in this package folder only.\n'
                    '[ -x "$PY" ] || chmod u+x "$PY" 2>/dev/null\n'
                    'if [ "$(uname -m)" != x86_64 ]; then\n'
                    '  echo "This package is for 64-bit x86 Linux (x86_64); this computer is $(uname -m)."; exit 126\n'
                    'fi\n'
                    'if ! "$PY" -I -c ""; then\n'
                    '  echo "Cannot run the package\'s own Python ($PY); the reason is above. It needs glibc 2.17 or newer."\n'
                    '  echo "Unzip the package into your home folder (not onto an NTFS/exFAT drive or a noexec mount) and run this again."\n'
                    '  exit 126\n'
                    'fi\n'
                    f'"$PY" -I -B -X utf8 "$PKG/app/renderer/northlight_install.py" --default-action {action} "$@"\n'
                    'status=$?\n'
                    '# Started from a file manager, the window would close before the result can be read.\n'
                    'if [ -t 0 ]; then echo; printf "Press Enter to close this window. "; read -r _; fi\n'
                    'exit $status\n').encode('ascii')
        return {'install.sh': script('install'), 'uninstall.sh': script('uninstall')}
    if platform == 'windows':
        def cmd(action):
            return ('@echo off\r\nchcp 65001 >nul\r\n'
                    f'"%~dp0runtime\\python.exe" -I -B -X utf8 "%~dp0app\\renderer\\northlight_install.py" '
                    f'--default-action {action} %*\r\n'
                    'set "result=%errorlevel%"\r\necho.\r\npause\r\nexit /b %result%\r\n').encode('ascii')
        return {'Install.cmd': cmd('install'), 'Uninstall.cmd': cmd('uninstall')}

    def command(action, title):
        return (f'#!/bin/bash\n# Northlight renderer {version}: {title} (macOS, WoWSilicon).\n'
                '# Start it in Terminal: type  bash  and a space, drag this file into the window, press Enter.\n'
                '# A first argument install, uninstall or status replaces the default action.\n'
                'PKG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"\n'
                '# A browser marks downloaded files as quarantined; this clears it in this package folder only.\n'
                '/usr/bin/xattr -dr com.apple.quarantine "$PKG" 2>/dev/null\n'
                f'"$PKG/runtime/bin/python3" -I -B -X utf8 "$PKG/app/renderer/northlight_install.py" '
                f'--default-action {action} "$@"\n'
                'status=$?\necho\nread -r -p "Press Enter to close this window. " _\nexit $status\n').encode('ascii')
    return {'Install Northlight.command': command('install', 'install'),
            'Uninstall Northlight.command': command('uninstall', 'uninstall')}


def build(platform, version, dll, variants, out, stormlib_dir):
    top = f'Northlight-{version}-{PLATFORMS[platform]}'
    tree = Tree(top)
    stamp = commit_time()
    check_closure(APP_FILES)
    for name in APP_FILES:
        tree.add('app/' + name, fp.REPO / name)
    dll_data = dll.read_bytes()
    dll_version = check_dll(dll_data)
    lib = build_stormlib.build(platform, stormlib_dir / platform)
    problems, _ = build_stormlib.verify(lib)
    if problems or sha(lib) != PINS['stormlib'][platform + '_sha256']:
        raise SystemExit(f'StormLib {platform} build does not verify or does not match its pin: {problems or sha(lib)}')
    runtime_pin = PINS['python_' + platform]
    {'windows': windows_runtime, 'mac': mac_runtime, 'linux': linux_runtime}[platform](tree, runtime_pin, lib)
    payload = {n: (fp.REPO / src).read_bytes() for n, src in PAYLOAD_COMMON.items()}
    if platform in PROXY_PLATFORMS:
        backend, license_text = dxvk_files('dxvk')
        backend2, license_text2 = dxvk_files('dxvk2')
        payload.update({'d3d9.dll': dll_data, DXVK_BACKEND: backend, 'renderer-backends/dxvk/LICENSE': license_text,
                        DXVK2_BACKEND: backend2, 'renderer-backends/dxvk2/LICENSE': license_text2})
        tree.add('LICENSES/DXVK-LICENSE.txt', license_text)
        if license_text2 != license_text:
            tree.add('LICENSES/DXVK-2.x-LICENSE.txt', license_text2)
    else:
        tree.add('payload/d3d9.dll', dll_data)   # installed by migrate_mac_proxy as mods/d3d9.dll
    for name, data in payload.items():
        tree.add('payload/' + name, data)
    tree.add('payload-manifest.json', (json.dumps(
        [{'path': n, 'sha256': hashlib.sha256(d).hexdigest(), **({'preserve': True} if n in PRESERVE else {})}
         for n, d in sorted(payload.items())], indent=2) + '\n').encode())
    for name, manifest in sorted(variants.items()):
        tree.add(f'variants/{name}.json', (json.dumps(manifest, indent=2) + '\n').encode())
    tree.add('LICENSES/StormLib-NOTICES.txt', build_stormlib.notices(build_stormlib.source_root()).encode('utf-8'))
    readme = fp.RENDERER / f'{platform}-package' / 'README.txt'
    text = readme.read_text(encoding='utf-8').replace('__RELEASE_VERSION__', version)
    tree.add('README.txt', text.replace('\n', '\r\n').encode('utf-8') if platform == 'windows' else text.encode('utf-8'))
    for name, data in launchers(platform, version).items():
        tree.add(name, data, executable=True)
    info = {'format': 'northlight-package/1', 'version': version, 'platform': platform,
            'dll_version': dll_version, 'dll_sha256': hashlib.sha256(dll_data).hexdigest(),
            'dll_pe_normalized_sha256': hashlib.sha256(pe_normalized(dll_data)).hexdigest(),
            'stormlib_sha256': sha(lib), 'stormlib_source_tree_sha256': PINS['stormlib']['source_tree_sha256'],
            'runtime': {k: runtime_pin[k] for k in ('name', 'version', 'url', 'sha256')},
            'dxvk': {k: PINS['dxvk'][k] for k in ('version', 'url', 'sha256')} if platform in PROXY_PLATFORMS else None,
            'dxvk_fallback': {k: PINS['dxvk2'][k] for k in ('version', 'url', 'sha256')} if platform in PROXY_PLATFORMS else None,
            'variants': {n: {'cache_digest': m['cache_digest'], 'files': len(m['files'])} for n, m in sorted(variants.items())},
            'app_files': {n: sha(fp.REPO / n) for n in APP_FILES}}
    tree.add('BUILD-INFO.json', (json.dumps(info, indent=2) + '\n').encode())
    path = out / f'{top}.zip'
    tree.write_zip(path, stamp)
    return {'platform': platform, 'file': path.name, 'sha256': sha(path), 'bytes': path.stat().st_size,
            'files': len(tree.files), 'dll_version': dll_version}


def pe_normalized(data):
    import pe_normalized_hash
    return pe_normalized_hash.normalized(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--platform', choices=[*PLATFORMS, 'all'], default='all')
    ap.add_argument('--version', required=True, help='package label, e.g. 0.3.162')
    ap.add_argument('--dll', type=Path, help='renderer build (default: renderer/frd9.dll or NORTHLIGHT_DLL)')
    ap.add_argument('--variant-manifest', type=Path, action='append', default=[],
                    help="a variant's northlight-cache.json (repeatable); the installer matches clients against it")
    ap.add_argument('--cache-zip', type=Path, action='append', default=[],
                    help='a variant cache zip to verify and copy next to the packages')
    ap.add_argument('--out', type=Path, help='default: <out>/packages')
    args = ap.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+([.-][A-Za-z0-9]+)*', args.version):
        ap.error('--version must look like 0.3.162')
    out = args.out or fp.out() / 'packages'
    out.mkdir(parents=True, exist_ok=True)
    variants = {}
    for path in args.variant_manifest:
        manifest = json.loads(path.read_text(encoding='utf-8'))
        if manifest.get('format') != 'northlight-cache-variant/1' or variant_digest(manifest['files']) != manifest['cache_digest']:
            raise SystemExit(f'{path}: not a northlight-cache-variant/1 manifest with a matching digest')
        variants[manifest['variant']] = manifest
    reports = []
    for zip_path in args.cache_zip:
        with zipfile.ZipFile(zip_path) as z:
            manifest = json.loads(z.read('northlight-cache.json'))
        if variants.get(manifest['variant'], manifest) != manifest:
            raise SystemExit(f'{zip_path.name}: its northlight-cache.json differs from --variant-manifest')
        variants[manifest['variant']] = manifest
        check_cache_zip(zip_path, manifest)
        name = f'Northlight-cache-{manifest["variant"]}-{manifest["cache_digest"][:12]}.zip'
        if (out / name).resolve() != zip_path.resolve():
            (out / name).unlink(missing_ok=True)
            try:
                os.link(zip_path, out / name)   # 3.5 GB: a hard link when on the same volume
            except OSError:
                shutil.copyfile(zip_path, out / name)
        reports.append({'platform': 'any', 'file': name, 'sha256': sha(out / name), 'bytes': (out / name).stat().st_size,
                        'variant': manifest['variant'], 'cache_digest': manifest['cache_digest']})
    dll = args.dll or fp.dll()
    for platform in list(PLATFORMS) if args.platform == 'all' else [args.platform]:
        reports.append(build(platform, args.version, dll, variants, out, fp.out() / 'stormlib'))
        print('Built', reports[-1]['file'], reports[-1]['bytes'], 'bytes', flush=True)
    (out / 'packages.json').write_text(json.dumps({'version': args.version, 'packages': reports}, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
