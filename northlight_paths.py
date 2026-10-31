"""Repository layout and machine configuration for the Northlight renderer (stdlib only, Python >= 3.9).

Scripts find repository files, the game client and the toolchain here, so no code names a
machine path. A machine path comes from, in order: the environment variable, then
northlight.local.ini [paths] at the repository root (ignored by git; see
northlight.local.ini.example), then a repository-relative default. A missing optional path
raises Missing with the variable to set; scripts/run_tests.py turns that into a SKIP via the
test's `requires=` tag.

Sources, shaders, scripts and tests share one flat namespace of unique basenames, as the C++
includes do: src('world_renderer.h') finds a source in whichever src/<group>/ holds it, and a
test's own files are its siblings in tests/.
"""
import configparser
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent
RENDERER = REPO / 'renderer'       # windows-package/, mac-package/, pipeline and packaging tools, the built DLL
SRC = REPO / 'src'
SRC_DIRS = sorted(d for d in SRC.iterdir() if d.is_dir()) if SRC.is_dir() else []   # DLL sources by group; one -I each
GENERATED = SRC / 'generated'      # headers written by scripts/generate_*.py and scripts/shaders/compile_*.py
GAMEDATA = SRC / 'gamedata'        # tables derived from the game's MPQs (renderer/extract_*.py)
SHADERS = REPO / 'shaders'         # *.hlsl and their *-shader-build.json manifests
COMPILED = SHADERS / 'compiled'    # compiled bytecode (<Entry>.bin) and its disassembly (.bin.asm)
SHADER_DIRS = [SHADERS, COMPILED]
SCRIPTS = REPO / 'scripts'
SCRIPT_DIRS = [SCRIPTS, SCRIPTS / 'shaders']
# Pipeline and packaging tools that have not moved yet (world_*_builder.py, extract_*.py,
# migrate_mac_proxy.py, windows-package/, their JSON). Never sources: check_layout rejects a
# source, shader or build script in renderer/.
LEGACY_DIRS = [RENDERER]
_SOURCE_SUFFIXES = {'.h', '.inl', '.inc', '.def', '.cpp', '.hlsl', '.bin', '.asm'}
TESTS = REPO / 'tests'
SUPPORT = TESTS / 'support'        # d3d9.h / windows.h shims for native test builds
FIXTURES = TESTS / 'fixtures'
RECORDS = RENDERER / 'records'     # release evidence, ignored by git; tests write here only with --record
ZIG_VERSION = '0.15.2'
LOCAL_INI = REPO / 'northlight.local.ini'


class Missing(RuntimeError):
    """An optional machine resource (client, toolchain, archive, ...) is not configured."""


_ini_cache = None


def setting(name):
    """NORTHLIGHT_<NAME> from the environment, else northlight.local.ini [paths] <name>, else None."""
    global _ini_cache
    value = os.environ.get('NORTHLIGHT_' + name.upper())
    if value:
        return value
    if _ini_cache is None:
        _ini_cache = {}
        if LOCAL_INI.is_file():
            # A broken optional file must never stop a tool (renderer_status on/off reads it too).
            parser = configparser.ConfigParser(interpolation=None, strict=False)
            try:
                parser.read(LOCAL_INI, encoding='utf-8')
                _ini_cache = dict(parser['paths']) if parser.has_section('paths') else {}
            except (configparser.Error, UnicodeDecodeError, OSError) as e:
                print(f'northlight_paths: ignoring unreadable {LOCAL_INI.name}: {str(e).splitlines()[0]}', file=sys.stderr)
    return _ini_cache.get(name) or None


def _path(name):
    value = setting(name)
    return Path(value).expanduser() if value else None


def _missing(what, name, hint=''):
    raise Missing(f'{what} not found: set NORTHLIGHT_{name.upper()} or [paths] {name} in northlight.local.ini{hint}.')


def is_client(p):
    """A WoW client folder: Wow.exe and Data/."""
    has = lambda *names: any((p / n).exists() for n in names)
    return has('Wow.exe', 'wow.exe', 'WoW.exe') and has('Data', 'data')


def client_root(required=True):
    """The WoW 3.3.5a client folder. Default: the repository's parent, if it is a client."""
    p = _path('client')
    if p is None and is_client(REPO.parent):
        p = REPO.parent
    if p is None or not is_client(p):
        if required:
            _missing('WoW client (folder with Wow.exe and Data/)', 'client')
        return None
    return p


def world_cache(required=True):
    """The client's world cache (<client>/world-cache, built by scripts/install_world_cache.py)."""
    client = client_root(required)
    p = client / 'world-cache' if client else None
    if p is None or not (p / 'Azeroth').is_dir():
        if required:
            _missing('world cache (<client>/world-cache/Azeroth; build it with scripts/install_world_cache.py)', 'client')
        return None
    return p


def tools():
    """Third-party tool downloads (zig, StormLib build, DXVK, ...). Default: tools/ in the repo."""
    return _path('tools') or REPO / 'tools'


def backups(required=True):
    """Old renderer tarballs (backups/renderer-<v>-before-*/...tar.gz) that differential tests read."""
    p = _path('backups') or REPO / 'backups'
    if not p.is_dir():
        if required:
            _missing('renderer backups directory', 'backups')
        return None
    return p


def archive(required=True):
    """The renderer part of the external archive (rollback-*/, runtime-diagnostics-*/, ...)."""
    p = _path('archive')
    if p is None or not (p / 'renderer').is_dir():
        if required:
            _missing('external archive (the folder that holds renderer/rollback-*)', 'archive')
        return None
    return p / 'renderer'


def dll(required=True):
    """The built renderer DLL: renderer/frd9.dll unless NORTHLIGHT_DLL names another."""
    p = _path('dll') or RENDERER / 'frd9.dll'
    if not p.is_file():
        if required:
            _missing('built renderer DLL (run scripts/build_renderer.py)', 'dll')
        return None
    return p


def shader_corpus():
    """Optional captured world-shader sample corpus; tests run extra sub-cases when it is set."""
    p = _path('shader_corpus')
    return p if p is not None and p.is_file() else None


_zig = None


def zig(required=True):
    """zig 0.15.2: NORTHLIGHT_ZIG, northlight.local.ini, tools/zig-*/zig, then PATH."""
    global _zig
    if _zig is None:
        candidates = [_path('zig')] if setting('zig') else [
            *sorted(tools().glob('zig-*/zig*')), *([Path(shutil.which('zig'))] if shutil.which('zig') else [])]
        found = []
        for c in candidates:
            if c is None or not c.is_file() or c.suffix not in ('', '.exe'):
                continue
            try:
                version = subprocess.run([str(c), 'version'], capture_output=True, text=True, timeout=60).stdout.strip()
            except OSError:
                continue
            if version == ZIG_VERSION:
                _zig = c
                break
            found.append(f'{c} ({version or "not runnable"})')
        if _zig is None:
            if required:
                _missing(f'zig {ZIG_VERSION}', 'zig', '; found: ' + ', '.join(found) if found else '')
            return None
    return _zig


def windows_headers(required=True):
    """The MinGW-w64 Windows SDK headers shipped with zig (d3d9.h, ...)."""
    z = zig(required)
    return z.parent / 'lib/libc/include/any-windows-any' if z else None


def zig_env():
    """Environment for zig: the global cache defaults to tools/zig-cache (ignored by git)."""
    return dict(os.environ, ZIG_GLOBAL_CACHE_DIR=os.environ.get('ZIG_GLOBAL_CACHE_DIR') or str(tools() / 'zig-cache'))


def wine_root(required=True):
    """Folder with bin/wine (and lib/external). NORTHLIGHT_WINE, northlight.local.ini, WoWSilicon's bundle, PATH."""
    p = _path('wine')
    if p is None:
        bundled = Path('/Applications/WoWSilicon.app/Contents/Resources/Wine')  # layout: allow-machine-path
        on_path = shutil.which('wine')
        p = bundled if (bundled / 'bin/wine').is_file() else Path(on_path).resolve().parents[1] if on_path else None
    if p is None or not (p / 'bin/wine').is_file():
        if required:
            _missing('Wine (folder containing bin/wine)', 'wine')
        return None
    return p


def stormlib(required=True):
    """The StormLib shared library used by mpq.py."""
    p = _path('stormlib') or tools() / 'storm-build/storm.framework/storm'
    if not p.is_file():
        if required:
            _missing('StormLib library', 'stormlib')
        return None
    return p


def stormlib_source(required=True):
    """The pinned StormLib source tree that scripts/build_stormlib.py compiles: tools/StormLib-master."""
    p = tools() / 'StormLib-master'
    if not (p / 'CMakeLists.txt').is_file():
        if required:
            _missing('StormLib source tree (StormLib-master, see renderer/package-pins.json)', 'tools')
        return None
    return p


def protect_output(path):
    """Keep generated fixtures out of the client's recursive loose-file scan.

    WoW skips .svn directories. Test installations can contain fake caches and
    long paths which must never be mounted as game content on the next launch.
    """
    path = Path(path)
    client = client_root(False)
    if client:
        try:
            relative = path.resolve().relative_to(client.resolve())
        except ValueError:
            pass
        else:
            if '.svn' not in (part.lower() for part in relative.parts):
                return path / '.svn'
    return path


def out():
    """Build and test output root (ignored by git)."""
    return protect_output(_path('out') or REPO / 'out')


def output_dir():
    """Where a test writes its report files: NORTHLIGHT_TEST_OUTPUT_DIR, else out/test-output/<script>.

    scripts/run_tests.py sets NORTHLIGHT_TEST_OUTPUT_DIR per test; with --record <version> it
    points every test at renderer/records/validation-<version> (release evidence)."""
    p = os.environ.get('NORTHLIGHT_TEST_OUTPUT_DIR')
    if p:
        p = protect_output(p)
    else:
        main = getattr(sys.modules.get('__main__'), '__file__', None) or 'adhoc'
        p = out() / 'test-output' / Path(main).stem
    p.mkdir(parents=True, exist_ok=True)
    return p


# ---- flat source/test namespace ----

_index = None
_IGNORED_SUFFIXES = {'.dll', '.pdb', '.exe', '.o', '.obj', '.lib', '.exp', '.pyc'}


def _listing(d):
    """Files under d that git tracks or would track (untracked, not ignored); a plain walk without git.

    git runs only in a repository checkout: in a package (no .git) a macOS without the command line
    tools has only the /usr/bin/git stub, which would pop up the tools installer dialog."""
    if not (REPO / '.git').exists():
        return _walk(d)
    try:
        r = subprocess.run(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', '.'],
                           cwd=d, capture_output=True, timeout=60)
        if r.returncode == 0:
            return sorted({Path(n) for n in r.stdout.decode('utf-8', 'surrogateescape').split('\0') if n and (d / n).is_file()})
    except OSError:
        pass
    return _walk(d)


def _walk(d):
    return sorted(p.relative_to(d) for p in d.rglob('*') if p.is_file() and p.suffix not in _IGNORED_SUFFIXES
                  and '__pycache__' not in p.parts and '.zig-cache' not in p.parts)


def _build_index():
    global _index
    index, owners = {}, {}
    for d in [*SRC_DIRS, *SHADER_DIRS, *SCRIPT_DIRS, *LEGACY_DIRS, TESTS]:
        if not d.is_dir():
            continue
        for rel in _listing(d):
            key = rel.as_posix()
            if len(rel.parts) == 1 and key in owners and key != 'README.md':
                raise RuntimeError(f'Duplicate basename {key} in {owners[key]} and {d}; basenames must be unique.')
            owners.setdefault(key, d)
            index.setdefault(key, d / rel)
    _index = index
    return index


def _lookup(name, dirs):
    index = _index if _index is not None else _build_index()
    p = index.get(Path(name).as_posix())
    if p is not None and p.suffix in _SOURCE_SUFFIXES and any(d in p.parents for d in LEGACY_DIRS):
        p = None   # renderer/ serves only the not-yet-moved pipeline and packaging files, never a source
    if p is None or not any(p.parent == d or d in p.parents for d in dirs):
        close = [k for k in index if '/' not in k and k.lower() == Path(name).name.lower()]
        raise FileNotFoundError(f'{name}: not a file in {", ".join(str(d.relative_to(REPO)) for d in dirs)}'
                                + (f' (did you mean {close[0]}?)' if close else ''))
    return p


def src(name):
    """A source or shader file by exact name (a basename, or a path relative to its dir). Also finds
    the pipeline and packaging files still in renderer/ (LEGACY_DIRS), e.g. 'windows-package/install.py'."""
    return _lookup(name, [*SRC_DIRS, *SHADER_DIRS, *LEGACY_DIRS])


def tracked(name):
    """A source, shader, script or test file by exact name; for hash lists that mix them."""
    return _lookup(name, [*SRC_DIRS, *SHADER_DIRS, *SCRIPT_DIRS, *LEGACY_DIRS, TESTS])


def sources(suffixes):
    """All production source files (top level of each source dir) with one of the suffixes."""
    index = _index if _index is not None else _build_index()
    return sorted(p for k, p in index.items() if '/' not in k and p.suffix in suffixes and any(p.parent == d for d in SRC_DIRS))


def include_flags():
    """-I flags for the production headers (bare-name includes)."""
    return [f for d in SRC_DIRS for f in ('-I', str(d))]


def test_include_flags():
    """-I flags for native test builds: production headers plus the test headers."""
    return [*include_flags(), '-I', str(TESTS)]


def use_source_modules():
    """Make the repository's Python modules importable (build_environment, compile_*_shaders,
    generate_*, world_scene_builder, ...)."""
    for d in reversed([*SCRIPT_DIRS, *LEGACY_DIRS]):
        if str(d) not in sys.path:
            sys.path.insert(1, str(d))


if __name__ == '__main__':
    # Print the resolved configuration: `python3 northlight_paths.py`
    rows = [('repo', REPO), ('out', out()), ('tools', tools())]
    for name, fn in [('client', client_root), ('zig', zig), ('wine', wine_root), ('stormlib', stormlib),
                     ('archive', archive), ('backups', backups), ('dll', dll)]:
        try:
            rows.append((name, fn()))
        except Missing as e:
            rows.append((name, f'-- {e}'))
    rows.append(('shader_corpus', shader_corpus() or '-- not set (optional)'))
    for k, v in rows:
        print(f'{k:14} {v}')
