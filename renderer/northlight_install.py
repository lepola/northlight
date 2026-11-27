#!/usr/bin/env python3
"""Northlight renderer player installer for macOS (WoWSilicon), Windows and Linux (Wine/Proton). Never starts the game.

    northlight_install.py install   [--client C] [--locale xxXX] [--cache ZIP|DIR] [--backend dxvk|dxvk2|native|legacy]
                                 [--sky current|experimental-preserve] [--no-art-layer] [--no-world-cache] [--instances SPEC] [--jobs N] [--yes]
    northlight_install.py uninstall [--client C] [--yes]
    northlight_install.py status    [--client C]

The launchers (Install Northlight.command, Install.cmd, install.sh) run this with the package's own Python as
`python -I -B -X utf8`. Install, in this order; nothing is written before the checks pass:
1. preflight: the game and WoWSilicon are closed; wow.exe and the 3.3.5a base archives are there;
   the locale is known; the client folder is writable, ASCII-only (the renderer opens its files
   with narrow paths), not under Program Files and short enough (Windows; Linux: the path Wine shows
   the game, wine_path of the folder as given, symlinks kept); the package is outside the client; enough disk (and 8 GB RAM for a local
   build); macOS: WoWSilicon's dlls.txt preload with DXVK is active; Linux: no name we write exists
   in another letter case (Wine would see both), and Wow.exe is not running under Wine (/proc).
2. identify the client's archive chain (client_identity). A client that matches the stock variant
   gets the prebuilt cache: Northlight-cache-stock-<digest12>.zip (or the folder a browser expanded
   it to) from --cache, next to the package, or in Downloads, extracted into world-cache.extract
   with a sha256 check per file, then swapped in. A killed extraction resumes. Any other client,
   or a stock client without the zip, builds the cache locally (scripts/install_world_cache.py,
   20-40 min). A cache that is already current is kept. The dungeon and raid maps are an optional layer on
   top of the continent cache (scripts/install_world_cache.py --instances): the installer asks which of six
   groups to build (--instances SPEC answers for it; the default is what the client has, else none), and a
   prebuilt extract keeps the groups already built. Foreign patches remain part of the cache inputs.
3. build lighting and applicable scenery repairs from local assets into the package work folder.
   The combo uses a free pair of base/locale patch slots when Z belongs to another mod.
4. the renderer: Windows and Linux install d3d9.dll, the DXVK backend and the ini files in one transaction
   (install.py; on Linux the game runs under Wine or Proton, so the payload is the Windows one and the
   summary says how to make Wine load the game-folder d3d9.dll); macOS runs migrate_mac_proxy (mods/d3d9.dll preloaded from
   dlls.txt; wow.exe is never read or written), then the ini files and the art layer in a transaction.
On screen a run is four stages with a heading each (your game, your choices, review, installing), the work as
numbered parts, and closing lines that are the same every time; what each part wrote is in the log.
Before step 2 writes anything the installer says what it is about to do and waits for Enter (plan_install). Run over
an install it is an update: it names the installed and the new version (the receipt <client>/northlight-install.json,
else the installed DLLs' own version lines), keeps the earlier sky and dungeon choices unless asked to change them,
and asks install_world_cache.py --plan whether the cache needs building, so a current game folder is asked for
neither the build's disk space nor its time. The game folders worked on are remembered in one per-user file
(user_state_file) so that the next download, unpacked elsewhere, can offer them; uninstall takes its folder out.
A failed pipeline step shows the last lines of its log and the log's path on the console.
One run per client at a time (<client>/northlight-installer.lock). Rerunning is safe: current parts are skipped. Uninstall restores every transaction of this
client, newest first, and deletes the world cache only if this installer or install_world_cache
made it. <client>/renderer-backups keeps only the files as they were before Northlight first changed
them: each install copies what it replaces (so an interrupted run rolls back), then drops the copies of
older Northlight versions and of restored transactions (tidy_backups). Log: <package>/logs/.
logs (Collect Logs) zips what a problem report needs into <package>/northlight-logs-<time>.zip (collect_logs).
"""
import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import tempfile
import urllib.parse
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath

APP = Path(__file__).resolve().parents[1]   # the repository, or app/ inside a package
for folder in (APP / 'scripts', APP):
    if str(folder) not in sys.path:
        sys.path.insert(0, str(folder))
sys.path.append(str(APP / 'renderer'))   # instance_maps; last, so nothing in renderer/ shadows a standard module
import client_archives  # noqa: E402
import instance_maps  # noqa: E402


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
INSTMP = 'world-cache.instmp'     # and its instance layer's
INSTANCES = 'instances-manifest.json'   # in world-cache/: the groups built
WATER_ARCHIVE_LIST = 'northlight-water-archives.txt'
SKY_CHOICE = 'northlight-sky.json'
SKY_MODES = ('current', 'experimental-preserve')
SKY_LABELS = {'current': "Northlight's sky", 'experimental-preserve': 'more of your HD sky (experimental)'}
RECEIPT = 'northlight-install.json'   # in the game folder, through the transaction: the versions this installer put there
# What a combined package adds to the game folder for Tideglass. --no-tideglass leaves these out.
TIDEGLASS_FILES = ('water-core.dll', 'water.ini', 'water-host.ini')
RECEIPT_FORMAT = 'northlight-install/1'
# Installs made before the receipt existed: each DLL carries its version in its start-up log line.
VERSION_IN_DLL = {'northlight': re.compile(rb'Northlight renderer (\d+(?:\.\d+)+)'),
                  'tideglass': re.compile(rb'Tideglass (\d+(?:\.\d+)+)')}
CHOICE = 'world-cache.instances.json'   # beside world-cache/, ours: the groups chosen (survives a swap of the folder)
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
              'f2e38013f9f03a51548bc785e40351959d1959191d69bbc478bd89056708d182',   # 0.3.199 stock (rain alpha .35)
              'bd7d4179a2f68ec5ad1bb22712aba6fc876a6e1e00268914f0a98d6cdf531b1a',   # 0.3.199 dev HD (narrower rain, core sigma .9 px)
              '0226cee25b2e4551ffee1c931576c5ce7fbe68696f0b2441821f94c9760133bf',   # 0.3.199 stock (narrower rain, core sigma .9 px)
              '8a9eccf7fab342511cd03f1698166633f860e2d7df76a99a75485fad79031d34',   # 0.3.199 dev HD (rain alpha .55, core sigma .75 px)
              '3cc7cc499b5ece0a7256c80536e85969105dd9e98c8101b1f6ec92f15c91a617',   # 0.3.199 stock (rain alpha .55, core sigma .75 px)
              'fd21cd54cc823aed020f478d6ba25ab6a5d841a2f526a6edbca1b27df9dfed56',   # 0.3.199 dev HD (rain alpha .75, core sigma .75 px)
              '8ce7a1b693938b5d6fb45578cd382fee1aa25c3638e3e0a82f69910ee3d98001',   # 0.3.199 stock (rain alpha .75, core sigma .75 px)
              '2403d3272338c4a4ec8b6cd99aac7878c0466f31c36f996307ab93337bccc274',   # 0.3.199 dev HD (rain on texel 16, core sigma .62 px)
              'e83501ac6b2bef2ddffe44bd4e1664091c7a13d8b650a7a13b046678d302c222',   # 0.3.199 stock (rain on texel 16, core sigma .62 px)
              '0d883ef5955dd29c6f843a2622b0fe364a9726b40b4e1f9421d6a84c0acb4426',   # 0.3.199 dev HD (rain mips keep a one-texel full-alpha core)
              '931ab91f93b6c764809ac99f03b2b9b4920348fbdbb24001b069dadba2bbc94b',   # 0.3.199 stock (rain mips keep a one-texel full-alpha core)
              '39d6c9398650060c37ab358ec7822e0aada7ff218ab3c86c3f81146ad44dbe87',   # 0.3.199 dev HD (rain alpha 1)
              '14976725862e3997f1004920bf12a5cc674b654bc9fdfbb53d8282d7bf0f350e',   # 0.3.199 stock (rain alpha 1)
              'a827c5cdb1137f271122f87aa131c5c2e163af1e873f5683ae25399ff4eaa6a8',   # 0.3.199 dev HD (rain colour .78)
              'bf79010b93a34301b5cc96c87b9fc9e0ec98faaa5e7a74bf50be89583a87cd4c',   # 0.3.199 stock (rain colour .78)
              '0966e42aa4e43040f93f9b839d0cf3ee75b7790a31a462ad899552206c93944d',   # 0.3.199 dev HD (rain colour .90, alpha .8)
              '07f59ab51e062453a1a0aec511dd17b62f3613cea2f0958f345c330f762f33e7',   # 0.3.199 stock (rain colour .90, alpha .8)
              'cc2e6520f24e1799ab8a35f925b1a3d27750261436919e310bd847e169e2e299',   # 0.3.199 dev HD (rain colour .80, alpha .65)
              'ffd666f0101ad7e9bc3d9192f1299e70e06ef91bca5ab97dfa54785650314278',   # 0.3.199 stock (rain colour .80, alpha .65)
              '3465087c08efe7023c303adf8fcd30c441d54872d9ce629b47ad417910303433',   # 0.3.199 dev HD (rain colour .80, alpha .5)
              'bba81752a96168e2cb1e859d48867560398dd0a41bff9b69176f68ff925d13b9',   # 0.3.199 stock (rain colour .80, alpha .5)
              '8088eea94a6cdf40330d0f4725cb98c67a8b94f44b3c34832f84e3d9b0f0fa00',   # 0.3.199 dev HD (rain colour .80, alpha .4)
              '559416a1feb1932f8a48cbf89ef52b16f39c5c709c92d2572d7f961f2828b4a4',   # 0.3.199 stock (rain colour .80, alpha .4)
              '50182018a6f22c2dce030644c7b361cba7de448a771e25199b77179c1fb51d47',   # 0.3.199 dev HD (transparent weather mist, withdrawn: snow and sand storms keep their mist)
              'a9748f26c9e515d43943e51c0809f307c55b7a687708486391b7a212c276e7fe',   # 0.3.199 stock (transparent weather mist, withdrawn)
              '7d49498b65b7f0f6ae97c1f481028d31b4af6edfaaa88c659642ed84d4d1b5b2',   # 0.3.199 dev HD (mist puffs at 1:4: skipped in rain only)
              'bfd8abde6bd55201f652d0077af309df2e3d2a2cdb45c48b172cb5bdf39f4ff7'}   # 0.3.199 stock (mist puffs at 1:4)
BASE_ARCHIVES = ('common', 'common-2', 'expansion', 'lichking', 'patch', 'patch-2', 'patch-3')
GIB = 1 << 30
LOCAL_BUILD_BYTES = 13 * GIB   # 1.2 x the ~11 GB cache
MIN_FREE = GIB // 2
PERCENT = re.compile(r': (\d+)% \(')
PART_WIDTH = 50          # the column a part's closing word starts at
PART_DETAIL = ' ' * 8    # what a part reports is indented under its line
# Lines of the transaction engine a player sees; its other lines go to the log.
ENGINE_SHOWN = ('Note', 'Existing', 'Removing', 'Installation interrupted')
LANGUAGES = {'enUS': 'English', 'enGB': 'English', 'deDE': 'German', 'frFR': 'French', 'esES': 'Spanish', 'esMX': 'Spanish',
             'ruRU': 'Russian', 'koKR': 'Korean', 'zhCN': 'Chinese', 'zhTW': 'Chinese'}
WINDOWS_MAX_PATH = 259
LONGEST_CACHE_NAME = 110   # world-cache.staging\models\<64 hex>.<pid>.tmp (world-cache.instmp\<map>\build-<map>-<time>-<pid>.json is shorter)
INSTANCE_DISK = 1.2   # of a group's measured size: the staging folder next to what it replaces


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


def user_state_file():
    """The one file outside the game and package folders: the game folders this installer worked on, so the next
    download can offer them (each download unpacks into a new folder). Uninstall takes its folder out again."""
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA')
        return Path(base) / 'Northlight' / 'installer.json' if base else None
    if sys.platform == 'darwin':
        return Path.home() / 'Library/Application Support/Northlight/installer.json'
    return Path(os.environ.get('XDG_STATE_HOME') or Path.home() / '.local/state') / 'northlight/installer.json'


def version_key(version):
    return tuple(int(n) for n in re.findall(r'\d+', version or ''))


def describe(state):
    """'Northlight 0.3.217 + Tideglass 0.1.0' for {'northlight': .., 'tideglass': ..}; tideglass None = not there,
    '' = there without a readable version."""
    text = f"Northlight {state['northlight']}" if state.get('northlight') else 'an earlier Northlight'
    if state.get('tideglass') is not None:
        text += f" + Tideglass {state['tideglass']}" if state['tideglass'] else ' + an earlier Tideglass'
    return text


def has_game(folder):
    try:
        return any(p.name.lower() == 'wow.exe' for p in Path(folder).iterdir())
    except OSError:
        return False


def minutes(low, high=None):
    """'about 2 minutes' / '20-40 minutes' from estimates in minutes."""
    if high is not None:
        return f'{low:g}-{high:g} minutes'
    whole = max(1, round(low))
    return 'about a minute' if whole == 1 else f'about {whole} minutes'


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


PROXY_PLATFORMS = ('windows', 'linux')   # the game-folder d3d9.dll proxy with the package's DXVK backends
# Linux: what the player sets once so that Wine loads the game-folder d3d9.dll (shown after every install).
WINE_STEPS = [
    'Before starting WoW, set your launcher to load the installed d3d9.dll:',
    '  Steam/Proton: game Properties > Launch Options:',
    '    WINEDLLOVERRIDES="d3d9=n,b" %command%',
    '  Lutris: Configure > Runner options > DLL overrides: add d3d9 with value n,b.',
    "  Bottles: in the bottle's DLL Overrides, set d3d9 to Native, then Builtin.",
    '  Plain Wine: from the game folder run WINEDLLOVERRIDES="d3d9=n,b" wine Wow.exe',
    '    or set d3d9 to native, builtin in winecfg for Wow.exe.',
    'Keep your existing launcher DXVK setting. Northlight supplies its own copy.',
]


def host_platform():
    if os.name == 'nt':
        return 'windows'
    return 'mac' if sys.platform == 'darwin' else 'linux' if sys.platform.startswith('linux') else None


def windows_running():
    out = subprocess.run(['tasklist.exe', '/FI', 'IMAGENAME eq wow.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                         text=True, errors='replace').stdout
    return ['wow.exe'] if 'wow.exe' in out.lower() else []


# Wine and Proton start a game as a process whose command line names the Windows path of Wow.exe
# (C:\...\Wow.exe or Z:\...\Wow.exe, maybe quoted) and whose name (comm) is Wow.exe; a native path ends in /Wow.exe.
WINE_WOW = r'(^|[\\/"\'])wow\.exe(["\' ]|$)'


def ancestors(pid=None):
    """This process and its parents (/proc/<pid>/stat): a launcher started as `bash install.sh --client .../Wow.exe`
    must not count as the game."""
    found, pid = set(), pid or os.getpid()
    while pid > 1 and pid not in found:
        found.add(pid)
        try:
            pid = int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return found


def linux_running(proc=Path('/proc')):
    """Wow.exe running under Wine or Proton: a process other than this installer and its parents whose name (comm,
    set by Wine) is Wow.exe or whose command line matches WINE_WOW (case-insensitive), in one pass over /proc."""
    mine, pattern = ancestors(), re.compile(WINE_WOW, re.I)
    for entry in proc.glob('[0-9]*'):
        try:
            if int(entry.name) in mine:
                continue
            if (entry / 'comm').read_text(errors='replace').strip().lower() == 'wow.exe' or \
                    pattern.search((entry / 'cmdline').read_bytes().replace(b'\0', b' ').decode('utf-8', 'replace')):
                return ['Wow.exe (Wine)']
        except (OSError, ValueError):
            continue
    return []


def wine_path(client):
    """The game folder as Wine shows it to the game: C:\\... inside a prefix's drive_c, else Z:\\<unix path>."""
    parts = Path(client).parts
    if 'drive_c' in parts:
        return 'C:\\' + '\\'.join(parts[len(parts) - parts[::-1].index('drive_c'):])
    return 'Z:' + str(client).replace('/', '\\')


def on_disk(client, name):
    """name (a client path) as its folder already lists it in another letter case (Data/Patch-Z.MPQ), else name."""
    path = Path(client) / name
    try:
        names = [p.name for p in path.parent.iterdir()]
    except OSError:
        return name
    same = [n for n in names if n.lower() == path.name.lower()]
    return name if path.name in names or not same else (path.parent / same[0]).relative_to(client).as_posix()


def case_conflicts(client, names):
    """Linux: client paths that differ only in case from a relative path in names (an existing D3D9.dll next to the
    d3d9.dll we would write, a Renderer-Backends folder, ...). Wine matches names case-insensitively, so it could
    load either one. Each path is followed as far as it exists with its exact case."""
    found = set()
    for name in names:
        folder = Path(client)
        for part in PurePosixPath(name).parts:
            try:
                entries = [p.name for p in folder.iterdir()]
            except OSError:
                break
            found |= {(folder / e).relative_to(client).as_posix() for e in entries if e != part and e.lower() == part.lower()}
            if part not in entries:
                break
            folder = folder / part
    return sorted(found)


class Package:
    """The unpacked player package: app/ (this code), runtime/, payload/, variants/, BUILD-INFO.json."""

    def __init__(self, root, platform):
        self.root, self.platform = Path(root), platform
        self.info = read_json(self.root / 'BUILD-INFO.json') or {}
        self.version = self.info.get('version', 'dev')
        self.payload = self.root / 'payload'
        self.dll = self.payload / 'd3d9.dll'
        self.stormlib = self.root / 'runtime' / {'windows': 'StormLib.dll', 'linux': 'lib/libstorm.so'}.get(platform, 'lib/libstorm.dylib')
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

    def __init__(self, package, platform=None, yes=False, out=None, tideglass=None):
        self.pkg = package
        self.platform = platform or package.platform
        # tideglass: False leaves the package's Tideglass out (--no-tideglass), True installs it, None does as the
        # last install in the game folder did (with it, on a first install).
        self.asked_tideglass = tideglass
        self.use_tideglass(tideglass is not False)
        self.yes = yes
        self.out = out or sys.stdout
        self.log = None
        self.given = None   # the game folder as given, symlinks kept (Wine shows the game that path)
        self.running = (lambda: migrate().running()) if self.platform == 'mac' else \
            linux_running if self.platform == 'linux' else windows_running
        self.proxy = self.platform in PROXY_PLATFORMS   # d3d9.dll and DXVK installed by install.py
        self.free_bytes = lambda path: shutil.disk_usage(path).free
        self.ram = None   # installed-memory probe; None = install_world_cache.total_memory
        self.search_dirs = [package.root.parent, package.root, Path.home() / 'Downloads']
        self.memory = None   # user_state_file() in a real run; None remembers nothing
        self.part = None   # the part of the work in progress (working)
        self.parts_done = self.parts_total = 0
        INSTALL.emit = self.engine_line

    def use_tideglass(self, wanted):
        """Set what this run installs of the package's Tideglass. Left out, the combined package still installs all of
        its own Northlight (its skies, raised fog and scenery repairs): only the water goes, which is the core, its
        settings and the open-air light it gives the underwater view."""
        package = self.pkg
        # combined: the package is the combined one, whatever this run installs of its water.
        self.combined = bool(package.info.get('tideglass'))
        self.tideglass = tideglass = (package.info.get('tideglass') or {}) if wanted else {}
        self.left_out = TIDEGLASS_FILES if self.combined and not wanted else ()
        self.product_name = 'Northlight + Tideglass' if tideglass else 'Northlight renderer'
        # Combined packages before Tideglass 0.1.0 carried one beta label instead of both parts' versions.
        self.title = (f"Northlight {package.info.get('dll_version')} + Tideglass {tideglass['version']}"
                      if tideglass.get('version') else
                      f"Northlight {package.info.get('dll_version')}" if self.left_out else
                      f'{self.product_name} {package.version}')
        self.offers = {'northlight': package.info.get('dll_version') or self.pkg.version,
                       'tideglass': tideglass.get('version') or ('' if tideglass else None)}

    def left_out_before(self, client):
        """Whether the last install in this game folder left Tideglass out of a package that had it."""
        receipt = read_json(Path(client) / RECEIPT)
        return (isinstance(receipt, dict) and receipt.get('format') == RECEIPT_FORMAT and
                bool(receipt.get('tideglass_left_out')))

    # ---- output ----

    def say(self, *parts, console=True):
        line = ' '.join(str(p) for p in parts)
        if self.part and line:   # under the part it belongs to
            line = '\n'.join(PART_DETAIL + each.strip() for each in line.split('\n'))
        if console:
            if self.part and not self.part['broken']:   # the part's own line is still open
                print(file=self.out)
                self.part['broken'] = True
            print(line, file=self.out, flush=True)
        if self.log:
            self.log.write(line + '\n')
            self.log.flush()

    def engine_line(self, *parts, **_):
        """A line from the transaction engine (install.py): the log has them all, the screen only what a player
        needs to act on or would otherwise miss."""
        line = ' '.join(str(p) for p in parts)
        self.say(line, console=self.log is None or line.startswith(ENGINE_SHOWN))

    def stopped(self, kind, why):
        """The end of a run that did not finish: why, set apart from what came before, and where the details are."""
        self.say('')
        self.say(kind, why)
        if self.log:
            self.say(f'  Full details: {self.log.name}')

    def banner(self, text):
        self.say(text)
        self.say('=' * len(text))

    def step(self, number, total, name):
        """The heading of one stage of a run."""
        self.say('')
        self.say(f'STEP {number} of {total}: {name}')

    def expect_parts(self, total):
        self.parts_done, self.parts_total = 0, total

    @contextlib.contextmanager
    def working(self, label):
        """One numbered part of the work: its line stays open until the part ends, so a quiet part reads
        '[1/4] Fog ... done' and a part that reports its progress has those lines beneath it. The value yielded
        takes another closing word in ['result']."""
        self.parts_done += 1
        head = f'  [{self.parts_done}/{max(self.parts_total, self.parts_done)}] {label} '
        head += '.' * max(3, PART_WIDTH - len(head))
        print(head, end='', file=self.out, flush=True)
        if self.log:
            self.log.write(head + '\n')
            self.log.flush()
        part = self.part = {'broken': False, 'result': 'done'}
        started = time.monotonic()
        try:
            yield part
        except BaseException:
            if not part['broken']:
                print(file=self.out, flush=True)
            self.part = None
            raise
        self.part = None
        took = (time.monotonic() - started) / 60
        result = part['result'] + (f' in {round(took)} minutes' if took >= 1.5 else '')
        print((PART_DETAIL if part['broken'] else ' ') + result, file=self.out, flush=True)
        if self.log:
            self.log.write(PART_DETAIL + result + '\n')
            self.log.flush()

    def open_log(self, action):
        try:
            self.pkg.logs.mkdir(parents=True, exist_ok=True)
            self.log = (self.pkg.logs / f'northlight-{action}-{datetime.now():%Y%m%d-%H%M%S}.log').open('a', encoding='utf-8')
        except OSError:
            self.log = None

    def interactive(self):
        return bool(sys.stdin) and sys.stdin.isatty()

    def ask(self, question, default):
        """A yes/no question; Enter takes the default. --yes accepts only questions whose default is yes."""
        if default and self.yes:
            return True
        if not self.interactive():
            return default
        answer = input(f'{question} Press Enter for {"yes" if default else "no"}, or type '
                       f'{"n" if default else "y"} > ').strip().lower()
        return default if not answer else answer in ('y', 'yes')

    def confirm(self, verb, can_change=False):
        """The one question before anything is written: 'go', or 'change' (offered when this run kept earlier
        choices without asking). --yes and no terminal go ahead; n, no, q or cancel stops with nothing changed."""
        if self.yes or not self.interactive():
            return 'go'
        question = f'  Press Enter to {verb}' + (', type C to change the sky and dungeon choices first,' if can_change else ',') + \
            ' or close this window to cancel > '
        while True:
            answer = input(question).strip().lower()
            if not answer:
                return 'go'
            if can_change and answer == 'c':
                return 'change'
            if answer in ('n', 'no', 'q', 'quit', 'cancel'):
                raise Refusal('Cancelled; nothing was changed.')
            self.say(f'  Press Enter alone to {verb}.')

    def pick(self, question, parse, default):
        """A typed answer: parse(text) returns the value or raises ValueError. --yes and no terminal take the
        default, so does an empty answer; an invalid one is asked once more, then the default is used."""
        if self.yes or not self.interactive():
            return default
        for attempt in range(2):
            answer = input(question).strip()
            if not answer:
                return default
            try:
                return parse(answer)
            except ValueError as e:
                self.say(f'  {e}' + (' Try again.' if attempt == 0 else ' Keeping the current choice.'))
        return default

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
            self.say(f'--- last lines of {Path(script).name} (exit {code}) ---')
            for line in tail:
                print('  | ' + line, file=self.out, flush=True)
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
        client = self.prompt_client() if raw is None else self.parse_client(raw)
        self.given = Path(os.path.abspath(client))
        return client.resolve()

    def parse_client(self, raw):
        """The game folder a typed, pasted or dragged answer names (the folder itself or its Wow.exe), else Refusal."""
        text = str(raw).strip().strip('"').strip("'")
        if self.platform == 'linux' and text.startswith('file://'):   # a folder dropped as a URI
            text = urllib.parse.unquote(text[len('file://'):])
        if self.platform in ('mac', 'linux'):
            text = text.replace('\\ ', ' ')   # a folder dragged into Terminal
        client = Path(text).expanduser()
        if client.name.lower() == 'wow.exe' and client.is_file():
            client = client.parent
        if not text or not client.is_dir():
            raise Refusal(f'Game folder not found: {text}. Choose the folder containing Wow.exe.')
        return client

    # ---- what earlier runs left ----

    def remembered(self):
        """Game folders earlier runs worked on that still hold Wow.exe, the latest first."""
        folders = (read_json(self.memory) or {}).get('game_folders') if self.memory else None
        return [f for f in folders if isinstance(f, str) and has_game(f)] if isinstance(folders, list) else []

    def remember(self, client, keep=True):
        """Put this game folder first in the remembered list (keep=False: take it out). Never fails a run."""
        if not self.memory:
            return
        try:
            folders = [f for f in self.remembered() if Path(f) != Path(client)]
            if keep:
                folders.insert(0, str(client))
            if folders:
                self.memory.parent.mkdir(parents=True, exist_ok=True)
                INSTALL.atomic_json(self.memory, {'game_folders': folders[:8]})
            else:
                self.memory.unlink(missing_ok=True)
                self.memory.parent.rmdir()   # only when empty
        except OSError:
            pass

    def installed(self, client):
        """The versions an earlier run left in this game folder ({'northlight': .., 'tideglass': ..}, see describe),
        or None when nothing of ours is installed. From the receipt, else from the installed DLLs themselves."""
        client = Path(client)
        try:
            if not INSTALL.transactions(client) and not (self.platform == 'mac' and self.mac_backups(client).is_dir()):
                return None
        except (OSError, ValueError):
            return None
        receipt = read_json(client / RECEIPT)
        if isinstance(receipt, dict) and receipt.get('format') == RECEIPT_FORMAT:
            return {'northlight': receipt.get('northlight'), 'tideglass': receipt.get('tideglass')}

        def stamped(name, part):
            try:
                found = VERSION_IN_DLL[part].search((client / on_disk(client, name)).read_bytes())
            except OSError:
                return None
            return found.group(1).decode('ascii') if found else ''
        return {'northlight': stamped('mods/d3d9.dll' if self.platform == 'mac' else INSTALL.PROXY, 'northlight') or None,
                'tideglass': stamped('water-core.dll', 'tideglass')}

    def relation(self, previous):
        """This package against what is installed: 'new' (nothing installed), 'same', 'older' or 'update'."""
        if previous is None:
            return 'new'
        def key(state):
            return version_key(state.get('northlight')), version_key(state.get('tideglass'))
        if not previous.get('northlight') or (previous.get('tideglass') == '' and self.offers['tideglass']):
            return 'update'   # an install too old to carry its versions
        if (previous.get('tideglass') is None) != (self.offers['tideglass'] is None):
            return 'update'   # Tideglass is being added, taken out, or left as it is by a Northlight-only package
        return 'same' if key(self.offers) == key(previous) else 'older' if key(self.offers) < key(previous) else 'update'

    def prompt_client(self):
        """Ask for the game folder until an answer names one. Folders of earlier runs (and, on macOS, WoWSilicon's)
        are offered by number, the latest as the answer to Enter. Without a terminal one wrong answer ends the run."""
        known = self.remembered()
        if self.platform == 'mac':
            try:
                versions = json.loads(migrate().VERSIONS.read_text(encoding='utf-8')).get('versions', {})
                known += sorted({str(Path(v['game_path']).parent) for v in versions.values() if v.get('game_path')} - set(known))
            except (OSError, ValueError, AttributeError):
                pass
        for i, path in enumerate(known, 1):
            there = self.installed(path)
            self.say(f'  {i}) {path}' + (f'   ({describe(there)} installed)' if there else ''))
        question = ('  Press Enter for 1, type a number, or paste another game folder (or drag Wow.exe here) > ' if known else
                    '  Paste the game folder path or drag Wow.exe here, then press Enter > ')
        while True:
            try:
                raw = input(question).strip()
            except EOFError:
                raise Refusal('No game folder was given; nothing was changed.')
            if raw.isdigit() and 0 < int(raw) <= len(known):
                return Path(known[int(raw) - 1])
            if not raw:
                if known:
                    return Path(known[0])
                raise Refusal('No game folder was given; nothing was changed.')
            try:
                client = self.parse_client(raw)
                if has_game(client):
                    return client
                problem = f'Wow.exe is not in {client}. Choose the folder that holds Wow.exe.'
            except Refusal as e:
                problem = str(e)
            if not self.interactive():
                raise Refusal(problem)
            self.say('  ' + problem)

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
        if self.platform == 'linux':
            shown = wine_path(self.given or client)
            if len(shown) + LONGEST_CACHE_NAME > WINDOWS_MAX_PATH:
                raise Refusal(f'The game folder path is too long for the game under Wine ({shown}: {len(shown)} '
                              f'characters; at most {WINDOWS_MAX_PATH - LONGEST_CACHE_NAME}). Move the game folder to a '
                              'shorter path, for example ~/Games/WoW.')
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
        if self.platform == 'linux':
            manifest = read_json(self.pkg.root / 'payload-manifest.json') or []
            conflicts = case_conflicts(client, [e['path'] for e in manifest] + [
                INSTALL.CONFIG, INSTALL.LEGACY, CACHE, STAGING, EXTRACT, PREVIOUS, 'renderer-backups', LOCK, RECEIPT])
            if conflicts:
                raise Refusal('The game folder has files whose names differ only in upper/lower case from the ones '
                              'Northlight installs, and Wine could load either one. Rename or remove these first: ' +
                              ', '.join(conflicts))
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
            elif self.proxy and other:
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
            raise Refusal(f'{given} is not the prebuilt {variant} outdoor lighting for this package '
                          f'(expected {prefix}.zip).')
        return None

    def chosen_instances(self, client):
        """The instance groups the client is meant to have, in group order: the recorded choice (written before every
        instance build, so a failed or killed run keeps it), else the cache's instances-manifest.json."""
        choice = read_json(client / CHOICE)
        if choice is not None:
            groups = choice.get('groups')
            return [k for k in instance_maps.KEYS if isinstance(groups, list) and k in groups]
        return self.built_instances(client)

    def built_instances(self, client):
        """The groups the cache's instances-manifest.json records as built."""
        groups = (read_json(client / CACHE / INSTANCES) or {}).get('groups')
        return [k for k in instance_maps.KEYS if isinstance(groups, dict) and k in groups]

    def remember_instances(self, client, keys):
        """Record the choice beside world-cache/; the next run's default, and uninstall removes it."""
        if keys or (client / CHOICE).exists():
            INSTALL.atomic_json(client / CHOICE, {'groups': keys})

    def instances_to_build(self, client, locale, without, keys):
        """The selected groups that will be built: those the orchestrator's dry run names, else all selected."""
        if not keys:
            return []
        try:
            code, last = self.run(APP / 'scripts/install_world_cache.py',
                                  ['--client', client, '--locale', locale, '--output', client / CACHE, '--without', without,
                                   '--instances', ','.join(keys), '--instances-plan'], show=False)
            if code == 0 and isinstance(last, dict) and isinstance(last.get('build'), list):
                return [k for k in keys if k in last['build']]
        except (OSError, ValueError):
            pass
        return keys

    def cache_plan(self, client, locale, without):
        """What a local build would do now, without doing it: {'mode': 'full' | 'partial' | 'finish' | 'up_to_date',
        'resumes': bool}, or None when the dry run itself fails (the build then says why)."""
        try:
            code, last = self.run(APP / 'scripts/install_world_cache.py',
                                  ['--client', client, '--locale', locale, '--output', client / CACHE, '--without', without,
                                   '--plan'], show=False)
        except (OSError, ValueError):
            return None
        return last if code == 0 and isinstance(last, dict) and last.get('event') == 'cache_plan' else None

    def choose_instances(self, client, spec, ask=True):
        """The instance group keys to have: --instances SPEC (Refusal when invalid), else asked (ask=True, on a
        terminal), else the current ones."""
        try:
            if spec is not None:
                return instance_maps.resolve(spec)
        except ValueError as e:
            raise Refusal(f'{e}. --instances takes group numbers 1-6, keys ({", ".join(instance_maps.KEYS)}), all or none.')
        current = self.chosen_instances(client)
        if not ask or self.yes or not self.interactive():
            return current
        self.say('  Dungeon and raid lighting (optional; you can add it later)')
        for n, g in enumerate(instance_maps.GROUPS, 1):
            self.say(f'    {n}  {g["label"]:<17} ~{g["est_gb"]:g} GB  ~{max(1, round(g["est_minutes"]))} min')
        names = ', '.join(g['label'] for g in instance_maps.GROUPS if g['key'] in current) or 'none'
        return self.pick(f'  Press Enter to keep {names}, or type numbers (e.g. 1 2 6), all or none > ',
                         instance_maps.resolve, current)

    def instance_labels(self, keys):
        return [g['label'] for g in instance_maps.GROUPS if g['key'] in keys]

    def recover(self, client):
        """A run killed between the two renames of a swap leaves only world-cache.previous: put it back."""
        cache, previous = client / CACHE, client / PREVIOUS
        if previous.exists() and not cache.exists():
            retry(previous.rename, cache)
            self.say('Recovered outdoor lighting after an interrupted run.')

    def swap(self, client):
        cache, staging, previous = client / CACHE, client / EXTRACT, client / PREVIOUS
        remove_tree(previous)
        if cache.exists():
            self.carry_instances(cache, staging)
            retry(cache.rename, previous)
        retry(staging.rename, cache)
        remove_tree(previous)

    def carry_instances(self, old, new):
        """The instance layer of the cache being replaced moves into its replacement. Never fails the install:
        what did not move is built again by the instance step."""
        import install_world_cache as iwc
        try:
            moved = iwc.carry_instances(old, new)
            if moved:
                self.say(f'Kept dungeon and raid lighting ({len(moved)} maps).')
        except (OSError, ValueError) as e:
            self.say(f'Dungeon and raid lighting could not be kept ({e}); it is built again.')
            iwc.drop_instance_record(old)
            iwc.drop_instance_record(new)

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
            self.say(f'Unpacking outdoor lighting: {len(files)} files, {total / GIB:.1f} GiB ...')
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
                    self.say(f'  outdoor lighting {100 * written // max(total, 1)}%')
                    next_report += step
            for part in staging.rglob('*.part'):
                part.unlink()
            # Written last: a staging folder with northlight-cache.json is complete.
            INSTALL.atomic_json(staging / VARIANT_FILE, manifest)
            (staging / EXTRACTING).unlink()
        self.still_closed('putting outdoor lighting in place (it waits complete in world-cache.extract)')
        self.swap(client)
        self.say('Outdoor lighting unpacked.')

    def build_cache(self, client, locale, jobs, without=LETTER):
        """Build the cache from this client's own archives (install_world_cache.py; no-op when current)."""
        self.still_closed('building outdoor lighting')
        args = ['--client', client, '--locale', locale, '--output', client / CACHE, '--without', without,
                '--progress', 'human'] + (['--jobs', jobs] if jobs else [])
        code, last = self.run(APP / 'scripts/install_world_cache.py', args)
        if code != 0:
            last = last or {}
            why = last.get('error') or '; '.join(last.get('problems') or []) or 'see the log'
            logs = ''.join(f'\n  {log}' for log in last.get('logs') or [])
            raise Failed(f'Building outdoor lighting failed (exit {code}): {why}.' +
                         (f' The last lines of these logs are shown above:{logs}' if logs else '') +
                         '\nRun the installer again to resume.')
        return (last or {}).get('event', 'done')

    def build_instances(self, client, locale, jobs, without, keys):
        """The instance layer (install_world_cache.py --instances): builds the selected groups, removes the others.
        Returns {'built': [...], 'kept': [...], 'removed': [...]} (group keys)."""
        args = ['--client', client, '--locale', locale, '--output', client / CACHE, '--without', without,
                '--progress', 'human', '--instances', ','.join(keys) or 'none'] + (['--jobs', jobs] if jobs else [])
        code, last = self.run(APP / 'scripts/install_world_cache.py', args)
        last = last or {}
        if code != 0:
            why = last.get('error') or '; '.join(last.get('problems') or []) or 'see the log'
            logs = ''.join(f'\n  {log}' for log in last.get('logs') or [])
            error = Failed(f'Building dungeon and raid lighting failed (exit {code}): {why}.' +
                           (f' The last lines of these logs are shown above:{logs}' if logs else '') +
                           '\nRun the installer again to resume.')
            error.logs = last.get('logs') or []
            raise error
        return {k: last.get(k) or [] for k in ('built', 'kept', 'removed')}

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
                found |= {a for e in record.get('files', []) if e['path'].lower() == name.lower() for a in INSTALL.afters(e) if a}
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

    def art_letter(self, client, locale, foreign):
        if not self.combined:
            return None if foreign else LETTER
        data = client_archives.data_dir(client)
        local = client_archives.locale_dir(client, locale)
        top, localized = client_archives._entries(data), client_archives._entries(local)
        owned = {}
        for _, record in INSTALL.transactions(client):
            for entry in record['files']:
                owned.setdefault(entry['path'].lower(), set()).update(INSTALL.afters(entry))
        available = None
        for letter in reversed('abcdefghijklmnopqrstuvwxyz'):
            pair = (top.get(f'patch-{letter}.mpq'), localized.get(f'patch-{locale.lower()}-{letter}.mpq'))
            existing = [p for p in pair if p is not None]
            # Prefer an installed pair even if a higher slot has since become free.
            if existing and all(p.is_file() and (key := p.relative_to(client).as_posix().lower()) in owned
                                and sha(p) in owned[key] for p in existing):
                return letter
            if available is None and (not existing or (letter == LETTER and not foreign and all(p.is_file() for p in existing))):
                available = letter
        return available

    def choose_sky(self, client, mode, ask=True):
        if mode is not None:
            if mode not in SKY_MODES:
                raise Refusal('Unknown sky choice: ' + str(mode))
            return mode
        saved = read_json(INSTALL.safe_path(client, SKY_CHOICE))
        current = saved.get('mode') if isinstance(saved, dict) else None
        if current not in SKY_MODES:
            current = 'current'
        if not ask or self.yes or not self.interactive():
            return current
        self.say('')
        self.say('  Sky')
        self.say("    1  Northlight's sky (recommended)")
        self.say('    2  Keep more of your HD sky (experimental: untested with most HD skies, and you may see two moons)')
        def parse(answer):
            choices = {'1': 'current', '2': 'experimental-preserve'}
            if answer not in choices:
                raise ValueError('Choose 1 or 2.')
            return choices[answer]
        return self.pick(f'  Press Enter for {SKY_MODES.index(current) + 1}, or type 1 or 2 > ', parse, current)

    def art_layer(self, client, locale, sky='current', letter=LETTER):
        """({client path: bytes} to install, None, {client path: action}) or ({}, reason it is skipped, {}).
        The action per target is installed (absent before), replaced (another layer of ours) or kept-identical.
        A target that exists in another letter case (Data/Patch-Z.MPQ on Linux) keeps its name on disk, so that
        Wine never sees two files."""
        combo = self.combined
        if not combo and not (client / CACHE / 'fog').is_dir():
            return {}, 'there is no outdoor lighting, and it needs the fog zones from it', {}
        work = self.pkg.work / 'art-layer'
        remove_tree(work)
        work.mkdir(parents=True)
        code, _ = self.run(APP / 'build_art_layer.py', ['--client', client, '--locale', locale, '--world-cache',
                                                         client / CACHE, '--output', work, '--letter', letter, '--sky', sky] +
                           (['--tideglass'] if combo else []) + (['--game-underwater'] if self.left_out else []), show=False)
        report = read_json(work / 'art-layer-manifest.json')
        if code != 0 or not report:
            raise Failed(f'Building skies, weather and scenery failed (exit {code}); its last lines are shown above. '
                         f'Full output: {self.log.name if self.log else "not logged"}.')
        layer = {on_disk(client, t): (work / t).read_bytes() for t in report['targets']}
        remove_tree(work)
        for name in report.get('locale_overrides', []):
            self.say('Keeping artwork from a higher-priority locale patch: ' + name)
        if not layer:
            return {}, report['skip_reason'], {}
        if 'sky' in report:
            self.say('Experimental sky preservation: ' + report['sky']['tirisfal'])
            for item in report['sky']['preserved']:
                self.say('  Preserved mixed moon artwork: ' + item['model'] + ' (a second moon may be visible)')
        steps = report.get('steps', [])
        if any(step.get('step') == 'mulgore' and step.get('kept_sky_id') for step in steps):
            self.say('Kept your Mulgore sky.')
        skipped = sum(1 for step in steps if 'skipped' in step)
        self.say(f'Skies, weather and scenery: built from your game files ({len(steps) - skipped} steps applied, '
                 f'{skipped} skipped; details in the log)', console=not combo or self.log is None)
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

    def tidy_backups(self, client):
        """Drop the copies of Northlight's own older versions and of restored transactions; only the original files
        stay (package records, and on macOS the migration records). Returns (bytes freed, reason it stopped or None)."""
        freed, _, why = INSTALL.consolidate(client)
        if why is None and self.platform == 'mac':
            more, _, why = migrate().consolidate(client, self.mac_backups(client))
            freed += more
        return freed, why

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

    def install(self, client, locale=None, cache=None, backend=None, art=None, world_cache=True, jobs=None,
                instances=None, sky=None, raised_air=None):
        self.step(1, 4, 'YOUR GAME')
        client = self.resolve_client(client)
        if self.asked_tideglass is None and self.pkg.info.get('tideglass') and self.left_out_before(client):
            self.use_tideglass(False)
        if art is None:
            art = True
        if raised_air is None:
            raised_air = self.combined
        plan = self.plan_install(client, locale, cache, backend, art, world_cache, instances, sky, raised_air)
        with locked(client):   # S5: taken once the read-only checks have passed
            return self.apply_install(plan, jobs)

    def check_water_client(self, client):
        # Match the image fields that the core checks before its build-specific reads.
        exe = client / on_disk(client, 'wow.exe')
        with exe.open('rb') as stream:
            dos = stream.read(64)
            if len(dos) == 64 and dos[:2] == b'MZ':
                offset = int.from_bytes(dos[60:64], 'little')
                if 64 <= offset <= 1024 * 1024:
                    stream.seek(offset)
                    pe = stream.read(56)
                    if (len(pe) == 56 and pe[:6] == b'PE\0\0\x4c\x01' and
                            struct.unpack_from('<I', pe, 8)[0] == 0x4C2452FE and
                            struct.unpack_from('<H', pe, 20)[0] >= 32 and pe[24:26] == b'\x0b\x01' and
                            struct.unpack_from('<I', pe, 52)[0] == 0x00400000):
                        return
        raise Refusal('The Tideglass beta requires a 32-bit build 12340 client. Its executable was left untouched.')

    def check_water_files(self, client, raised_air=False):
        # A copied core or settings file may belong to another renderer. Only a recorded
        # install may upgrade these names. Edited settings are kept by the transaction.
        names = ('water-core.dll', 'water.ini', 'water-host.ini', WATER_ARCHIVE_LIST) if self.tideglass else ()
        if raised_air:
            names += ('raised-fog/Northrend.rfs', 'raised-fog/Expansion01.rfs')
        conflicts = case_conflicts(client, names)
        if conflicts:
            raise Refusal('Conflicting Tideglass filename: ' + ', '.join(conflicts))
        owned = {entry['path']: entry for _, record in INSTALL.transactions(client) for entry in record['files']}
        for name in names:
            path = INSTALL.safe_path(client, name)
            if path.exists() and (name not in owned or
                                  (name not in ('water.ini', 'water-host.ini') and
                                   INSTALL.sha(path) not in INSTALL.afters(owned[name]))):
                raise Refusal(f'{name} already exists outside this install or was changed. '
                              'Move it aside with the game closed before installing the combined renderer.')

    def plan_install(self, client, locale, cache, backend, art, world_cache, instances=None, sky=None, raised_air=False):
        """Every check, read-only, then the choices and the one confirmation: returns what apply_install does. Raises
        Refusal. A first install asks the dungeon and sky questions; a rerun keeps the earlier answers and offers to
        change them at the confirmation."""
        self.say(f'Game folder: {client}', console=False)
        backend = INSTALL.resolve_backend(client, backend)   # called once; payload_plan takes the result
        locale = self.preflight(client, locale)
        self.say(f'  Found World of Warcraft 3.3.5a ({LANGUAGES.get(locale, locale)}) in {client}')
        self.check_payload(backend)
        if self.tideglass:
            self.check_water_client(client)
        if self.combined:
            self.check_water_files(client, raised_air)
        elif raised_air:
            raise Refusal('--raised-air requires the combined Northlight and Tideglass package')
        marker = INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER)   # a linked renderer-backends folder is refused here
        if self.proxy and marker.is_file():
            self.say('  Note: Northlight no longer switches to DXVK 2.7.1 by itself; the leftover '
                     'renderer-backends\\dxvk\\northlight-dxvk3-init.pending will be removed by this install.')
        # The screen gives this as a tip in the closing lines, where a player who meets the problem looks.
        if self.platform == 'windows' and backend == 'dxvk':
            self.say('Graphics backend: DXVK 3.1.1. On an AMD RX 5000/6000 card, or if the game closes at startup, run '
                     'Install.cmd --backend dxvk2 to use DXVK 2.7.1 instead; Northlight does not switch by itself.', console=False)
        if self.platform == 'linux' and backend == 'dxvk':
            self.say('Graphics backend: DXVK 3.1.1 from the package, whatever DXVK your Wine prefix or launcher has. It '
                     'needs a Vulkan 1.3 driver with 32-bit support. If the game closes at startup, run '
                     'bash install.sh --backend dxvk2 to use DXVK 2.7.1 instead; Northlight does not switch by itself.',
                     console=False)
        if self.proxy and backend == 'legacy':
            proxy = client / INSTALL.PROXY
            if not ((proxy.is_file() and not INSTALL.is_ours(client)) or (client / INSTALL.LEGACY).is_file()):
                raise Refusal('--backend legacy keeps the d3d9.dll found in the game folder as the backend, '
                              'but there is none. Use --backend dxvk, dxvk2 or native.')
        if self.platform == 'mac':
            try:
                migrate().plan(client, self.pkg.dll, roots=[self.mac_backups(client)])
            except ValueError as e:
                raise Refusal(str(e))
        previous = self.installed(client)
        relation = self.relation(previous)
        old = self.legacy(client)
        foreign = self.foreign_z(client, locale)
        # An old full package's letters (patch-y) go away before the cache step: identify without them.
        letter = self.art_letter(client, locale, foreign)
        without = (letter or '') + ''.join(x for x in self.legacy_letters(old) if x != letter)
        for rel, digest in foreign:
            self.say(f'  {rel} belongs to another mod; it is kept, and its content is part of this client.')
            self.say(f'{rel} sha256 {digest}', console=False)
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
            self.say(f'  Game files: {variant} 3.3.5a ({locale}).', console=bool(self.pkg.variants))
        elif not self.pkg.variants:
            self.say(f'Using your own game files ({locale}).', console=False)
        else:
            self.say(f'  Game files: modified 3.3.5a ({locale}); no prebuilt outdoor lighting matches:')
            for line in differences[:12]:
                self.say('    ' + str(line))
        if action == 'build' and manifest:
            self.say(f'  The prebuilt download Northlight-cache-{variant}-{manifest["cache_digest"][:12]}.zip was not found next to '
                     'the package or in Downloads; outdoor lighting is built from your game files instead.')
        if action == 'extract' and owner == 'local':
            self.say('  The prebuilt outdoor lighting replaces what this game folder built earlier (once).')
        if action in ('extract', 'build') and owner == 'foreign' and old == []:
            if not self.ask(f'  {client / CACHE} was not made by this installer. Replace it?', False):
                raise Refusal('The existing world-cache was kept; nothing changed. Move it away or answer yes.')
        # What a local build would do, so that an update of a current game folder asks for neither 13 GB nor 40 minutes.
        build = self.cache_plan(client, locale, without) if action == 'build' else None
        mode = build['mode'] if build else 'full' if action == 'build' else None
        files = self.pending_files(client, backend)
        can_sky = bool(art and letter)
        choosing, checked = relation == 'new', False
        asked_elsewhere = (instances is not None or not world_cache) and (sky is not None or not can_sky)
        while True:
            self.step(2, 4, 'YOUR CHOICES')
            asking = choosing and not self.yes and self.interactive()
            keys = self.choose_instances(client, instances, ask=choosing) if world_cache else []
            if world_cache and not (asking and instances is None):
                self.say('  Dungeon and raid lighting: ' + (', '.join(self.instance_labels(keys)) or 'none'))
            need = MIN_FREE + (sum(m['bytes'] for m in manifest['files'].values()) if action == 'extract' else
                               LOCAL_BUILD_BYTES if mode == 'full' else 0)
            building = self.instances_to_build(client, locale, without, keys)   # kept groups need no space
            need += int(sum(g['est_gb'] for g in instance_maps.GROUPS if g['key'] in building) * INSTANCE_DISK * GIB)
            free = self.free_bytes(client)
            if free < need:
                raise Refusal(f'Not enough free disk space on the game drive: {free / GIB:.1f} GiB free, '
                              f'{need / GIB:.1f} GiB needed.')
            if mode == 'full':
                import install_world_cache as iwc
                # A dry run that could not be read leaves the decision to the builder for a cache it made itself.
                refusal = iwc.memory_refusal('full', self.ram() if self.ram else iwc.total_memory()) if build else \
                    self.memory_refusal(client)
                if refusal:
                    raise Refusal(refusal[0].upper() + refusal[1:] + '. Run again with --no-world-cache to install the '
                                  'renderer without static world shadows and GI.')
            if not checked:
                self.say('Checks passed.', console=False)
                checked = True
            p = {'client': client, 'locale': locale, 'backend': backend, 'art': art, 'old': old, 'letter': letter,
                 'without': without, 'variant': variant, 'manifest': manifest, 'source': source, 'action': action,
                 'instances': keys, 'raised_air': raised_air, 'previous': previous, 'relation': relation, 'mode': mode,
                 'resumes': bool(build and build.get('resumes')), 'building': building, 'files': files,
                 'sky': self.choose_sky(client, sky, ask=choosing) if can_sky else None}
            if can_sky and not (asking and sky is None):
                self.say(f'  Sky: {SKY_LABELS[p["sky"]]}')
            if not world_cache and not can_sky:
                self.say('  Nothing to choose for this install.')
            elif not choosing and not asked_elsewhere and not self.yes and self.interactive():
                self.say('  These are your choices from last time. You can change them at the next step.')
            self.step(3, 4, 'REVIEW')
            for line in self.plan_lines(p):
                self.say(line)
            verb = {'new': 'install', 'same': 'check and repair', 'older': 'install the older version'}.get(relation, 'update')
            if self.confirm(verb, can_change=not choosing and not asked_elsewhere) == 'go':
                return p
            choosing = True

    def pending_files(self, client, backend):
        """How many package files an install would write now, or None when that cannot be told. Read-only; the
        layers generated from the game files are not known before they are built."""
        emit, INSTALL.emit = INSTALL.emit, lambda *parts, **_: None
        try:
            plan = INSTALL.payload_plan(client, self.pkg.root, backend=backend, leave_out=self.left_out)[0] if self.proxy else \
                INSTALL.payload_plan(client, self.pkg.root, proxy=False, leave_out=self.left_out)[0]
        except (ValueError, OSError):
            return None
        finally:
            INSTALL.emit = emit
        return len(plan)

    def plan_lines(self, p):
        """What apply_install is about to do, in a player's words."""
        client, relation, mode, keys = p['client'], p['relation'], p['mode'], p['instances']
        combo = bool(self.tideglass)
        product = 'Northlight and Tideglass' if combo else 'Northlight'
        lines = []
        if relation == 'same':
            lines += [f'  {describe(p["previous"])} is already installed here.',
                      '  Running the installer again checks every part and repairs what is missing or changed.']
        elif relation != 'new':
            lines += [f'  Installed now:  {describe(p["previous"])}', f'  This package:   {describe(self.offers)}']
            if relation == 'older':
                lines.append('  This package is OLDER than what is installed. Going ahead replaces the newer files with '
                             'these older ones.')
        lines.append('  Nothing has been changed yet. This will:')
        if relation == 'new':
            lines.append(f'    - add {product} to the game folder (every file it replaces is copied aside first, so '
                         'uninstalling puts the game back as it was)')
        elif p['files'] is None or p['files']:
            lines.append(f'    - update the {product} files that changed')
        else:
            lines.append(f'    - keep the {product} files, which are already current')
        if p['action'] == 'skip':
            lines.append('    - leave out outdoor lighting (--no-world-cache)')
        elif p['action'] == 'keep' or mode == 'up_to_date':
            lines.append('    - keep outdoor lighting, which is up to date')
        elif p['action'] == 'extract':
            size = sum(m['bytes'] for m in p['manifest']['files'].values()) / GIB
            lines.append(f'    - unpack the prebuilt outdoor lighting ({size:.1f} GiB)')
        elif mode == 'finish':
            lines.append('    - put in place the outdoor lighting an interrupted run already built')
        elif mode == 'partial':
            lines.append('    - update part of outdoor lighting (a few minutes)')
        else:
            lines.append(('    - continue building outdoor lighting where an interrupted run stopped' if p['resumes'] else
                          '    - build outdoor lighting from your game files') +
                         f' (20-40 minutes, about {LOCAL_BUILD_BYTES // GIB} GB on the game drive)')
        left_out = []
        if p['action'] != 'skip':
            groups = {g['key']: g for g in instance_maps.GROUPS}
            names = lambda chosen: ', '.join(groups[k]['label'] for k in instance_maps.KEYS if k in chosen)
            kept = [k for k in keys if k not in p['building']]
            dropped = [k for k in self.built_instances(client) if k not in keys]
            if p['building']:
                lines.append(f'    - build dungeon and raid lighting: {names(p["building"])} '
                             f'({minutes(sum(groups[k]["est_minutes"] for k in p["building"]))}, '
                             f'{sum(groups[k]["est_gb"] for k in p["building"]):.1f} GB)')
            if kept:
                lines.append(f'    - keep dungeon and raid lighting: {names(kept)}')
            if dropped:
                lines.append(f'    - remove dungeon and raid lighting: {names(dropped)}')
            if not keys and not dropped and relation == 'new':
                left_out.append('dungeon and raid lighting (run the installer again to add it)')
        if not p['art']:
            lines.append('    - leave skies, weather and scenery as they are (--no-art-layer)')
        elif not p['letter']:
            lines.append('    - leave skies, weather and scenery as they are: other mods use every patch slot it could take')
        else:
            lines.append(f'    - build skies, weather and scenery from your game files, with {SKY_LABELS[p["sky"]]}')
        if p['raised_air']:
            lines.append('    - prepare outdoor fog for Dalaran and Telredor')
        if self.left_out:
            had = (p['previous'] or {}).get('tideglass') is not None
            lines.append('    - take out the Tideglass water installed before (water-core.dll); your water.ini and '
                         'water-host.ini are kept. The game draws its own water; the rest stays as it is' if had else
                         '    - leave out the Tideglass water' +
                         (', as chosen before' if self.asked_tideglass is None else '') +
                         ': the game draws its own water, and everything else is installed. '
                         'Installing with --tideglass adds it')
        if relation != 'new':
            lines.append('    - keep your settings (northlight-quality.ini' + (', water.ini, water-host.ini' if combo else '') +
                         ') and any look preset you edited')
        for item in left_out:
            lines.append('  Left out: ' + item)
        if mode == 'full':
            lines.append('  Takes 20-40 minutes.')
        else:
            # Measured only for the dungeon groups; the rest is a few minutes on a client with every continent.
            extra = sum(g['est_minutes'] for g in instance_maps.GROUPS if g['key'] in p['building'])
            lines.append(f'  Takes a few minutes, plus {minutes(extra)} for the dungeon lighting.' if extra else
                         '  Takes a few minutes.')
        return lines

    def apply_install(self, p, jobs):
        client, locale, action = p['client'], p['locale'], p['action']
        self.still_closed('changing anything')
        self.remember(self.given or client)   # before the long steps: a rerun that continues is offered this folder
        product = 'Northlight and Tideglass' if self.tideglass else 'Northlight'
        instance_step = action != 'skip' and bool(p['instances'] or self.built_instances(client))   # also to remove what is built
        art_step = bool(p['art'] and p['letter'])
        self.step(4, 4, 'INSTALLING')
        self.expect_parts(bool(p['raised_air']) + (action in ('extract', 'build')) + instance_step + art_step + 1)
        if p['mode'] == 'full' or p['building']:
            self.say('  If this is interrupted, run the installer again and it continues.')
        raised = {}
        if p['raised_air']:
            with self.working('Fog for Dalaran and Telredor'), tempfile.TemporaryDirectory() as work:
                code, _ = self.run(APP / 'renderer' / 'raised_air.py',
                                   ['--client', client, '--locale', locale, '--output', work])
                if code:
                    raise Failed('Could not generate raised-area samples. See the installer log.')
                for name in ('Northrend.rfs', 'Expansion01.rfs'):
                    raised['raised-fog/' + name] = (Path(work) / name).read_bytes()
        for backup, _ in reversed(p['old']):
            self.say('  Restoring the older package first:', backup)
            INSTALL.restore(client, backup)
        self.recover(client)
        self.drop_previous(client)
        report = {'client': str(client), 'locale': locale, 'variant': p['variant'], 'archive_warnings': [],
                  'instances': {'built': [], 'kept': [], 'removed': []}}
        instance_changed = False
        try:
            if action == 'extract':
                with self.working('Outdoor lighting (unpacking)'):
                    self.extract(client, p['source'], p['manifest'])
                report['world_cache'] = f'extracted (prebuilt {p["variant"]} cache {p["manifest"]["cache_digest"][:12]})'
            elif action == 'build':
                label = {'up_to_date': 'Outdoor lighting (checking)',
                         'finish': 'Outdoor lighting (finishing)',
                         'partial': 'Outdoor lighting (a few minutes)'}.get(
                             p['mode'], 'Outdoor lighting (20-40 minutes)')
                with self.working(label) as part:
                    event = self.build_cache(client, locale, jobs, p['without'])
                    if event == 'up_to_date':
                        part['result'] = 'up to date'
                report['world_cache'] = 'kept (up to date)' if event == 'up_to_date' else 'built from your game files'
                report['archive_warnings'] = self.archive_warnings(client)
            elif action == 'keep':
                report['world_cache'] = f'kept (prebuilt {p["variant"]} cache {p["manifest"]["cache_digest"][:12]})'
            else:
                report['world_cache'] = 'not installed (--no-world-cache)'
            if action != 'skip':   # nothing selected and nothing built earlier: no process
                self.remember_instances(client, p['instances'])   # before the long step: a failure keeps the intent
                if p['instances'] or self.built_instances(client):
                    # The layer is optional: its failure is a warning in the summary, never a reason to skip the renderer.
                    self.still_closed('building dungeon and raid lighting')
                    names = ', '.join(self.instance_labels(p['instances']))
                    with self.working(f'Dungeon and raid lighting: {names}' if names else 'Dungeon and raid lighting (removing it)') as part:
                        try:
                            done = self.build_instances(client, locale, jobs, p['without'], p['instances'])
                            report['instances'] = done
                            instance_changed = bool(done['built'] or done['removed'])
                            if not instance_changed:
                                part['result'] = 'up to date'
                        except Failed as e:
                            logs = getattr(e, 'logs', None) or [str(self.log.name) if self.log else 'the installer log']
                            report['instances_failed'] = {'groups': p['instances'], 'log': logs[0]}
                            self.say(f'WARNING: {e}', console=False)   # the closing lines say it, with the log to read
                            part['result'] = 'FAILED; the rest of the install goes on'
        finally:
            if p['source']:
                p['source'].close()
        if not p['art']:
            layer, why, actions = {}, 'disabled with --no-art-layer', {}
        elif not p['letter']:
            layer, why, actions = {}, 'other mods use every patch slot it could take; their patches were kept', {}
        else:
            with self.working('Skies, weather and scenery') as part:
                layer, why, actions = self.art_layer(client, locale, p['sky'], p['letter'])
                if why:
                    part['result'] = 'left as they are'
        if why:
            report['art_layer'] = 'skipped: ' + why
        else:
            report['art_layer'] = '; '.join(f'{action} {hashlib.sha256(layer[name]).hexdigest()[:8]} {name}'
                                            for name, action in actions.items())
            report['sky'] = p['sky']
            if p['sky'] != 'current' or (client / SKY_CHOICE).exists():
                layer[SKY_CHOICE] = (json.dumps({'mode': p['sky']}) + '\n').encode()
        layer.update(raised)
        layer[RECEIPT] = (json.dumps({'format': RECEIPT_FORMAT, **self.offers, 'package': self.pkg.version,
                                      **({'tideglass_left_out': True} if self.left_out else {})}, indent=2) + '\n').encode()
        if self.tideglass:
            # Include newly staged art patches in the same precedence order as the
            # mounted client. Files in layer become visible only after the transaction.
            paths = client_archives.chain(client, locale=locale, pending=actions)
            layer[WATER_ARCHIVE_LIST] = ''.join('./' + path.relative_to(client).as_posix() + '\n'
                                                for path in paths).encode('utf-8')
        backups, payload = [], 'kept (unchanged)'
        with self.working(f'{product} files') as part:
            if self.platform == 'mac':
                self.still_closed('installing the renderer')
                record = migrate().apply(client, self.pkg.dll, self.mac_backups(client))
                if record:
                    backups.append(record)
                    payload = 'installed (mods/d3d9.dll preloaded from dlls.txt)'
                plan, staged, _ = INSTALL.payload_plan(client, self.pkg.root, extra=layer, proxy=False, leave_out=self.left_out)
            else:
                plan, staged, _ = INSTALL.payload_plan(client, self.pkg.root, backend=p['backend'], extra=layer,
                                                       leave_out=self.left_out)
            files = [e['path'] for e in plan if e['path'] not in layer]
            if plan:
                self.still_closed('installing the renderer files')
                backups.append(INSTALL.commit(client, plan, staged, self.pkg.payload, self.pkg.version))
                if files:
                    payload = ('installed: ' if payload.startswith('kept') else payload + '; ') + ', '.join(files)
            if not backups:
                part['result'] = 'already current'
        if self.proxy:   # the leftover of 0.3.189-0.3.194, whatever the backend; only after the commit
            INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER).unlink(missing_ok=True)
        changed = bool(backups) or not report['world_cache'].startswith(('kept', 'not')) or instance_changed
        try:   # never fails the install: the copies just stay
            freed, kept_why = self.tidy_backups(client)
        except (OSError, ValueError) as e:
            freed, kept_why = 0, str(e)
        backups = [b for b in backups if b.exists()]   # the run's own record is often merged away
        report.update(payload=payload, backups=[str(b) for b in backups], backups_freed=freed)
        self.say('')
        relation = p['relation']
        self.say(('DONE, except for dungeon and raid lighting: ' if report.get('instances_failed') else 'DONE: ') +
                 (f'{self.title} is already installed; nothing needed changing.' if not changed else
                  f'{self.title} is installed.' if relation in ('new', 'same') else
                  f'{self.title} is installed in place of {describe(p["previous"])}.' if relation == 'older' else
                  f'{self.title} is installed, updated from {describe(p["previous"])}.'))
        # The parts above say what was done; what each one wrote is in the log (and on screen without one).
        details = not self.combined or self.log is None
        self.say(f'  Outdoor lighting: {report["world_cache"]}', console=details)
        self.say('  ' + self.instance_summary(report['instances']), console=details)
        if report.get('instances_failed'):
            failed = report['instances_failed']
            self.say('  Dungeon and raid lighting: ' + ', '.join(self.instance_labels(failed['groups'])) + f' failed (see {failed["log"]}); '
                     'the renderer was updated. Run the installer again, or use --instances none to skip.')
        if report['archive_warnings']:   # shown during the build too; repeated here, where it is not scrolled away
            import install_world_cache as iwc
            for warning in report['archive_warnings']:
                self.say('    ' + iwc.archive_warning_text(warning))
        self.say(f'  Skies, weather and scenery: {report["art_layer"]}', console=details or bool(why))
        self.say(f'  Renderer: {payload}' + (f' (graphics backend {p["backend"]})' if self.proxy else ''), console=details)
        if freed:
            self.say(f'  Undo data: {freed / (1 << 20):.1f} MiB of older Northlight copies removed; only the original files are kept', console=details)
        if kept_why:
            self.say(f'  Undo data: kept as it is: {kept_why}')
        for b in backups:
            self.say('  Undo data:', b, console=details)
        if self.platform == 'linux':
            for line in WINE_STEPS:
                self.say('  ' + line)
        self.say('  Start WoW as usual. Press Ctrl+Shift+F11' + (' (with fn on a Mac keyboard)' if self.platform == 'mac' else '') +
                 ' in game for the settings.')
        if self.combined:
            self.say("  To update later: run the new download's installer on this game folder. Your settings are kept.")
            self.say('  To undo:         close WoW and run ' +
                     ('Uninstall.cmd' if self.platform == 'windows' else 'bash uninstall.sh' if self.platform == 'linux'
                      else '"Uninstall Northlight.command"') + ' from this download or any newer one.')
        if self.proxy and p['backend'] == 'dxvk':
            self.say('  If the game closes at startup, or you have an AMD RX 5000/6000 card: run Install.cmd --backend dxvk2'
                     if self.platform == 'windows' else
                     '  If the game closes at startup: run bash install.sh --backend dxvk2')
        if self.log:
            self.say(f'  Full details of this install: {self.log.name}')
        return report

    def instance_summary(self, done):
        states = [(k, s) for s in ('built', 'kept', 'removed') for k in done[s]]
        order = {g['key']: n for n, g in enumerate(instance_maps.GROUPS)}
        label = {g['key']: g['label'] for g in instance_maps.GROUPS}
        text = ', '.join(f'{label[k]} ({s})' for k, s in sorted(states, key=lambda e: order[e[0]])) or 'none'
        return f'Dungeon and raid lighting: {text}'

    def uninstall(self, client):
        self.step(1, 3, 'YOUR GAME')
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
        owner, _ = self.cache_owner(client)
        self.say(f'Game folder: {client}', console=False)
        if found or owner in ('variant', 'local'):
            previous = self.installed(client)
            self.say(f'  Found {describe(previous) if previous else self.product_name} in {client}')
            self.step(2, 3, 'REVIEW')
            self.say('  Nothing has been changed yet. This will:')
            if found:
                self.say('    - put the game files back as they were before the first install '
                         f'({sum(len(r.get("files", [])) for _, r in found)} files restored or removed)')
                self.say('    - keep settings files you edited')
            if owner in ('variant', 'local'):
                self.say('    - delete outdoor lighting (the world-cache folder)')
            self.confirm('remove it')
            self.step(3, 3, 'REMOVING')
        with locked(client):
            return self.apply_uninstall(client, found)

    def apply_uninstall(self, client, found):
        owner, _ = self.cache_owner(client)
        self.expect_parts(bool(found) + (owner in ('variant', 'local')))
        if found:
            with self.working('Game files'):
                for backup, record in found:
                    self.say('Restoring', backup, console=self.log is None)
                    self.restore(client, backup, record)
        (client / CHOICE).unlink(missing_ok=True)
        self.remember(self.given or client, keep=False)
        if owner in ('variant', 'local'):
            with self.working('Outdoor lighting'):
                remove_tree(client / CACHE)
        elif owner == 'foreign':
            self.say(f'  {client / CACHE} was not made by this installer; left in place.')
        for name in (STAGING, INSTMP, EXTRACT, PREVIOUS):   # ours only: each carries our marker or manifest
            leftover = client / name
            if leftover.exists() and ((leftover / EXTRACTING).exists() or (leftover / VARIANT_FILE).exists() or
                                      (leftover / 'install-state.json').exists() or (leftover / 'instances-ready.json').exists() or
                                      (leftover / 'install-manifest.json').exists()):
                remove_tree(leftover)
        if self.proxy:
            INSTALL.safe_path(client, INSTALL.LEGACY_DXVK3_MARKER).unlink(missing_ok=True)   # leftover of 0.3.189-0.3.194; no record holds it
            for folder in ('renderer-backends/dxvk', 'renderer-backends/dxvk2', 'renderer-backends/legacy', 'renderer-backends'):
                try:
                    (client / folder).rmdir()   # only when empty
                except OSError:
                    pass
        self.say('')
        self.say(f'DONE: {self.product_name} is removed.' if found or owner in ('variant', 'local') else
                 f'  Nothing to remove: {self.product_name} is not installed in {client}.')
        left = self.foreign_proxy(client)
        if left:
            self.say('  ' + left)
        self.say(f'  Backups stay in {client / "renderer-backups"}.')
        if self.platform == 'linux' and (found or owner in ('variant', 'local')):
            self.say('  You can remove the WINEDLLOVERRIDES d3d9=n,b setting you added for Northlight (keep it if '
                     'another d3d9.dll, such as DXVK in the game folder, needs it).')
        if self.log:
            self.say(f'  Full details: {self.log.name}')
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

    def collect_logs(self, client):
        """One ZIP in the package folder for a problem report: this package's installer logs, the status report, a
        system summary and the game folder's GAME_LOG_FILES. Returns its path."""
        client = self.resolve_client(client)
        try:
            state = self.status(client)
        except (Failed, ValueError, OSError) as e:
            state = {'client': str(client), 'status_error': str(e)}
        crashes = sorted((client / 'Errors').glob('*.txt'), key=lambda f: f.stat().st_mtime)[-10:] \
            if (client / 'Errors').is_dir() else []
        report = {'package': self.pkg.info, 'system': system_summary(), 'status': state,
                  'crash_reports': [{'name': f.name, 'bytes': f.stat().st_size,
                                     'modified': datetime.fromtimestamp(f.stat().st_mtime).isoformat(timespec='seconds')}
                                    for f in crashes]}
        files = [client / n for n in GAME_LOG_FILES] + [f for pattern in GAME_LOG_PATTERNS for f in sorted(client.glob(pattern))]
        if self.log:
            self.log.flush()
        archive = self.pkg.root / f'northlight-logs-{datetime.now():%Y%m%d-%H%M%S}.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('report.json', json.dumps(report, indent=2) + '\n')
            for f in files:
                if f.is_file():
                    z.write(f, 'game/' + f.name)
            for f in sorted(self.pkg.logs.glob('*.log')) if self.pkg.logs.is_dir() else []:
                z.write(f, 'installer/' + f.name)
        self.say(f'Logs collected in {archive}')
        self.say('Attach this file to your issue or message. It holds the installer, renderer and Tideglass logs and '
                 'settings and folder paths from this computer; nothing from your account, characters or chat.')
        return archive

    def status(self, client):
        client = self.resolve_client(client)
        owner, manifest = self.cache_owner(client)
        state = {'package_version': self.pkg.version, 'client': str(client),
                 'world_cache': {'owner': owner, 'variant': (manifest or {}).get('variant'),
                                 'digest': (manifest or {}).get('cache_digest') or (manifest or {}).get('fingerprint'),
                                 'instances': self.chosen_instances(client),
                                 'instances_built': self.built_instances(client)},
                 'transactions': [{'backup': str(b), 'kind': r.get('kind', 'package'), 'version': r.get('version'),
                                   'files': len(r.get('files', []))} for b, r in self.all_transactions(client)],
                 'running': self.running(), 'game_started_by_this_tool': False}
        try:
            locale = client_archives.detect_locale(client)
            state['locale'] = locale
            state['variant'] = self.identify_chain(client, locale, self.art_letter(client, locale, self.foreign_z(client, locale)) or '')[0]
        except (ValueError, OSError, ImportError) as e:
            state['identity_error'] = str(e)
        if self.platform == 'mac':
            state['mac_proxy'] = migrate().status(client)
        self.say(json.dumps(state, indent=2))
        return state


# From the game folder: the renderer's and Tideglass's logs and settings, and DXVK's logs. Never account, character,
# chat or realm files (WTF, Logs, Cache, Interface). The client's own crash reports are listed by name only: what
# they hold is not ours to vouch for, and a tester can attach one when asked.
GAME_LOG_FILES = ('northlight-renderer.log', 'northlight-renderer.prev.log', 'water-core.log', 'water.log',
                  'water.ini', 'water-host.ini', 'northlight-renderer.ini', 'northlight-quality.ini', 'dxvk.conf')
GAME_LOG_PATTERNS = ('*_d3d9.log', '*_dxgi.log')


def system_summary():
    import platform
    summary = {'platform': platform.platform(), 'machine': platform.machine(), 'python': platform.python_version()}
    if sys.platform == 'darwin':
        summary['macos'] = platform.mac_ver()[0]
    return summary


def package_root():
    """The package folder (app/../) when this file runs from a package."""
    root = APP.parent
    return root if APP.name == 'app' and (root / 'BUILD-INFO.json').is_file() else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', nargs='?', choices=['install', 'uninstall', 'status', 'logs'])
    # The launchers pass their own action here, so `Install Northlight.command status --client X` works too.
    ap.add_argument('--default-action', choices=['install', 'uninstall', 'status', 'logs'], help=argparse.SUPPRESS)
    ap.add_argument('--client', help='the game folder (the folder with wow.exe); asked when missing')
    ap.add_argument('--locale', help='the game language, e.g. enUS (default: WTF/Config.wtf, else the only one)')
    ap.add_argument('--cache', help='the Northlight-cache-<variant>-<digest>.zip (or its expanded folder)')
    ap.add_argument('--backend', choices=['dxvk', 'dxvk2', 'native', 'legacy'], default=None,
                    help='Windows and Linux: the Direct3D 9 behind the renderer (default dxvk = DXVK 3.1.1, or the dxvk2 or native already '
                         'installed; dxvk2 = DXVK 2.7.1 for AMD RX 5000/6000 or drivers DXVK 3 does not support; '
                         'legacy = the d3d9.dll found in the game folder)')
    ap.add_argument('--sky', choices=SKY_MODES,
                    help='current skies or experimental preservation of more HD sky artwork, untested with most HD skies; '
                         'default: keep the last choice, current on first install')
    art = ap.add_mutually_exclusive_group()
    art.add_argument('--art-layer', dest='art_layer', action='store_true', help='build the default lighting and scenery MPQs')
    art.add_argument('--no-art-layer', dest='art_layer', action='store_false', help='skip generating lighting and scenery MPQs')
    ap.set_defaults(art_layer=None)
    ap.add_argument('--raised-air', action=argparse.BooleanOptionalAction, default=None, help='generate extra outdoor fog coverage for Dalaran and Telredor')
    ap.add_argument('--tideglass', action=argparse.BooleanOptionalAction, default=None,
                    help='--no-tideglass installs the Northlight renderer and leaves the Tideglass water out, for the '
                         'game\'s own water or one of your own; a later run keeps that choice until --tideglass')
    ap.add_argument('--no-world-cache', action='store_true', help='install the renderer without a world cache')
    ap.add_argument('--instances', metavar='SPEC', help='dungeon and raid world cache groups to have: numbers 1-6, keys, all or none '
                    '(default: asked; the groups the client has, else none)')
    ap.add_argument('--jobs', type=int, help='parallel builders for a local cache build (default: by memory)')
    ap.add_argument('--yes', action='store_true', help='answer the recommended default to every question')
    ap.add_argument('--package', type=Path, help=argparse.SUPPRESS)   # development: a package folder to use
    args = ap.parse_args(argv)
    args.action = args.action or args.default_action
    if not args.action:
        ap.error('choose install, uninstall, status or logs')
    platform = host_platform()
    if platform is None:
        print('ERROR: this installer runs on macOS (WoWSilicon), Windows or Linux (Wine/Proton).', file=sys.stderr)
        return 2
    root = args.package or package_root()
    if root is None:
        print('ERROR: run this from an unpacked Northlight package (Install Northlight.command, Install.cmd or install.sh).',
              file=sys.stderr)
        return 2
    installer = Installer(Package(root, platform), platform, yes=args.yes, tideglass=args.tideglass)
    installer.memory = user_state_file()
    installer.open_log(args.action)
    try:
        if args.action == 'install':
            installer.banner(f'{installer.title} installer')
            installer.install(args.client, args.locale, args.cache, args.backend, args.art_layer,
                              not args.no_world_cache, args.jobs, instances=args.instances, sky=args.sky,
                              raised_air=args.raised_air)
        elif args.action == 'uninstall':
            installer.banner(f'{installer.product_name} uninstaller')
            installer.uninstall(args.client)
        elif args.action == 'logs':
            installer.collect_logs(args.client)
        else:
            installer.status(args.client)
        return 0
    except Refusal as e:
        installer.stopped('STOPPED:', e)
        return 2
    except (Failed, ValueError, OSError) as e:
        installer.stopped('ERROR:', e)
        return 1
    except KeyboardInterrupt:
        installer.stopped('Interrupted.', 'Run the installer again to continue.')
        return 1
    finally:
        if installer.log:
            installer.log.close()


if __name__ == '__main__':
    sys.exit(main())
