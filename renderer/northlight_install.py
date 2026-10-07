#!/usr/bin/env python3
"""Northlight renderer player installer for macOS (WoWSilicon) and Windows. Never starts the game.

    northlight_install.py install   [--client C] [--locale xxXX] [--cache ZIP|DIR] [--backend dxvk|dxvk2|native|legacy]
                                 [--no-art-layer] [--no-world-cache] [--jobs N] [--yes]
    northlight_install.py uninstall [--client C] [--yes]
    northlight_install.py status    [--client C]

The launchers (Install Northlight.command, Install.cmd) run this with the package's own Python as
`python -I -B -X utf8`. Install, in this order; nothing is written before the checks pass:
1. preflight: the game and WoWSilicon are closed; wow.exe and the 3.3.5a base archives are there;
   the locale is known; the client folder is writable, ASCII-only (the renderer opens its files
   with narrow paths), not under Program Files and short enough (Windows); the package is outside
   the client; enough disk (and 8 GB RAM for a local build); macOS: WoWSilicon's dlls.txt preload
   with DXVK is active.
2. identify the client's archive chain (client_identity). A client that matches the stock variant
   gets the prebuilt cache: Northlight-cache-stock-<digest12>.zip (or the folder a browser expanded
   it to) from --cache, next to the package, or in Downloads, extracted into world-cache.extract
   with a sha256 check per file, then swapped in. A killed extraction resumes. Any other client,
   or a stock client without the zip, builds the cache locally (scripts/install_world_cache.py,
   20-40 min). A cache that is already current is kept. A patch-z that is not ours (D11) is part
   of the client: identification and the local build then read it (without=''), and the art layer
   is skipped.
3. the art layer (patch-z) is always built from the client's own Light*.dbc (build_art_layer.py)
   into the package's work folder and installed through the transaction, unless Data/patch-z.mpq
   or the locale patch-<loc>-z.mpq is someone else's (then it is skipped and reported).
4. the renderer: Windows installs d3d9.dll, the DXVK backend and the ini files in one transaction
   (install.py); macOS runs migrate_mac_proxy (mods/d3d9.dll preloaded from
   dlls.txt; wow.exe is never read or written), then the ini files and the art layer in a transaction.
One run per client at a time (<client>/northlight-installer.lock). Rerunning is safe: current parts are skipped. Uninstall restores every transaction of this
client, newest first, and deletes the world cache only if this installer or install_world_cache
made it. Every transaction's backups stay in <client>/renderer-backups. Log: <package>/logs/.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath

APP = Path(__file__).resolve().parents[1]   # the repository, or app/ inside a package
for folder in (APP / 'scripts', APP):
    if str(folder) not in sys.path:
        sys.path.insert(0, str(folder))
import client_archives  # noqa: E402


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


INSTALL = load('northlight_installer', APP / 'renderer/windows-package/install.py')
_migrate = None


def migrate():
    """renderer/migrate_mac_proxy.py (macOS only; loaded on first use)."""
    global _migrate
    if _migrate is None:
        _migrate = load('migrate_mac_proxy', APP / 'renderer/migrate_mac_proxy.py')
    return _migrate


CACHE, PREVIOUS = 'world-cache', 'world-cache.previous'
STAGING = 'world-cache.staging'   # install_world_cache.py's build folder
EXTRACT = 'world-cache.extract'   # this installer's extraction folder
LOCK = 'northlight-installer.lock'   # one installer run per client at a time
VARIANT_FORMAT = 'northlight-cache-variant/1'
LOCAL_FORMAT = 'northlight-world-cache-install/1'
VARIANT_FILE = 'northlight-cache.json'
EXTRACTING = '.northlight-extract.json'   # in the staging folder while an extraction is incomplete
LETTER = 'z'
# Our art layers so far (dev HD client, older HD, stock 3.3.5a): never treated as foreign.
KNOWN_OURS = {'a6362b62564d8b0e9bbc71e1e8e8420b4b66557c021ea0656c3c7d34a30ac4a2',
              '51571ec6380f3cc5a88f50158cc9f857dbe584a48e127de5a01b927aa8242fa8',
              '672163c83848cbb7cb9aff4870557789032b0a1d7987b864d78035dfd08b375f',
              '7169280ab24f38a60ed23f27745c5c2eedeecfb2d04dabb935e0bcbcd6ee600a',   # 0.3.178 dev HD (retimed bands)
              '019f5231a95bb67f6e6c868752dda86c05abf9cae95ba7e6b01ae48cc78392e9',   # 0.3.178 stock (retimed bands)
              '9388d875b4838804464ce5767f1677f5686ac85a847d54fef278b06f07befac6',   # 0.3.198 dev HD (storm bands, weather textures)
              '89fd3c93f97261e1aa306f5530d4ce5d792659d6c530f3b1400f214b322bc1cd',   # 0.3.198 stock (storm bands, weather textures)
              '8f6fbefe5de3f7b7c77319898c9460a94b55d786b679f32e6808d133ba8e2ffb',   # 0.3.199 dev HD (rain alpha .35)
              'f2e38013f9f03a51548bc785e40351959d1959191d69bbc478bd89056708d182'}   # 0.3.199 stock (rain alpha .35)
BASE_ARCHIVES = ('common', 'common-2', 'expansion', 'lichking', 'patch', 'patch-2', 'patch-3')
GIB = 1 << 30
LOCAL_BUILD_BYTES = 13 * GIB   # 1.2 x the ~11 GB cache
MIN_FREE = GIB // 2
PERCENT = re.compile(r': (\d+)% \(')
WINDOWS_MAX_PATH = 259
LONGEST_CACHE_NAME = 110   # world-cache.staging\models\<64 hex>.<pid>.tmp


class Refusal(Exception):
    """A preflight check failed: nothing was written (exit 2)."""


class Failed(Exception):
    """A step failed after writing started; a rerun resumes (exit 1)."""


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1 << 20), b''):
            h.update(data)
    return h.hexdigest()


def retry(fn, *args):
    """Windows: Defender or the indexer briefly holds a new file; retry renames and deletes."""
    delay = 0.2
    for attempt in range(7):
        try:
            return fn(*args)
        except PermissionError:
            if attempt == 6:
                raise
            time.sleep(delay)
            delay = min(delay * 2, 3.0)


def remove_tree(path):
    if path.exists():
        retry(shutil.rmtree, path)


def cache_path(root, name):
    """A manifest path inside a cache folder; refuses anything that could leave it."""
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '\\' in name or ':' in name or any(x in ('', '.', '..') for x in name.split('/')):
        raise Failed(f'Unsafe path in the cache manifest: {name!r}')
    return root.joinpath(*p.parts)


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def pid_alive(pid):
    if pid <= 0:
        return False
    if os.name == 'nt':
        import ctypes
        kernel = ctypes.windll.kernel32
        handle = kernel.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel.CloseHandle(handle)
        return bool(ok) and code.value == 259   # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


@contextlib.contextmanager
def locked(client):
    """<client>/northlight-installer.lock holds the pid of the run working on this client; a lock whose
    process is gone is taken over. Removed when the run ends."""
    path = client / LOCK
    for _ in range(3):
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            try:
                pid, age = int(path.read_text().split()[0]), 0
            except (OSError, ValueError, IndexError):   # being written right now, or unreadable
                try:
                    pid, age = 0, time.time() - path.stat().st_mtime
                except OSError:
                    pid, age = 0, 99
            if pid != os.getpid() and (pid_alive(pid) or (pid == 0 and age < 10)):
                raise Refusal(f'Another Northlight installer (process {pid or "starting"}) is working on this game '
                              'folder. Wait for it to finish, then run this again.')
            path.unlink(missing_ok=True)   # stale: its process is gone
            continue
        except OSError:
            raise Refusal(f'Cannot write into {client}. Check the folder permissions.')
        with os.fdopen(fd, 'w') as f:
            f.write(str(os.getpid()))
        break
    else:
        raise Refusal(f'Cannot take {path}; remove it if no installer is running.')
    try:
        yield
    finally:
        path.unlink(missing_ok=True)


def host_platform():
    return 'windows' if os.name == 'nt' else 'mac' if sys.platform == 'darwin' else None


def windows_running():
    out = subprocess.run(['tasklist.exe', '/FI', 'IMAGENAME eq wow.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                         text=True, errors='replace').stdout
    return ['wow.exe'] if 'wow.exe' in out.lower() else []


class Package:
    """The unpacked player package: app/ (this code), runtime/, payload/, variants/, BUILD-INFO.json."""

    def __init__(self, root, platform):
        self.root, self.platform = Path(root), platform
        self.info = read_json(self.root / 'BUILD-INFO.json') or {}
        self.version = self.info.get('version', 'dev')
        self.payload = self.root / 'payload'
        self.dll = self.payload / 'd3d9.dll'
        self.stormlib = self.root / 'runtime' / ('StormLib.dll' if platform == 'windows' else 'lib/libstorm.dylib')
        self.variants = {p.stem: read_json(p) for p in sorted((self.root / 'variants').glob('*.json'))}
        self.work = self.root / 'work'
        self.logs = self.root / 'logs'


class Source:
    """Files of a cache variant: a zip, or the folder a browser expanded it to."""

    def __init__(self, path):
        self.path = Path(path)
        self.zip = zipfile.ZipFile(self.path) if self.path.is_file() else None

    def open(self, name):
        return self.zip.open(name) if self.zip else (self.path / name).open('rb')

    def manifest(self):
        try:
            with self.open(VARIANT_FILE) as f:
                return json.loads(f.read().decode('utf-8'))
        except (OSError, KeyError, ValueError):
            return None

    def close(self):
        if self.zip:
            self.zip.close()


class Installer:
    """install / uninstall / status. The hooks (running, free_bytes, ram, identify_chain, run, ask,
    search_dirs) are attributes so tests can replace them."""

    def __init__(self, package, platform=None, yes=False, out=None):
        self.pkg = package
        self.platform = platform or package.platform
        self.yes = yes
        self.out = out or sys.stdout
        self.log = None
        self.running = (lambda: migrate().running()) if self.platform == 'mac' else windows_running
        self.free_bytes = lambda path: shutil.disk_usage(path).free
        self.ram = None   # installed-memory probe; None = install_world_cache.total_memory
        self.search_dirs = [package.root.parent, package.root, Path.home() / 'Downloads']

    # ---- output ----

    def say(self, *parts, console=True):
        line = ' '.join(str(p) for p in parts)
        if console:
            print(line, file=self.out, flush=True)
        if self.log:
            self.log.write(line + '\n')
            self.log.flush()

    def open_log(self, action):
        try:
            self.pkg.logs.mkdir(parents=True, exist_ok=True)
            self.log = (self.pkg.logs / f'northlight-{action}-{datetime.now():%Y%m%d-%H%M%S}.log').open('a', encoding='utf-8')
        except OSError:
            self.log = None

    def ask(self, question, default):
        """y/N (default False) or Y/n (default True). --yes accepts only questions whose default is yes."""
        if default and self.yes:
            return True
        if not sys.stdin or not sys.stdin.isatty():
            return default
        answer = input(question + (' [Y/n] ' if default else ' [y/N] ')).strip().lower()
        return default if not answer else answer in ('y', 'yes')

    # ---- children ----

    def child_command(self, script, args):
        return [sys.executable, '-I', '-B', '-X', 'utf8', str(script), *map(str, args)]   # -B: no __pycache__ in the package

    def child_env(self):
        env = dict(os.environ, NORTHLIGHT_OUT=str(self.pkg.work / 'out'))
        if self.pkg.stormlib.is_file():
            env['NORTHLIGHT_STORMLIB'] = str(self.pkg.stormlib)
        return env

    def run(self, script, args, show=True):
        """Run a pipeline script under this Python. Every line goes to the log; the console gets only its
        readable progress lines (show=True; JSON and report dumps stay in the log). On a failure the last
        lines are shown. Returns (exit code, the last JSON object it printed or None)."""
        proc = subprocess.Popen(self.child_command(script, args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding='utf-8', errors='replace', env=self.child_env(), bufsize=1)
        last, tail = None, []
        for line in proc.stdout:
            line = line.rstrip('\n')
            tail = (tail + [line])[-25:]
            report = line.lstrip().startswith(('{', '}', '[', ']', '"'))
            step = PERCENT.search(line)   # install_world_cache's 5% steps: the console shows every 10%
            self.say('  ' + line, console=show and not report and not (step and int(step.group(1)) % 10))
            if line.startswith('{'):
                try:
                    last = json.loads(line)
                except ValueError:
                    pass
        code = proc.wait()
        if code != 0 and not show:
            for line in tail:
                print('  ' + line, file=self.out, flush=True)
        return code, last

    # ---- client identity ----

    def identify_chain(self, client, locale, without=LETTER):
        """(variant name or None, [differences]) of the client's archive chain against the packaged variants.
        without: '' when the client's z letter is someone else's, so its archives count."""
        if not self.pkg.variants:
            return None, ['this package has no prebuilt cache variants']
        import client_identity
        result = client_identity.identify(client, locale, self.pkg.variants, without=without)
        return result['variant'], list(result.get('differences') or [])

    # ---- preflight ----

    def resolve_client(self, raw):
        if raw is None:
            raw = self.prompt_client()
        text = str(raw).strip().strip('"').strip("'")
        if self.platform == 'mac':
            text = text.replace('\\ ', ' ')   # a folder dragged into Terminal
        client = Path(text).expanduser()
        if not client.is_dir():
            raise Refusal(f'Game folder not found: {text}')
        return client.resolve()

    def prompt_client(self):
        if self.platform == 'mac':
            try:
                versions = json.loads(migrate().VERSIONS.read_text(encoding='utf-8')).get('versions', {})
                found = sorted({str(Path(v['game_path']).parent) for v in versions.values() if v.get('game_path')})
            except (OSError, ValueError, AttributeError):
                found = []
            for i, path in enumerate(found, 1):
                self.say(f'  {i}) {path}')
            raw = input('Game folder (a number above, or drag the folder here) and Enter: ').strip()
            return found[int(raw) - 1] if raw.isdigit() and 0 < int(raw) <= len(found) else raw
        return input('Game folder (the folder with wow.exe): ')

    def preflight(self, client, locale):
        """Read-only checks; returns the locale. Raises Refusal with what to do next."""
        root = self.pkg.root.resolve()
        if root == client or client in root.parents:
            raise Refusal('The package is inside the game folder. Move the package folder somewhere else '
                          '(for example Downloads) and run it from there.')
        if root in client.parents:
            raise Refusal('The game folder is inside the package folder. Choose the real game folder.')
        if not str(client).isascii():
            raise Refusal(f'The game folder path has non-ASCII characters: {client}\n'
                          'The renderer cannot open its files there. Move or rename the folder so that the whole '
                          'path uses only A-Z, 0-9, spaces and simple punctuation, then run the installer again.')
        if self.platform == 'windows':
            if len(str(client)) + LONGEST_CACHE_NAME > WINDOWS_MAX_PATH:
                raise Refusal(f'The game folder path is too long ({len(str(client))} characters; at most '
                              f'{WINDOWS_MAX_PATH - LONGEST_CACHE_NAME}). Move the game to a shorter path such as C:\\Games\\WoW.')
            for var in ('ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432'):
                base = os.environ.get(var)
                if base and (client == Path(base) or Path(base) in client.parents):
                    raise Refusal('The game is under Program Files, where writes need administrator rights and '
                                  'are redirected. Move the game folder to for example C:\\Games\\WoW.')
        entries = {p.name.lower() for p in client.iterdir()}
        if 'wow.exe' not in entries:
            raise Refusal(f'wow.exe is not in {client}. Choose the folder that holds wow.exe.')
        try:
            data = client_archives.data_dir(client)
        except FileNotFoundError:
            raise Refusal(f'{client} has no Data folder. Choose a complete WoW 3.3.5a game folder.')
        present = {p.name.lower() for p in data.iterdir()}
        missing = [n + '.mpq' for n in BASE_ARCHIVES if n + '.mpq' not in present]
        if missing:
            raise Refusal('This is not a complete WoW 3.3.5a client; Data is missing: ' + ', '.join(missing))
        try:
            locale = client_archives.detect_locale(client, locale)
        except ValueError:
            raise Refusal('The game has several languages and WTF/Config.wtf does not say which one is used. '
                          'Run the installer again with --locale, for example --locale enUS. Installed: '
                          + ', '.join(client_archives.installed_locales(client)))
        except FileNotFoundError as e:
            raise Refusal(str(e))
        local = {p.name.lower() for p in client_archives.locale_dir(client, locale).iterdir()}
        missing = [n for n in (f'locale-{locale}.mpq', f'patch-{locale}.mpq', f'patch-{locale}-2.mpq',
                               f'patch-{locale}-3.mpq') if n.lower() not in local]
        if missing:
            raise Refusal(f'Data/{locale} is missing: ' + ', '.join(missing))
        busy = self.running()
        if busy:
            raise Refusal('Close these first, then run the installer again: ' + ', '.join(busy))
        probe = client / f'.northlight-write-test-{os.getpid()}'
        try:
            probe.write_bytes(b'')
            probe.unlink()
        except OSError:
            raise Refusal(f'Cannot write into {client}. Check the folder permissions.')
        return locale

    def check_payload(self, backend='dxvk'):
        """The package's own files match its manifest (a damaged download is refused). The files of a DXVK
        backend other than the selected one may be missing or damaged (antivirus products remove DXVK builds):
        they are reported and skipped by install.payload_plan."""
        manifest = read_json(self.pkg.root / 'payload-manifest.json')
        if manifest is None or not self.pkg.dll.is_file():
            raise Refusal('The package is incomplete (payload-manifest.json or payload/d3d9.dll missing). '
                          'Unzip the download again.')
        for e in manifest:
            path = self.pkg.payload / e['path']
            other = INSTALL.other_dxvk(e['path'], backend)
            if path.is_file():
                if INSTALL.package_sha(path) != e['sha256']:
                    raise Refusal(INSTALL.package_problem(e['path'], backend, False, 'payload/' + e['path']))
            elif self.platform == 'windows' and other:
                self.say(f'Note: payload/{e["path"]} is missing (an antivirus product may have removed it); '
                         f'backend {other} will not be available.')
            else:
                raise Refusal(INSTALL.package_problem(e['path'], backend, True, 'payload/' + e['path']))
        expected = self.pkg.info.get('dll_sha256')
        if expected and INSTALL.package_sha(self.pkg.dll) != expected:
            raise Refusal('Package file damaged: payload/d3d9.dll. Download and unzip the package again.')

    # ---- world cache ----

    def cache_owner(self, client):
        """('variant', manifest) | ('local', manifest) | ('foreign', None) | (None, None) for <client>/world-cache."""
        cache = client / CACHE
        if not cache.exists():
            return None, None
        variant = read_json(cache / VARIANT_FILE)
        if variant and variant.get('format') == VARIANT_FORMAT:
            return 'variant', variant
        local = read_json(cache / 'install-manifest.json')
        if local and local.get('format') == LOCAL_FORMAT:
            return 'local', local
        return 'foreign', None

    def find_source(self, variant, manifest, given):
        """The cache zip or expanded folder for this variant, or None."""
        prefix = f'Northlight-cache-{variant}-{manifest["cache_digest"][:12]}'
        if given:
            candidates = [Path(given).expanduser()]
        else:
            candidates = [p for d in self.search_dirs if d.is_dir() for p in sorted(d.glob(prefix + '*'))]
        for path in candidates:
            if not (path.is_dir() or zipfile.is_zipfile(path)):
                continue
            try:
                source = Source(path)
            except (OSError, zipfile.BadZipFile):
                continue
            found = source.manifest()
            if found and found.get('cache_digest') == manifest['cache_digest']:
                return source
            source.close()
        if given:
            raise Refusal(f'{given} is not the {variant} world cache for this package '
                          f'(expected {prefix}.zip).')
        return None

    def recover(self, client):
        """A run killed between the two renames of a swap leaves only world-cache.previous: put it back."""
        cache, previous = client / CACHE, client / PREVIOUS
        if previous.exists() and not cache.exists():
            retry(previous.rename, cache)
            self.say('Recovered the previous world cache after an interrupted swap.')

    def swap(self, client):
        cache, staging, previous = client / CACHE, client / EXTRACT, client / PREVIOUS
        remove_tree(previous)
        if cache.exists():
            retry(cache.rename, previous)
        retry(staging.rename, cache)
        remove_tree(previous)

    def extract(self, client, source, manifest):
        """Stream every manifest file into world-cache.extract with a sha256 check, then swap it in.
        A killed extraction resumes: files already verified are kept. (world-cache.staging belongs to
        install_world_cache.py and is left alone.)"""
        staging = client / EXTRACT
        self.recover(client)
        state = read_json(staging / EXTRACTING)
        done = read_json(staging / VARIANT_FILE)
        if staging.exists() and not (state or done or {}).get('cache_digest') == manifest['cache_digest']:
            self.say('Removing an unfinished extraction of another cache version...')
            remove_tree(staging)
        files = manifest['files']
        total = sum(meta['bytes'] for meta in files.values())
        if not (done and done.get('cache_digest') == manifest['cache_digest']):
            staging.mkdir(exist_ok=True)
            INSTALL.atomic_json(staging / EXTRACTING, {'cache_digest': manifest['cache_digest']})
            written, step, next_report = 0, max(total // 10, 1), 0
            self.say(f'Installing the world cache: {len(files)} files, {total / GIB:.1f} GiB ...')
            for name, meta in sorted(files.items()):
                target = cache_path(staging, name)
                if not (target.is_file() and target.stat().st_size == meta['bytes'] and sha(target) == meta['sha256']):
                    target.parent.mkdir(parents=True, exist_ok=True)
                    part = target.with_name(target.name + '.part')
                    h = hashlib.sha256()
                    with source.open(name) as src, part.open('wb') as dst:
                        for data in iter(lambda: src.read(1 << 20), b''):
                            h.update(data)
                            dst.write(data)
                    if h.hexdigest() != meta['sha256']:
                        part.unlink()
                        raise Failed(f'The cache download is damaged ({name}). Download {source.path.name} again '
                                     'and run the installer again.')
                    retry(os.replace, part, target)
                written += meta['bytes']
                if written >= next_report:
                    self.say(f'  world cache {100 * written // max(total, 1)}%')
                    next_report += step
            for part in staging.rglob('*.part'):
                part.unlink()
            # Written last: a staging folder with northlight-cache.json is complete.
            INSTALL.atomic_json(staging / VARIANT_FILE, manifest)
            (staging / EXTRACTING).unlink()
        self.still_closed('swapping in the world cache (it waits complete in world-cache.extract)')
        self.swap(client)
        self.say('World cache installed.')

    def build_cache(self, client, locale, jobs, without=LETTER):
        """Build the cache from this client's own archives (install_world_cache.py; no-op when current)."""
        self.still_closed('building the world cache')
        self.say('Building the world cache from this client (20-40 minutes; a rerun resumes if interrupted)...')
        args = ['--client', client, '--locale', locale, '--output', client / CACHE, '--without', without,
                '--progress', 'human'] + (['--jobs', jobs] if jobs else [])
        code, last = self.run(APP / 'scripts/install_world_cache.py', args)
        if code != 0:
            raise Failed(f'The world cache build failed (exit {code}): {json.dumps(last) if last else "see the log"}. '
                         'Run the installer again to resume.')
        return (last or {}).get('event', 'done')

    def archive_warnings(self, client):
        """The archives the cache this client built could not list or open (its install-manifest's tolerated)."""
        manifest = read_json(client / CACHE / 'install-manifest.json')
        tolerated = manifest.get('tolerated') if isinstance(manifest, dict) else None
        return (tolerated.get('archive_warnings') if isinstance(tolerated, dict) else None) or []

    # ---- art layer ----

    def our_shas(self, client, name):
        """sha256 values this installer ever wrote to <name> in this client (any transaction)."""
        found = set()
        for path in (client / 'renderer-backups').glob('*/transaction.json'):
            record = read_json(path) or {}
            if Path(record.get('client', '')).resolve() == client:
                found |= {e['after'] for e in record.get('files', []) if e['path'].lower() == name.lower() and e['after']}
        return found

    def foreign_z(self, client, locale):
        """[(client path, sha256)] of Data/patch-z.mpq and Data/<loc>/patch-<loc>-z.mpq files that are not ours
        (D11: ours = a known layer or one our transactions wrote). Decided before identification: a foreign
        z is part of the client, so identification and a local build must read it."""
        found = []
        for folder, name in ((client_archives.data_dir(client), f'patch-{LETTER}.mpq'),
                             (client_archives.locale_dir(client, locale), f'patch-{locale.lower()}-{LETTER}.mpq')):
            path = client_archives._entries(folder).get(name)
            if path is None or not path.is_file():
                continue
            rel, current = path.relative_to(client).as_posix(), sha(path)
            if current not in KNOWN_OURS | self.our_shas(client, rel):
                found.append((rel, current))
        return found

    def art_layer(self, client, locale):
        """({client path: bytes} to install, None, {client path: action}) or ({}, reason it is skipped, {}).
        The action per target is installed (absent before), replaced (another layer of ours) or kept-identical."""
        if not (client / CACHE / 'fog').is_dir():
            return {}, 'no world cache (its fog zones are an input)', {}
        work = self.pkg.work / 'art-layer'
        remove_tree(work)
        work.mkdir(parents=True)
        code, _ = self.run(APP / 'build_art_layer.py', ['--client', client, '--locale', locale, '--world-cache',
                                                         client / CACHE, '--output', work, '--letter', LETTER], show=False)
        report = read_json(work / 'art-layer-manifest.json')
        if code != 0 or not report:
            raise Failed(f'The art layer build failed (exit {code}); see the log.')
        layer = {t: (work / t).read_bytes() for t in report['targets']}
        remove_tree(work)
        steps = report.get('steps', [])
        skipped = sum(1 for step in steps if 'skipped' in step)
        self.say(f'Art layer: built from this client\'s Light tables ({len(steps) - skipped} steps applied, '
                 f'{skipped} skipped; details in the log)')
        actions = {}
        for name, data in layer.items():
            target = client / name
            new = hashlib.sha256(data).hexdigest()
            if not target.is_file():
                actions[name] = 'installed'
                continue
            current = sha(target)
            if current not in KNOWN_OURS | {new} | self.our_shas(client, name):
                return {}, f'{name} belongs to another mod (sha256 {current[:12]}); it is left as it is', {}
            actions[name] = 'kept-identical' if current == new else 'replaced'
        return layer, None, actions

    # ---- transactions ----

    def mac_backups(self, client):
        return client / 'renderer-backups' / 'mac-proxy'

    def all_transactions(self, client):
        """Unrestored transactions of this client, newest first: [(folder, record)]. At the same second a
        package transaction is newer than the macOS migration (install always runs the migration first)."""
        found = [(b, r) for b, r in INSTALL.transactions(client)]
        if self.platform == 'mac' and self.mac_backups(client).is_dir():
            m = migrate()
            found += [(b, read_json(b / 'transaction.json')) for b in m.backups(client, [self.mac_backups(client)])
                      if (read_json(b / 'transaction.json') or {}).get('status') != 'restored']
        return sorted(found, key=lambda br: (br[0].name[:15], br[1].get('kind', 'package') == 'package',
                                              br[1].get('created', ''), br[0].name),
                      reverse=True)

    def restore(self, client, backup, record):
        if record.get('kind', 'package') == 'package':
            INSTALL.restore(client, backup)
            return
        m = migrate()
        for line in m.undo(client, record['files'], backup):
            self.say('  ' + line)
        record['status'] = 'restored'
        INSTALL.atomic_json(backup / 'transaction.json', record)

    def legacy(self, client):
        """D13: an old full package (with world-cache/ in its transaction) is restored first, or refused. Package
        updates installed on top of it go with it: the whole chain from the oldest such record up is returned."""
        old = INSTALL.legacy_cache_transactions(client)
        if not old:
            return []
        every = INSTALL.transactions(client)
        old = every[[b for b, _ in every].index(old[0][0]):]
        problems = INSTALL.chain_problems(client, list(reversed(old)))
        if problems:
            raise Refusal('An older renderer package with its own world-cache is installed, and its files have '
                          'changed since, so it cannot be restored automatically: ' + problems[0][1] +
                          '\nRestore or remove that installation first (its backups are in renderer-backups).')
        versions = ', '.join(sorted({r.get('version', '?') for _, r in old}))
        if not self.ask(f'An older renderer package ({versions}) is installed with its own world-cache. '
                        'Restore it first (recommended)?', True):
            raise Refusal('The older package must be restored first; nothing changed.')
        return old

    # ---- actions ----

    def memory_refusal(self, client):
        """The orchestrator's own rule (install_world_cache.memory_refusal: installed RAM, full builds only),
        so the front end is never stricter. A cache it built may need only an incremental rebuild: then the
        orchestrator alone decides (its exit 2 carries the same message)."""
        import install_world_cache as iwc
        if self.cache_owner(client)[0] == 'local':
            return None
        return iwc.memory_refusal('full', self.ram() if self.ram else iwc.total_memory())

    def still_closed(self, step):
        """M2: the game or WoWSilicon may have been started during a long step; nothing is written while it runs."""
        busy = self.running()
        if busy:
            raise Failed(f'{", ".join(busy)} started while the installer was working, so it stopped before {step}. '
                         'Close it and run the installer again: the finished parts are kept.')

    def legacy_letters(self, old):
        """Patch letters that restoring these old full packages will remove (they added Data/patch-<x>.mpq or
        Data/<loc>/patch-<loc>-<x>.mpq): identification must not count them (patch-y shipped with 0.3.98-0.3.144)."""
        letters = set()
        for _, record in old:
            for e in record['files']:
                name = e['path'].rsplit('/', 1)[-1].lower()
                if e['before'] is None and name.startswith('patch-') and name.endswith('.mpq') and \
                        len(name.split('-')[-1]) == 5 and name.split('-')[-1][0].isalpha():
                    letters.add(name.split('-')[-1][0])
        return ''.join(sorted(letters))

    def complete_cache(self, client, manifest):
        """The installed variant cache has every manifest file at its size (cheap: stat only)."""
        cache = client / CACHE
        for name, meta in manifest['files'].items():
            try:
                if cache_path(cache, name).stat().st_size != meta['bytes']:
                    return False
            except OSError:
                return False
        return True

    def drop_previous(self, client):
        """S2: a swap killed after its second rename leaves world-cache.previous (about 10 GB); once the live
        cache is ours and complete, a previous folder carrying our marker or manifest is deleted."""
        previous = client / PREVIOUS
        if previous.exists() and self.cache_owner(client)[0] in ('variant', 'local') and \
                ((previous / VARIANT_FILE).exists() or (previous / 'install-manifest.json').exists()):
            self.say('Removing world-cache.previous left by an interrupted run...')
            remove_tree(previous)

    def install(self, client, locale=None, cache=None, backend=None, art=True, world_cache=True, jobs=None):
        client = self.resolve_client(client)
        plan = self.plan_install(client, locale, cache, backend, art, world_cache)
        with locked(client):   # S5: taken once the read-only checks have passed
            return self.apply_install(plan, jobs)

    def plan_install(self, client, locale, cache, backend, art, world_cache):
        """Every check, read-only: returns what apply_install does. Raises Refusal."""
        self.say(f'Northlight renderer {self.pkg.version} installer; game folder: {client}')
        backend = INSTALL.resolve_backend(client, backend)   # called once; payload_plan takes the result
        locale = self.preflight(client, locale)
        self.check_payload(backend)
        marker = INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER)   # a linked renderer-backends folder is refused here
        if self.platform == 'windows' and marker.is_file():
            self.say('Note: Northlight no longer switches to DXVK 2.7.1 by itself; the leftover '
                     'renderer-backends\\dxvk\\northlight-dxvk3-init.pending will be removed by this install.')
        if self.platform == 'windows' and backend == 'dxvk':
            self.say('Graphics backend: DXVK 3.1.1. On AMD RX 5000/6000 cards or drivers DXVK 3 does not support, '
                     'run Install.cmd --backend dxvk2 (DXVK 2.7.1). If the game closes at start with DXVK 3, choose '
                     '--backend dxvk2 yourself; Northlight does not switch by itself.')
        if self.platform == 'windows' and backend == 'legacy':
            proxy = client / INSTALL.PROXY
            if not ((proxy.is_file() and not INSTALL.is_ours(client)) or (client / INSTALL.LEGACY).is_file()):
                raise Refusal('--backend legacy keeps the d3d9.dll found in the game folder as the backend, '
                              'but there is none. Use --backend dxvk, dxvk2 or native.')
        if self.platform == 'mac':
            try:
                migrate().plan(client, self.pkg.dll, roots=[self.mac_backups(client)])
            except ValueError as e:
                raise Refusal(str(e))
        old = self.legacy(client)
        foreign = self.foreign_z(client, locale)
        # An old full package's letters (patch-y) go away before the cache step: identify without them.
        without = ('' if foreign else LETTER) + ''.join(x for x in self.legacy_letters(old) if x != LETTER)
        for rel, digest in foreign:
            self.say(f'{rel} belongs to another mod (sha256 {digest[:12]}); it is kept, and its content is part of '
                     'this client.')
        try:
            variant, differences = self.identify_chain(client, locale, without)
        except (ValueError, OSError) as e:
            raise Refusal(f'Cannot read the game archives: {e}. Check that the client is complete.')
        manifest = self.pkg.variants.get(variant) if variant else None
        owner, current = self.cache_owner(client)
        source, action = None, 'skip'
        if world_cache:
            if manifest and owner == 'variant' and current.get('cache_digest') == manifest['cache_digest'] and \
                    self.complete_cache(client, manifest):
                action = 'keep'
            elif manifest:
                source = self.find_source(variant, manifest, cache)
                action = 'extract' if source else 'build'
            else:
                action = 'build'
        if variant:
            self.say(f'Client: {variant} 3.3.5a archives ({locale}).')
        else:
            self.say(f'Client: modified archives ({locale}); no prebuilt cache matches:')
            for line in differences[:12]:
                self.say('  ' + str(line))
        if action == 'build' and manifest:
            self.say(f'The prebuilt cache Northlight-cache-{variant}-{manifest["cache_digest"][:12]}.zip was not found next to '
                     'the package or in Downloads; the cache is built from this client instead.')
        if action == 'extract' and owner == 'local':
            self.say('The prebuilt cache replaces the cache this client built earlier (once).')
        if action in ('extract', 'build') and owner == 'foreign' and old == []:
            if not self.ask(f'{client / CACHE} was not made by this installer. Replace it?', False):
                raise Refusal('The existing world-cache was kept; nothing changed. Move it away or answer yes.')
        need = MIN_FREE + (sum(m['bytes'] for m in manifest['files'].values()) if action == 'extract' else
                           LOCAL_BUILD_BYTES if action == 'build' else 0)
        free = self.free_bytes(client)
        if free < need:
            raise Refusal(f'Not enough free disk space on the game drive: {free / GIB:.1f} GiB free, '
                          f'{need / GIB:.1f} GiB needed.')
        if action == 'build':
            refusal = self.memory_refusal(client)
            if refusal:
                raise Refusal(refusal[0].upper() + refusal[1:] + '. Run again with --no-world-cache to install the '
                              'renderer without static world shadows and GI.')
        self.say('Checks passed.')
        return {'client': client, 'locale': locale, 'backend': backend, 'art': art, 'old': old, 'foreign': foreign,
                'without': without, 'variant': variant, 'manifest': manifest, 'source': source, 'action': action}

    def apply_install(self, p, jobs):
        client, locale, action, foreign = p['client'], p['locale'], p['action'], p['foreign']
        self.still_closed('changing anything')
        for backup, _ in reversed(p['old']):
            self.say('Restoring the older package first:', backup)
            INSTALL.restore(client, backup)
        self.recover(client)
        self.drop_previous(client)
        report = {'client': str(client), 'locale': locale, 'variant': p['variant'], 'archive_warnings': []}
        try:
            if action == 'extract':
                self.extract(client, p['source'], p['manifest'])
                report['world_cache'] = f'extracted (prebuilt {p["variant"]} cache {p["manifest"]["cache_digest"][:12]})'
            elif action == 'build':
                event = self.build_cache(client, locale, jobs, p['without'])
                report['world_cache'] = 'kept (up to date)' if event == 'up_to_date' else 'built from this client'
                report['archive_warnings'] = self.archive_warnings(client)
            elif action == 'keep':
                report['world_cache'] = f'kept (prebuilt {p["variant"]} cache {p["manifest"]["cache_digest"][:12]})'
            else:
                report['world_cache'] = 'not installed (--no-world-cache)'
        finally:
            if p['source']:
                p['source'].close()
        if not p['art']:
            layer, why, actions = {}, 'disabled with --no-art-layer', {}
        elif foreign:
            layer, why, actions = {}, f'{foreign[0][0]} belongs to another mod; it is left as it is', {}
        else:
            layer, why, actions = self.art_layer(client, locale)
        if why:
            report['art_layer'] = 'skipped: ' + why
        else:
            report['art_layer'] = '; '.join(f'{action} {hashlib.sha256(layer[name]).hexdigest()[:8]} {name}'
                                            for name, action in actions.items())
        backups, payload = [], 'kept (unchanged)'
        if self.platform == 'mac':
            self.still_closed('installing the renderer')
            record = migrate().apply(client, self.pkg.dll, self.mac_backups(client))
            if record:
                backups.append(record)
                payload = 'installed (mods/d3d9.dll preloaded from dlls.txt)'
            plan, staged, _ = INSTALL.payload_plan(client, self.pkg.root, extra=layer, proxy=False)
        else:
            plan, staged, _ = INSTALL.payload_plan(client, self.pkg.root, backend=p['backend'], extra=layer)
        files = [e['path'] for e in plan if e['path'] not in layer]
        if plan:
            self.still_closed('installing the renderer files')
            backups.append(INSTALL.commit(client, plan, staged, self.pkg.payload, self.pkg.version))
            if files:
                payload = ('installed: ' if payload.startswith('kept') else payload + '; ') + ', '.join(files)
        if self.platform == 'windows':   # the leftover of 0.3.189-0.3.194, whatever the backend; only after the commit
            INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER).unlink(missing_ok=True)
        report.update(payload=payload, backups=[str(b) for b in backups])
        changed = backups or not report['world_cache'].startswith(('kept', 'not'))
        self.say('')
        self.say(f'Northlight renderer {self.pkg.version}: ' + ('installed.' if changed else 'already installed; nothing changed.'))
        self.say(f'  world cache: {report["world_cache"]}')
        if report['archive_warnings']:   # shown during the build too; repeated here, where it is not scrolled away
            import install_world_cache as iwc
            for warning in report['archive_warnings']:
                self.say('    ' + iwc.archive_warning_text(warning))
        self.say(f'  art layer:   {report["art_layer"]}')
        self.say(f'  renderer:    {payload}' + (f' (backend {p["backend"]})' if self.platform == 'windows' else ''))
        for b in backups:
            self.say('  rollback:   ', b)
        self.say('Start WoW yourself; this installer never starts the game.')
        return report

    def uninstall(self, client):
        client = self.resolve_client(client)
        busy = self.running()
        if busy:
            raise Refusal('Close these first, then run the uninstaller again: ' + ', '.join(busy))
        try:
            INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER)   # a linked renderer-backends folder is refused before any restore
        except ValueError as e:
            raise Refusal(str(e))
        found = self.all_transactions(client)
        problems = INSTALL.chain_problems(client, found)
        if problems:
            backup, problem = problems[0]
            raise Refusal(f'Cannot restore {backup.name}: {problem}. Nothing was changed.')
        with locked(client):
            return self.apply_uninstall(client, found)

    def apply_uninstall(self, client, found):
        for backup, record in found:
            self.say('Restoring', backup)
            self.restore(client, backup, record)
        owner, _ = self.cache_owner(client)
        if owner in ('variant', 'local'):
            self.say('Deleting the world cache...')
            remove_tree(client / CACHE)
        elif owner == 'foreign':
            self.say(f'{client / CACHE} was not made by this installer; left in place.')
        for name in (STAGING, EXTRACT, PREVIOUS):   # ours only: each carries our marker or manifest
            leftover = client / name
            if leftover.exists() and ((leftover / EXTRACTING).exists() or (leftover / VARIANT_FILE).exists() or
                                      (leftover / 'install-state.json').exists() or
                                      (leftover / 'install-manifest.json').exists()):
                remove_tree(leftover)
        if self.platform == 'windows':
            INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER).unlink(missing_ok=True)   # leftover of 0.3.189-0.3.194; no record holds it
            for folder in ('renderer-backends/dxvk', 'renderer-backends/dxvk2', 'renderer-backends/legacy', 'renderer-backends'):
                try:
                    (client / folder).rmdir()   # only when empty
                except OSError:
                    pass
        self.say('Northlight renderer removed.' if found or owner in ('variant', 'local') else 'Nothing to remove.')
        left = self.foreign_proxy(client)
        if left:
            self.say(left)
        self.say(f'Backups stay in {client / "renderer-backups"}.')
        return {'restored': [str(b) for b, _ in found], 'world_cache_deleted': owner in ('variant', 'local'),
                'proxy_left': bool(left)}

    def foreign_proxy(self, client):
        """S4: after our transactions are restored, a renderer that still loads was installed by another tool
        (the developer tools: renderer_status.py or migrate_mac_proxy with a repository backup root). It is
        reported with the command that undoes it, never removed here."""
        if self.platform == 'mac':
            state = migrate().status(client)
            if not state['proxy_preloaded']:
                return None
            return ('NOTE: the renderer is still installed (mods/d3d9.dll in dlls.txt, version '
                    f'{state["proxy_version"]}), but not by this package, so it was left as it is. It was installed '
                    'by the developer tools; undo it with them, with the game closed:\n'
                    f'  python3 <repository>/renderer_status.py off        (from {client}, if the repository is there)\n'
                    f'  python3 <repository>/renderer/migrate_mac_proxy.py --client "{client}" --restore --apply')
        proxy = client / INSTALL.PROXY
        if INSTALL.is_ours(client):
            return (f'NOTE: {proxy} is a Northlight renderer that this package did not install, so it was left as it '
                    'is. Remove it with the tool that installed it (an older package: its Restore.cmd), or delete it '
                    'with the game closed.')
        return None

    def status(self, client):
        client = self.resolve_client(client)
        owner, manifest = self.cache_owner(client)
        state = {'package_version': self.pkg.version, 'client': str(client),
                 'world_cache': {'owner': owner, 'variant': (manifest or {}).get('variant'),
                                 'digest': (manifest or {}).get('cache_digest') or (manifest or {}).get('fingerprint')},
                 'transactions': [{'backup': str(b), 'kind': r.get('kind', 'package'), 'version': r.get('version'),
                                   'files': len(r.get('files', []))} for b, r in self.all_transactions(client)],
                 'running': self.running(), 'game_started_by_this_tool': False}
        try:
            locale = client_archives.detect_locale(client)
            state['locale'] = locale
            state['variant'] = self.identify_chain(client, locale, '' if self.foreign_z(client, locale) else LETTER)[0]
        except (ValueError, OSError, ImportError) as e:
            state['identity_error'] = str(e)
        if self.platform == 'mac':
            state['mac_proxy'] = migrate().status(client)
        self.say(json.dumps(state, indent=2))
        return state


def package_root():
    """The package folder (app/../) when this file runs from a package."""
    root = APP.parent
    return root if APP.name == 'app' and (root / 'BUILD-INFO.json').is_file() else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', nargs='?', choices=['install', 'uninstall', 'status'])
    # The launchers pass their own action here, so `Install Northlight.command status --client X` works too.
    ap.add_argument('--default-action', choices=['install', 'uninstall', 'status'], help=argparse.SUPPRESS)
    ap.add_argument('--client', help='the game folder (the folder with wow.exe); asked when missing')
    ap.add_argument('--locale', help='the game language, e.g. enUS (default: WTF/Config.wtf, else the only one)')
    ap.add_argument('--cache', help='the Northlight-cache-<variant>-<digest>.zip (or its expanded folder)')
    ap.add_argument('--backend', choices=['dxvk', 'dxvk2', 'native', 'legacy'], default=None,
                    help='Windows: the Direct3D 9 behind the renderer (default dxvk = DXVK 3.1.1, or the dxvk2 or native already '
                         'installed; dxvk2 = DXVK 2.7.1 for AMD RX 5000/6000 or drivers DXVK 3 does not support; '
                         'legacy = the d3d9.dll found in the game folder)')
    ap.add_argument('--no-art-layer', action='store_true', help='do not install the lighting art layer (patch-z)')
    ap.add_argument('--no-world-cache', action='store_true', help='install the renderer without a world cache')
    ap.add_argument('--jobs', type=int, help='parallel builders for a local cache build (default: by memory)')
    ap.add_argument('--yes', action='store_true', help='answer the recommended default to every question')
    ap.add_argument('--package', type=Path, help=argparse.SUPPRESS)   # development: a package folder to use
    args = ap.parse_args(argv)
    args.action = args.action or args.default_action
    if not args.action:
        ap.error('choose install, uninstall or status')
    platform = host_platform()
    if platform is None:
        print('ERROR: this installer runs on macOS (WoWSilicon) or Windows.', file=sys.stderr)
        return 2
    root = args.package or package_root()
    if root is None:
        print('ERROR: run this from an unpacked Northlight package (Install Northlight.command or Install.cmd).',
              file=sys.stderr)
        return 2
    installer = Installer(Package(root, platform), platform, yes=args.yes)
    installer.open_log(args.action)
    try:
        if args.action == 'install':
            installer.install(args.client, args.locale, args.cache, args.backend, not args.no_art_layer,
                              not args.no_world_cache, args.jobs)
        elif args.action == 'uninstall':
            installer.uninstall(args.client)
        else:
            installer.status(args.client)
        return 0
    except Refusal as e:
        installer.say('STOPPED:', e)
        return 2
    except (Failed, ValueError, OSError) as e:
        installer.say('ERROR:', e)
        return 1
    except KeyboardInterrupt:
        installer.say('Interrupted. Run the installer again to continue.')
        return 1
    finally:
        if installer.log:
            installer.log.close()


if __name__ == '__main__':
    sys.exit(main())
