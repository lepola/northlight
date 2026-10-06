#!/usr/bin/env python3
"""Build the renderer's world cache from the player's own client (stdlib + StormLib; no game, no Wine).

    python3 scripts/install_world_cache.py                        # <client>/world-cache, all four continents
    python3 scripts/install_world_cache.py --client C --output O  # any client, any output folder
    python3 scripts/install_world_cache.py --maps Azeroth --tiles 31 48 33 50 --output O   # a partial test build
    python3 scripts/install_world_cache.py --progress human       # readable progress (the installer)

Steps: world_scene_builder per map (in parallel, largest map first, as many as the per-map memory
budgets fit into free memory), then world_lights_builder, regional_fog_builder and
build_celestial_disc_assets, then tests/validate_world_cache.py. Every builder runs as
`python -I -X utf8` and reads the archive chain without our own art layer (--without, default z),
so installing the art layer never makes the cache stale.

Per-step digests decide what is rebuilt. scene:<map> covers the archive chain (lower-case names,
sizes and mtimes, in the game's order), the locale, the tiles and the scene builder's sources;
lights and fog cover the scene digests and their own sources; celestial covers the chain and its
sources. A changed scene digest rebuilds everything in <output>.staging, and only a complete,
validated build replaces <output>, by renames (the previous cache is deleted afterwards unless
--keep-previous). Otherwise only the stale lights/, fog/ or celestial/ are rebuilt in the staging
folder and swapped in folder by folder. <output>/install-manifest.json is written last. A manifest
from before the per-step digests is migrated: its recorded chain and sources give the digests it
was built with (our art letter in its chain is ignored). A rerun with the same digests does
nothing (--force rebuilds). A cache built with a builder source listed in SOURCE_EQUIVALENTS counts
as built with today's when the change made no difference there (for 0.3.183's, when its reports
show no oversized MOGP: clamp_free).

What the builders tolerated (a WMO group's oversized MOGP clamped, a WMO fog could not read) goes to
the manifest's 'tolerated' and one 'tolerated' progress line; more than 5% unreadable WMOs fails.
An archive the builders could not list (no (listfile)) or open (a patch archive only) does not fail
the build: each gets one 'archive_warning' progress line as soon as a scene builder reports it, and
the manifest's tolerated 'archive_warnings' (world_scene_builder.Assets).

A killed run resumes from its staging folder when the digests still match (stale *.tmp files are
swept first), and a staging folder whose build had finished completes its swap. On Windows,
renames and deletes are retried while a virus scanner holds a file. A full build on a computer
with less than 8 GB of installed memory is refused before anything is built (a lights, fog or
celestial update needs about 0.5 GB and always runs). When the cache is complete, a leftover
<output>.previous and <output>/<step>.previous from an interrupted swap are deleted (unless
--keep-previous). The orchestrator touches only <output>, <output>.staging and <output>.previous
(side_folders()); <output>.extract belongs to the installer's zip extraction and is never touched.
--without '' reads the whole chain: the installer passes it when its patch-z ownership check finds
a z archive that is not ours, so that archive counts as client content.

Progress is one JSON object per line (--progress json, the default) or readable text
(--progress human). The last stdout line is always JSON: {"event": "done"|"up_to_date"|"failed", ...}.
Exit status: 0 done or up to date, 1 the build failed, 2 bad input (client, locale) or too little memory.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402
import client_archives  # noqa: E402

MAPS = ['Azeroth', 'Kalimdor', 'Expansion01', 'Northrend']
POST = ['lights', 'fog', 'celestial']
FORMAT = 'northlight-world-cache-install/1'
# The sources each step's output depends on. lights and fog also depend on the scene digests
# (they read the FGS3 tiles), which cover world_scene_builder.py, the module they import.
STEP_SOURCES = {
    'scene': ['world_scene_builder.py', 'm2_visibility.py', 'mpq.py', 'client_archives.py'],
    'lights': ['world_lights_builder.py', 'world_light_placements.py', 'outdoor_light_profiles.py',
               'outdoor-light-profiles.json'],
    'fog': ['regional_fog_builder.py', 'world_light_placements.py', 'forest_regions.inc'],
    'celestial': ['build_celestial_disc_assets.py', 'world_scene_builder.py', 'mpq.py', 'client_archives.py'],
}
TOP_LEVEL = ['client_archives.py', 'mpq.py']   # at the repository root, outside fp.tracked()'s namespace
SOURCES = sorted({n for names in STEP_SOURCES.values() for n in names} | {'validate_world_cache.py'})
# Earlier bytes of a source whose output equals the current one's: a cache built with them is not
# stale for that source. {source: {sha256: True when the cache must also be clamp_free()}}.
# - 0.3.183's scene and fog builders (unchanged since 0.3.166): 0.3.184 clamps an oversized top-level
#   MOGP of a WMO group file, so its output differs only where 0.3.183 hit such an overrun
#   (clamp_free() checks the cache's reports for one).
# - 0.3.191's world_scene_builder.py (since 0.3.184) and mpq.py (since 0.3.166): 0.3.192 builds where
#   they stopped (an archive without a (listfile), an unopenable custom patch, an unreadable file), so
#   every cache they finished is what 0.3.192 builds.
# Revisit whenever one of these files changes again.
SOURCE_EQUIVALENTS = {
    'world_scene_builder.py': {'a54e08fe2774cd79702c04cf6e3dfd17854a12da4b6305ca38a4ade0545f0484': True,
                               '9c7c13d16fab67e4d65d81c8faf88fd20a129f6b02e1faa6734501885434ad4a': False},
    'regional_fog_builder.py': {'30a2af943dd0f758c1bd6067b9d58bd6b6e2f64b85af2394f2f9e6fc01daa7e3': True},
    'mpq.py': {'0b949fc438e1b71433242cd580848c7cf14281696e409903710f4191ac4b4d7a': False},
}
PGOM_OVERRUN = "Chunk exceeds file: b'PGOM'"   # 0.3.183's error for an oversized top-level MOGP
# Memory budget of one world_scene_builder process per continent (the builder keeps every mesh and
# texture of its map). Measured peak RSS, stock 3.3.5a, 2026-09-26: Northrend 4.8 GB, Azeroth 3.2,
# Expansion01 3.0, Kalimdor 2.3; the budgets leave room for HD textures.
MAP_MEMORY_GB = {'Northrend': 6.5, 'Azeroth': 4.5, 'Expansion01': 4.5, 'Kalimdor': 3.5}
POST_MEMORY_GB = 0.5         # lights 0.3 GB, fog and celestial 0.15
MIN_TOTAL_GB = 7.0           # installed RAM; an "8 GB" PC may report a little less than 8 GiB
LOCK_RETRIES = (0.2, 0.4, 0.8, 1.6, 3.0) if os.name == 'nt' else ()
TMP = re.compile(r'\.\d+\.tmp$')
ART_LETTER = re.compile(r'patch(?:-[a-z]{4})?-([0-9a-z])\.mpq$')
PROGRESS = 'json'
print_lock = threading.Lock()
warned, warned_lock = set(), threading.Lock()   # archives this run has shown an archive_warning for


def emit(**fields):
    with print_lock:
        if PROGRESS == 'json':
            print(json.dumps(fields), flush=True)
        else:
            text = human(fields)
            if text:
                print(text, flush=True)


def archive_warning_text(w):
    """The installer's words for a world_scene_builder archive warning {archive, problem}."""
    if w.get('problem') == 'unreadable':
        return (f"Warning: {w['archive']} could not be opened as an MPQ archive, so the world cache is built "
                'without it: its files are missing from the static shadows and GI, and models it changes may look '
                'wrong there.')
    return (f"Warning: {w['archive']} has no (listfile), so its files could not be listed. Files it replaces are "
            'still used, but new files only it adds may be missing from the static shadows and GI, and models it '
            'changes may look wrong there.')


def warn_archive(warning):
    """One 'archive_warning' event per archive and run: every builder process reports the same chain."""
    with warned_lock:
        if warning['archive'] in warned:
            return
        warned.add(warning['archive'])
    emit(event='archive_warning', **warning)


def human(f):
    """A readable line for a progress event, or None to stay quiet."""
    event, what = f.get('event'), f.get('map') or f.get('step')
    if event == 'fingerprint':
        return f"World cache: {f['archives']} archives, locale {f['locale']}"
    if event == 'plan':
        if f['mode'] == 'full':
            return (f"Building the world cache: {', '.join(f['maps'])}, up to {f['jobs']} maps at a time "
                    f"({f['total_memory_gb']} GB memory). This takes 10 to 40 minutes.")
        return f"Updating {', '.join(f['steps'])}; the world geometry is up to date."
    if event == 'start':
        return f'{what}: started'
    if event == 'progress':
        return f"{what}: {f['percent']}% ({f['done']}/{f['total']} tiles)"
    if event == 'end':
        state = 'done' if f['exit'] == 0 else f"FAILED (exit {f['exit']})"
        return f"{what}: {state} in {f['seconds']:.0f} s"
    if event == 'resume':
        return f"Resuming the interrupted build in {f['staging']}"
    if event == 'finish_staging':
        return f"Completing the swap of a finished build from {f['staging']}"
    if event == 'removed_previous_cache':
        return f"Removed {f['path']} left by an interrupted swap"
    if event == 'recovered_previous_cache':
        return f"Restored {f['path']} after an interrupted swap"
    if event == 'archive_warning':
        return archive_warning_text(f)
    if event == 'tolerated':
        return (f"Tolerated: {f['clamped_wmo_groups']} WMO groups with an oversized MOGP (clamped), "
                f"{f['fog_unreadable_wmos']} unreadable WMOs left out of the fog")
    if event == 'low_memory':
        return (f"Warning: {f['available_gb']} GB of free memory, {what} needs about {f['needs_gb']} GB; "
                'the build may be slow. Close other programs if you can.')
    return None


def final(event, code, **fields):
    """The last stdout line, JSON in both progress modes; returns the exit status."""
    if PROGRESS == 'human':
        text = {'done': f"World cache ready in {fields.get('seconds', 0) / 60:.1f} min",
                'up_to_date': 'World cache is up to date',
                'failed': f"World cache build FAILED: {fields.get('error') or '; '.join(fields.get('problems', []))}"}[event]
        print(text, flush=True)
    with print_lock:
        print(json.dumps({'event': event, **fields}), flush=True)
    return code


def retry(fn, *args):
    """fn(*args), retried on PermissionError after each LOCK_RETRIES delay (6 attempts on Windows)."""
    for delay in LOCK_RETRIES:
        try:
            return fn(*args)
        except PermissionError:
            time.sleep(delay)
    return fn(*args)


def source_path(name):
    return fp.REPO / name if name in TOP_LEVEL else fp.tracked(name)


def source_hashes():
    return {n: hashlib.sha256(source_path(n).read_bytes()).hexdigest() for n in SOURCES}


def is_art_layer(archive, without):
    m = ART_LETTER.search(archive.lower())
    return bool(m and m.group(1) in without)


def gather_inputs(client, view, locale, maps, tiles, without):
    chain = client_archives.chain(client, view, locale, without)
    rows = client_archives.fingerprint(chain, client)['archives']
    return {'format': FORMAT, 'locale': locale, 'view': view, 'without': without, 'maps': maps, 'tiles': tiles,
            'archives': [dict(r, archive=r['archive'].lower()) for r in rows], 'sources': source_hashes()}


def _hash(*parts):
    return hashlib.sha256(json.dumps(parts, sort_keys=True).encode()).hexdigest()


def step_digests(inputs, without=None):
    """{'scene:<map>': .., 'lights': .., 'fog': .., 'celestial': ..} of an inputs record. The view is not
    part of it (the chain is), and archives of the art letters in `without` are ignored."""
    without = inputs.get('without', '') if without is None else without
    chain = [[a['archive'].lower(), a['bytes'], a['mtime_ns']] for a in inputs['archives']
             if not is_art_layer(a['archive'], without)]
    sources = inputs['sources']
    own = {step: {n: sources.get(n) for n in names} for step, names in STEP_SOURCES.items()}
    tiles = list(inputs['tiles']) if inputs['tiles'] else None
    scene = {m: _hash('scene', chain, inputs['locale'], m, tiles, own['scene']) for m in MAPS if m in inputs['maps']}
    digests = {f'scene:{m}': d for m, d in scene.items()}
    digests['lights'] = _hash('lights', list(scene.values()), own['lights'])
    digests['fog'] = _hash('fog', list(scene.values()), own['fog'])
    digests['celestial'] = _hash('celestial', chain, inputs['locale'], own['celestial'])
    return digests


def recorded_digests(manifest, without):
    """The digests an installed cache was built with; migrated from a manifest that predates them."""
    if isinstance(manifest.get('digests'), dict):
        return manifest['digests']
    try:
        return step_digests(manifest['inputs'], without)
    except (KeyError, TypeError, AttributeError):
        return {}


def read_json(path):
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
        return data if isinstance(data, dict) else None
    except (OSError, ValueError):
        return None


def present(output, step, maps):
    if step == 'lights':
        return all((output / 'lights' / f'{m}.fgl').is_file() for m in maps)
    if step == 'fog':
        return (output / 'fog' / 'manifest.json').is_file()
    return all((output / 'celestial' / f'{b}.fct').is_file() for b in ('sun', 'moon'))


def clamp_free(output, built):
    """True when <output>'s scene reports cover every built tile and neither they nor the fog manifest
    record an oversized MOGP (a killed and resumed build, or unreadable JSON, is not clamp free)."""
    for m in built:
        covered = set()
        for report in output.glob(f'build-{m}-*.json'):
            data = read_json(report)
            try:
                covered |= {f'{x}_{y}' for x, y in (r['tile'] for r in data['generated'])}
                if PGOM_OVERRUN in json.dumps([data['unsupported_assets'], data['failures']]):
                    return False
            except (KeyError, TypeError, ValueError):
                return False
        if not {p.stem for p in (output / m).glob('*.fg3')} <= covered:
            return False
    fog = read_json(output / 'fog' / 'manifest.json')
    return fog is not None and PGOM_OVERRUN not in json.dumps(fog.get('unsupported'))


def equivalent_digests(recorded, digests, output, without, built):
    """The recorded inputs' digests with SOURCE_EQUIVALENTS sources taken as today's, or None when that
    changes nothing, their scenes still differ, or a source that needs it is not provably clamp free."""
    inputs = recorded.get('inputs')
    try:
        if not isinstance(inputs, dict) or step_digests(inputs, without) != recorded_digests(recorded, without):
            return None
        sources, clamp = dict(inputs['sources']), False
        for n, equal in SOURCE_EQUIVALENTS.items():
            if sources.get(n) in equal:
                clamp |= equal[sources[n]]
                sources[n] = hashlib.sha256(source_path(n).read_bytes()).hexdigest()
        if sources == inputs['sources']:
            return None
        new = step_digests(dict(inputs, sources=sources), without)
    except (KeyError, TypeError, AttributeError):
        return None
    if any(new.get(k) != v for k, v in digests.items() if k.startswith('scene:')) or (clamp and not clamp_free(output, built)):
        return None
    return new


def plan(digests, recorded, output, force, without):
    """('full', POST) | ('partial', stale post steps) | ('up_to_date', [])."""
    if force or recorded is None:
        return 'full', POST
    old = recorded_digests(recorded, without)
    scenes = [k for k in digests if k.startswith('scene:')]
    built = recorded.get('maps_built') or recorded.get('inputs', {}).get('maps') or []
    if old != digests:
        old = equivalent_digests(recorded, digests, output, without, built) or old
    if sorted(k for k in old if k.startswith('scene:')) != sorted(scenes) or \
            any(old[k] != digests[k] for k in scenes) or not all((output / m).is_dir() for m in built):
        return 'full', POST
    stale = [s for s in POST if old.get(s) != digests[s] or not present(output, s, built)]
    return ('partial', stale) if stale else ('up_to_date', [])


def available_memory():
    """Bytes of memory a new process can use without swapping (best effort, stdlib only)."""
    try:
        if sys.platform == 'darwin':
            text = subprocess.run(['vm_stat'], capture_output=True, text=True, timeout=10).stdout
            page = int(re.search(r'page size of (\d+)', text).group(1))
            pages = sum(int(m.group(1)) for m in re.finditer(r'Pages (?:free|inactive|speculative|purgeable):\s+(\d+)', text))
            return pages * page
        if sys.platform.startswith('linux'):
            with open('/proc/meminfo') as f:
                return int(re.search(r'MemAvailable:\s+(\d+) kB', f.read()).group(1)) * 1024
        if sys.platform == 'win32':
            return _windows_memory().avail
    except (OSError, AttributeError, ValueError, subprocess.SubprocessError):
        pass
    return total_memory() // 2


def total_memory():
    """Bytes of installed physical memory: GetPhysicallyInstalledSystemMemory on Windows (the usable
    total of GlobalMemoryStatusEx is lower on PCs with integrated graphics), hw.memsize on macOS."""
    try:
        import ctypes
        if sys.platform == 'win32':
            kib = ctypes.c_ulonglong()
            if ctypes.windll.kernel32.GetPhysicallyInstalledSystemMemory(ctypes.byref(kib)):
                return kib.value * 1024
            return _windows_memory().total
        if sys.platform == 'darwin':
            value, size = ctypes.c_uint64(), ctypes.c_size_t(8)
            if ctypes.CDLL(None).sysctlbyname(b'hw.memsize', ctypes.byref(value), ctypes.byref(size), None, 0) == 0:
                return value.value
        return os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_PHYS_PAGES')
    except (OSError, AttributeError, ValueError):
        return 8 << 30


def _windows_memory():
    import ctypes

    class Status(ctypes.Structure):
        _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong)] + \
                   [(n, ctypes.c_ulonglong) for n in ('total', 'avail', 'totalPage', 'availPage',
                                                      'totalVirtual', 'availVirtual', 'availExtended')]
    s = Status(dwLength=ctypes.sizeof(Status))
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(s))
    return s


def map_jobs(maps, override_gb=None):
    """[(map, memory budget in bytes)], largest first."""
    return sorted(((m, (override_gb or MAP_MEMORY_GB[m]) * (1 << 30)) for m in maps), key=lambda job: -job[1])


def admit(needs, used, running, jobs, budget):
    """Index of the first pending job (they are sorted largest first) that fits into the memory budget
    next to the running ones; the first job always starts when nothing runs. None: wait."""
    if not needs or running >= jobs:
        return None
    if not running:
        return 0
    return next((i for i, need in enumerate(needs) if used + need <= budget), None)


def sweep(folder):
    """Delete the *.<pid>.tmp files a killed builder left behind."""
    removed = 0
    for p in folder.rglob('*.tmp') if folder.is_dir() else []:
        if TMP.search(p.name) and p.is_file():
            p.unlink()
            removed += 1
    return removed


class Step:
    """One builder process: its output lines become progress events, its log goes to <staging>/logs."""

    def __init__(self, name, args, log, map_name=None):
        self.name, self.map, self.log = name, map_name, log
        self.started = time.time()
        self.done = self.total = self.shown = 0
        self.proc = subprocess.Popen([sys.executable, '-I', '-B', '-X', 'utf8', *map(str, args)], stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', bufsize=1)
        self.thread = threading.Thread(target=self.pump, daemon=True)
        self.thread.start()
        self.peak_rss = None

    def pump(self):
        with open(self.log, 'a', encoding='utf-8') as log:
            for line in self.proc.stdout:
                log.write(line)
                if not line.startswith('{'):
                    continue
                try:
                    event = json.loads(line)
                except ValueError:
                    continue
                if 'archive_warning' in event:
                    warn_archive(event['archive_warning'])
                elif 'tiles_total' in event:
                    self.total += event['tiles_total']
                elif 'tile' in event:
                    self.done += 1
                    if PROGRESS == 'json':
                        emit(event='tile', step=self.name, map=self.map, tile=event['tile'], done=self.done,
                             total=self.total, seconds=round(time.time() - self.started, 1))
                    elif self.total and self.done * 20 // self.total > self.shown:
                        self.shown = self.done * 20 // self.total
                        emit(event='progress', step=self.name, map=self.map, percent=self.shown * 5,
                             done=self.done, total=self.total)

    def finish(self, status=None, usage=None):
        """Collect the exit status: from os.wait4 (status, usage) where it exists, else from Popen."""
        if usage is not None:
            self.proc.returncode = os.waitstatus_to_exitcode(status)
            self.peak_rss = usage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)
        self.thread.join()
        self.seconds = round(time.time() - self.started, 1)

    def record(self):
        return {'step': self.name, 'map': self.map, 'exit': self.proc.returncode, 'seconds': self.seconds,
                'peak_rss_mb': round(self.peak_rss / 1e6) if self.peak_rss else None, 'log': self.log.name}


def run_parallel(specs, jobs, budget=float('inf')):
    """specs: [(name, args, log, map, memory bytes)], largest first; at most `jobs` processes at once and,
    past the first, only as many as fit into `budget` bytes. Returns the finished Steps."""
    pending, running, finished = list(specs), {}, []
    while pending or running:
        while (i := admit([s[4] for s in pending], sum(s.memory for s in running.values()), len(running),
                          jobs, budget)) is not None:
            name, args, log, map_name, memory = pending.pop(i)
            if memory > budget:
                emit(event='low_memory', step=name, map=map_name, needs_gb=round(memory / (1 << 30), 1),
                     available_gb=round(budget / (1 << 30), 1))
            emit(event='start', step=name, map=map_name)
            step = Step(name, args, log, map_name)
            step.memory = memory
            running[step.proc.pid] = step
        if hasattr(os, 'wait4'):
            pid, status, usage = os.wait4(-1, 0)   # our only children are these steps
            step = running.pop(pid, None)
            if step is None:
                continue
            step.finish(status, usage)
        else:
            while not (done := [s for s in running.values() if s.proc.poll() is not None]):
                time.sleep(0.5)
            step = running.pop(done[0].proc.pid)
            step.finish()
        emit(event='end', step=step.name, map=step.map, exit=step.proc.returncode, seconds=step.seconds,
             peak_rss_mb=step.record()['peak_rss_mb'])
        finished.append(step)
    return finished


def scene_failures(staging, steps):
    failures = {}
    for step in steps:
        for report in staging.glob(f'build-{step.map}-*-{step.proc.pid}.json'):
            data = json.loads(report.read_text())
            if data['failures']:
                failures[step.map] = data['failures']
    return failures


def post_problems(staging, steps, built):
    problems = []
    if 'lights' in steps:
        problems += [f'missing lights/{m}.fgl' for m in built if not (staging / 'lights' / f'{m}.fgl').is_file()]
    if 'celestial' in steps:
        problems += [f'missing celestial/{b}.fct' for b in ('sun', 'moon') if not (staging / 'celestial' / f'{b}.fct').is_file()]
    if 'fog' in steps:
        fog = read_json(staging / 'fog/manifest.json') or {}
        problems += [f'fog {m}: {v["tiles"]}/{v["expected"]} tiles' for m, v in fog.get('maps', {}).items()
                     if v['tiles'] != v['expected']]
        problems += [] if fog else ['missing fog/manifest.json']
        unreadable, roots = len(fog.get('unreadable_wmos', {})), fog.get('stats', {}).get('wmo_roots', 0)
        problems += [f'fog: {unreadable} unreadable WMOs of {roots}'] if unreadable * 20 > roots else []
    return problems


def tolerated(staging, scene_steps, steps, recorded=None):
    """What this run's builders tolerated: WMO groups whose oversized MOGP was clamped (scene reports and
    the fog manifest), the WMOs fog could not read (their buildings may have fog inside) and the archives
    the scene builders could not list or open. A run without scene steps keeps the recorded archive
    warnings: its chain is the one the installed scenes were built from."""
    clamped, archives = set(), {}
    for step in scene_steps:
        for report in staging.glob(f'build-{step.map}-*-{step.proc.pid}.json'):
            data = read_json(report) or {}
            clamped.update(data.get('clamped_wmo_groups', []))
            archives.update((w['archive'], w) for w in data.get('archive_warnings', []))
    if not scene_steps:
        archives.update((w['archive'], w) for w in ((recorded or {}).get('tolerated') or {}).get('archive_warnings', []))
    fog = (read_json(staging / 'fog' / 'manifest.json') or {}) if 'fog' in steps else {}
    clamped.update(fog.get('clamped_wmo_groups', []))
    return {'clamped_wmo_groups': sorted(clamped), 'fog_unreadable_wmos': fog.get('unreadable_wmos', {}),
            'archive_warnings': list(archives.values())}


def swap(staging, output, keep_previous):
    """Replace output with staging by renames; the previous cache goes to <output>.previous."""
    previous = output.with_name(output.name + '.previous')
    if previous.exists():
        retry(shutil.rmtree, previous)
    if output.exists():
        retry(output.rename, previous)
    retry(staging.rename, output)
    if previous.exists() and not keep_previous:
        retry(shutil.rmtree, previous)


def swap_steps(staging, output, steps):
    """Move each rebuilt staging/<step> over output/<step> (the old one via <step>.previous), then the
    manifest; the staging folder goes last. A rerun after a kill continues where this stopped."""
    for step in steps:
        new, old, previous = staging / step, output / step, output / f'{step}.previous'
        if not new.is_dir():
            continue   # already moved
        if previous.exists():
            retry(shutil.rmtree, previous)
        if old.exists():
            retry(old.rename, previous)
        retry(new.rename, old)
    (output / 'logs').mkdir(exist_ok=True)
    for log in (staging / 'logs').glob('*.log'):
        retry(os.replace, log, output / 'logs' / log.name)
    if (staging / 'install-manifest.json').is_file():
        retry(os.replace, staging / 'install-manifest.json', output / 'install-manifest.json')
    for step in steps:
        if (output / f'{step}.previous').exists():
            retry(shutil.rmtree, output / f'{step}.previous')
    retry(shutil.rmtree, staging)


def finish_staging(staging, output, digests, keep_previous):
    """A staging folder whose build finished (its manifest matches, no problems, no install-state.json)
    but whose swap was interrupted: complete the swap. A staging folder with neither is a leftover
    of a completed swap and goes. Returns True when a swap was completed."""
    if not staging.is_dir() or (staging / 'install-state.json').exists():
        return False
    manifest = read_json(staging / 'install-manifest.json')
    if manifest is None:
        retry(shutil.rmtree, staging)
        return False
    if manifest.get('problems') or manifest.get('digests') != digests:
        return False
    emit(event='finish_staging', staging=str(staging))
    if manifest.get('swap') == 'full':
        swap(staging, output, keep_previous)
    else:
        swap_steps(staging, output, manifest.get('swap') or [])
    return True


def memory_refusal(mode, total):
    """The preflight error of a build on `total` bytes of installed memory, or None. Only a full build
    (the scene step, up to 6.5 GB per map) is refused; lights, fog and celestial need about 0.5 GB."""
    if mode == 'full' and total < MIN_TOTAL_GB * (1 << 30):
        return (f'building the world cache needs a computer with at least 8 GB of memory '
                f'(this one has {total / (1 << 30):.1f} GB)')
    return None


def side_folders(output):
    """The folders a run may leave beside or inside <output>: what an uninstall removes with it."""
    return [output.with_name(output.name + '.staging'), output.with_name(output.name + '.previous'),
            *(output / f'{s}.previous' for s in POST)]


def drop_previous(output):
    """Delete <output>.previous and <output>/<step>.previous left by a swap killed before its cleanup;
    called only when <output> is complete (its manifest exists)."""
    for folder in side_folders(output)[1:]:
        if folder.exists():
            retry(shutil.rmtree, folder)
            emit(event='removed_previous_cache', path=str(folder))


def recover(output):
    """A run killed between the two renames of a swap leaves only <name>.previous: restore it (the
    manifest still describes it)."""
    for folder in [output, *(output / s for s in POST)]:
        previous = folder.with_name(folder.name + '.previous')
        if previous.exists() and not folder.exists():
            retry(previous.rename, folder)
            emit(event='recovered_previous_cache', path=str(folder))


def post_specs(steps, cache, staging, built, common):
    logs = staging / 'logs'
    specs = {'lights': [fp.tracked('world_lights_builder.py'), '--cache', cache, '--output', staging / 'lights',
                        '--maps', *built, *common],
             'fog': [fp.tracked('regional_fog_builder.py'), '--cache', cache, '--output', staging / 'fog', *common],
             'celestial': [fp.tracked('build_celestial_disc_assets.py'), '--output', staging / 'celestial', *common]}
    return [(s, specs[s], logs / f'{s}.log', None, POST_MEMORY_GB * (1 << 30)) for s in steps]


def main(argv=None):
    global PROGRESS
    warned.clear()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    client_archives.add_arguments(ap)
    ap.set_defaults(without='z')
    ap.add_argument('--output', type=Path, help='world cache folder (default: <client>/world-cache)')
    ap.add_argument('--maps', nargs='+', choices=MAPS, default=MAPS)
    ap.add_argument('--tiles', nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'), help='only these tiles (testing)')
    ap.add_argument('--jobs', type=int, help='parallel builder processes (default: by the memory budgets and CPUs)')
    ap.add_argument('--job-memory-gb', type=float, help='memory budget of every map process (default: per map)')
    ap.add_argument('--force', action='store_true', help='rebuild everything even when the digests match')
    ap.add_argument('--keep-previous', action='store_true', help='keep the replaced cache as <output>.previous')
    ap.add_argument('--progress', choices=['json', 'human'], default='json', help='progress format (default json)')
    args = ap.parse_args(argv)
    PROGRESS = args.progress

    started = time.time()
    try:
        client = (args.client or fp.client_root()).resolve()
        locale = client_archives.detect_locale(client, args.locale)
        output = (args.output or client / 'world-cache').resolve()
        inputs = gather_inputs(client, args.archives, locale, args.maps, args.tiles, args.without)
    except (fp.Missing, OSError, ValueError) as e:
        return final('failed', 2, stage='input', error=str(e))
    digests = step_digests(inputs)
    digest = _hash(digests)
    staging = output.with_name(output.name + '.staging')
    manifest_path = output / 'install-manifest.json'
    emit(event='fingerprint', sha256=digest, locale=locale, view=args.archives, without=args.without,
         archives=len(inputs['archives']))
    if finish_staging(staging, output, digests, args.keep_previous):
        return final('done', 0, output=str(output), fingerprint=digest, seconds=round(time.time() - started, 1),
                     completed_interrupted_swap=True)
    recover(output)
    recorded = read_json(manifest_path) if manifest_path.is_file() else None
    if recorded is not None and not args.keep_previous:
        drop_previous(output)
    mode, steps = plan(digests, recorded, output, args.force, args.without)
    if mode == 'up_to_date':
        return final('up_to_date', 0, output=str(output), fingerprint=digest)
    total = total_memory()
    refusal = memory_refusal(mode, total)
    if refusal:
        return final('failed', 2, stage='preflight', total_memory_gb=round(total / (1 << 30), 1), error=refusal)

    state = staging / 'install-state.json'
    if staging.exists():
        saved = read_json(state) or {}
        if not args.force and saved.get('digests') == digests and saved.get('mode') == mode and saved.get('steps') == steps:
            emit(event='resume', staging=str(staging), swept_tmp_files=sweep(staging))
        else:
            retry(shutil.rmtree, staging)
    (staging / 'logs').mkdir(parents=True, exist_ok=True)
    state.write_text(json.dumps({'digests': digests, 'mode': mode, 'steps': steps, 'inputs': inputs}, indent=2) + '\n')

    common = ['--client', client, '--archives', args.archives, '--locale', locale]
    common += ['--without', args.without] if args.without else []
    tiles = ['--tiles', *args.tiles] if args.tiles else []
    jobs = args.jobs or max(1, (os.cpu_count() or 2) - 1)
    budget = float('inf') if args.jobs else available_memory() * 0.8
    emit(event='plan', mode=mode, steps=steps, jobs=min(jobs, len(args.maps) if mode == 'full' else len(steps)), maps=args.maps,
         available_memory_gb=round(available_memory() / (1 << 30), 1), total_memory_gb=round(total / (1 << 30), 1))

    if mode == 'full':
        ran = run_parallel([('scene', [fp.tracked('world_scene_builder.py'), '--instanced', '--map', m, *tiles,
                                       '--output', staging, *common], staging / 'logs' / f'scene-{m}.log', m, need)
                            for m, need in map_jobs(args.maps, args.job_memory_gb)], jobs, budget)
        failed = [s.map for s in ran if s.proc.returncode]
        tile_failures = scene_failures(staging, ran)
        built = [m for m in args.maps if any((staging / m).glob('*.fg3'))]
        if failed or not built:
            return final('failed', 1, step='scene', maps=failed or args.maps, staging=str(staging),
                         problems=[f'scene {m} failed' for m in failed or args.maps])
        cache, previous_steps, scene_steps = staging, [], ran
    else:
        ran, tile_failures, scene_steps = [], recorded.get('tile_failures', {}), []
        built = recorded.get('maps_built') or recorded['inputs']['maps']
        cache, previous_steps = output, [s for s in recorded.get('steps', []) if s.get('step') not in (*steps, 'validate')]
    ran += run_parallel(post_specs(steps, cache, staging, built, common), jobs, budget)
    ran += run_parallel([('validate', [fp.tracked('validate_world_cache.py'), '--cache', cache, '--report',
                                       staging / 'validation.json', '--maps', *args.maps, *tiles, *common],
                          staging / 'logs' / 'validate.log', None, POST_MEMORY_GB * (1 << 30))], 1)
    problems = [f'{s.name} exit {s.proc.returncode}' for s in ran if s.proc.returncode]
    problems += post_problems(staging, steps, built)
    problems += [f'scene {m}: {len(f)} tile failures' for m, f in tile_failures.items()]
    tolerance = tolerated(staging, scene_steps, steps, recorded)
    if tolerance['clamped_wmo_groups'] or tolerance['fog_unreadable_wmos']:
        emit(event='tolerated', **{k: len(tolerance[k]) for k in ('clamped_wmo_groups', 'fog_unreadable_wmos')})
    for warning in tolerance['archive_warnings']:
        warn_archive(warning)
    manifest = {'format': FORMAT, 'fingerprint': digest, 'digests': digests, 'inputs': inputs, 'maps_built': built,
                'swap': 'full' if mode == 'full' else steps, 'built_unix': time.time(),
                'seconds': round(time.time() - started, 1), 'jobs': jobs,
                'steps': previous_steps + [s.record() for s in ran], 'tile_failures': tile_failures, 'tolerated': tolerance,
                'problems': problems}
    (staging / 'install-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if problems:
        return final('failed', 1, problems=problems, staging=str(staging))
    state.unlink()
    if mode == 'full':
        swap(staging, output, args.keep_previous)
    else:
        if (staging / 'validation.json').is_file():
            retry(os.replace, staging / 'validation.json', output / 'validation.json')
        swap_steps(staging, output, steps)
    return final('done', 0, output=str(output), fingerprint=digest, mode=mode, steps=steps, seconds=manifest['seconds'])


if __name__ == '__main__':
    sys.exit(main())
