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

Instance layer: --instances SPEC (group keys or numbers from renderer/instance_maps.py, "all" or "none") runs only
that layer, on top of the existing continent cache in --output. Each selected group is built by one scene process
into <output>.instmp (resumable), its lights go to lights/<map>.fgl (manifest lights/instances-manifest.json),
the maps are validated, and then the map folders, lights and new models move into <output> and
<output>/instances-manifest.json is written last. A group whose digest (archive chain, locale, group maps,
scene, lights and validation sources) is unchanged is kept; a deselected one is removed together with the
models only its tiles used. Changing the selection never touches the continents. A continent rebuild
(swap(), swap_steps('lights')) carries the instance content over (carry_instances()).
Groups also record separate scene/light digests: light-only changes reuse the map tiles and models;
validation-only changes run no builder. Read-only staging inputs use hard links, with a copy fallback.
A current legacy group is upgraded to these digests without rebuilding its content.

Progress is one JSON object per line (--progress json, the default) or readable text
(--progress human). The last stdout line is always JSON: {"event": "done"|"up_to_date"|"failed", ...}.
A failed build names the logs of its failed steps ('logs'); --progress human also prints their last lines.
Exit status: 0 done or up to date, 1 the build failed, 2 bad input (client, locale) or too little memory.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402
fp.use_source_modules()
import client_archives  # noqa: E402
import instance_maps  # noqa: E402

MAPS = ['Azeroth', 'Kalimdor', 'Expansion01', 'Northrend']
POST = ['lights', 'fog', 'celestial']
FORMAT = 'northlight-world-cache-install/1'
# The sources each step's output depends on. lights and fog also depend on the scene digests
# (they read the FGS3 tiles), which cover world_scene_builder.py, the module they import.
STEP_SOURCES = {
    'scene': ['world_scene_builder.py', 'wmo_lamp_emission.py', 'world_emitter_coverage.py',
              'point_transmission_builder.py', 'm2_visibility.py', 'mpq.py', 'client_archives.py'],
    'lights': ['world_lights_builder.py', 'light_attachments.py', 'wmo_lamp_emission.py', 'world_light_placements.py', 'outdoor_light_profiles.py',
               'world_emitter_coverage.py', 'world_emitter_index.py', 'build_equipment_emitters.py', 'emitter_pin_report.py',
                 'attached_emitter_sources.py', 'emitter_tracks.py', 'equipment_emitter_patch.py', 'point_transmission_builder.py', 'build_glue_emitters.py',
               'outdoor-light-profiles.json', 'wotlk-emitter-profiles.json'],
    'fog': ['regional_fog_builder.py', 'world_light_placements.py', 'forest_regions.inc'],
    'celestial': ['build_celestial_disc_assets.py', 'world_scene_builder.py', 'mpq.py', 'client_archives.py'],
}
TOP_LEVEL = ['client_archives.py', 'mpq.py']   # at the repository root, outside fp.tracked()'s namespace
SOURCES = sorted({n for names in STEP_SOURCES.values() for n in names} | {'validate_world_cache.py', 'instance_maps.py'})
# Earlier bytes of a source whose output equals the current one's: a cache built with them is not
# stale for that source. {source: {sha256: True when the cache must also be clamp_free()}}.
# - 0.3.183's scene and fog builders (unchanged since 0.3.166): 0.3.184 clamps an oversized top-level
#   MOGP of a WMO group file, so its output differs only where 0.3.183 hit such an overrun
#   (clamp_free() checks the cache's reports for one).
# - 0.3.193's world_scene_builder.py (since 0.3.184), mpq.py and client_archives.py (since 0.3.166):
#   0.3.194 builds where they stopped (an archive without a (listfile), an unopenable custom patch, an
#   unreadable file) and adds is_custom_patch() beside an unchanged chain(), so every cache they
#   finished is what 0.3.194 builds.
# - the instance-map change (world_scene_builder.py, world_lights_builder.py, world_light_placements.py as of
#   main before it): continent output is byte-identical (checked on real tiles, scene, lights and fog), so a cache
#   they finished is what today's sources build. world_scene_builder.py is in the scene and celestial digests,
#   world_light_placements.py in lights and fog.
# Revisit whenever one of these files changes again.
SOURCE_EQUIVALENTS = {
    'world_scene_builder.py': {'a54e08fe2774cd79702c04cf6e3dfd17854a12da4b6305ca38a4ade0545f0484': True,
                               '9c7c13d16fab67e4d65d81c8faf88fd20a129f6b02e1faa6734501885434ad4a': False,
                               'c612cf39a6166cd33d815a9fbcca8b65ae545058d02b156b57edaf39b7d9b551': False},
    'world_lights_builder.py': {'5ba2844340b271a3d7671b98d98a899379e92493f0cf82a8062d9b46452850a5': False},
    'world_light_placements.py': {'651c5766dde74f945ebdff268973ff1c87e35c203fccca484bce4a75886adad8': False},
    'regional_fog_builder.py': {'30a2af943dd0f758c1bd6067b9d58bd6b6e2f64b85af2394f2f9e6fc01daa7e3': True},
    'mpq.py': {'0b949fc438e1b71433242cd580848c7cf14281696e409903710f4191ac4b4d7a': False},
    'client_archives.py': {'efebb6a31dd00ebb73fe6fe40e1e1d2ecf4e749a6dd5d96457d8842ed1798e6c': False},
}
PGOM_OVERRUN = "Chunk exceeds file: b'PGOM'"   # 0.3.183's error for an oversized top-level MOGP
# Memory budget of one world_scene_builder process per continent (the builder keeps every mesh and
# texture of its map). Measured peak RSS, stock 3.3.5a, 2026-09-26: Northrend 4.8 GB, Azeroth 3.2,
# Expansion01 3.0, Kalimdor 2.3; the budgets leave room for HD textures.
MAP_MEMORY_GB = {'Northrend': 6.5, 'Azeroth': 4.5, 'Expansion01': 4.5, 'Kalimdor': 3.5}
INSTANCE_MEMORY_GB = 1.5     # one scene process per instance group (measured peak 0.3-1.3 GB)
POST_MEMORY_GB = 0.5         # lights 0.3 GB, fog and celestial 0.15
MIN_TOTAL_GB = 7.0           # installed RAM; an "8 GB" PC may report a little less than 8 GiB
LOCK_RETRIES = (0.2, 0.4, 0.8, 1.6, 3.0) if os.name == 'nt' else ()
TMP = re.compile(r'\.\d+\.tmp$')
ART_LETTER = re.compile(r'patch(?:-[a-z]{4})?-([0-9a-z])\.mpq$')
INSTANCE_FORMAT = 'northlight-world-cache-instances/1'
INSTANCE_MANIFEST = 'instances-manifest.json'   # in <output>, and the lights builder's own in <output>/lights
INSTANCE_STAGING = '.instmp'
INSTANCE_READY = 'instances-ready.json'
NOT_MAPS = {'models', 'lights', 'fog', 'celestial', 'logs'}
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
                'wrong there. Later installs keep this cache until the archive changes; to try it again, delete '
                'the world-cache folder and run the installer again.')
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
        if f['mode'] == 'instances':
            return None if not f['groups'] else (f"Building the instance world cache: {', '.join(f['groups'])}, up to {f['jobs']} groups at a time. "
                    'This takes a few minutes per group.')
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
    if event == 'instances_removed':
        return f"Instance world cache: removed {', '.join(f['groups'])}"
    if event == 'instances_reuse':
        return f"Instance world cache: keeping geometry for {', '.join(f['groups'])}"
    if event == 'instances_carry_failed':
        return f"Warning: the instance world cache could not be carried over ({f['error']}); it is built again next time."
    if event == 'low_memory':
        return (f"Warning: {f['available_gb']} GB of free memory, {what} needs about {f['needs_gb']} GB; "
                'the build may be slow. Close other programs if you can.')
    return None


LOG_TAIL_LINES = 20
MAX_FAILED_LOGS = 3   # a console screen: more logs would scroll the reason away


def log_tail(path, count=LOG_TAIL_LINES):
    """The last `count` lines of a builder log, without its per-tile progress records (a crash's traceback is last)."""
    try:
        lines = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError as e:
        return [f'(cannot read the log: {e})']
    return [line for line in lines if line.strip() and not line.startswith(('{"tile"', '{"tiles_total"'))][-count:]


def failed_logs(steps, tile_failures=None):
    """The logs to show for a failed build (at most MAX_FAILED_LOGS): those of the steps that exited non-zero; when
    every step exited 0 (missing output, tile failures), the scene logs of the maps with tile failures and the
    validation log, else every step's."""
    failed = [s for s in steps if s.proc.returncode]
    if not failed:
        failed = [s for s in steps if (s.map and s.map in (tile_failures or {})) or s.name == 'validate'] or steps
    return [str(s.log) for s in failed][:MAX_FAILED_LOGS]


def final(event, code, **fields):
    """The last stdout line, JSON in both progress modes; returns the exit status. A failure's 'logs' are named,
    and in human mode their last lines are printed too, so the reason is on the console, not only in the log."""
    if PROGRESS == 'human':
        layer = 'Instance world cache' if fields.get('layer') == 'instances' else 'World cache'
        text = {'done': f"{layer} ready in {fields.get('seconds', 0) / 60:.1f} min",
                'up_to_date': f'{layer} is up to date',
                'failed': f"{layer} build FAILED: {fields.get('error') or '; '.join(fields.get('problems', []))}"}[event]
        print(text, flush=True)
        for log in fields.get('logs') or []:
            print(f'--- last lines of {log} ---', flush=True)
            for line in log_tail(log):
                print('  | ' + line, flush=True)   # '|': the installer shows them, never as a JSON report
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
        old = manifest['digests']
        inputs = manifest.get('inputs')
        if newly_excluded_art(inputs, without) and step_digests(inputs) == old:
            # A layer falsely counted as foreign was ours: its mesh geometry did not change.
            # Keep post steps stale: our old native-light/sky edits can affect their inputs.
            normalized = step_digests(inputs, without)
            return {k: normalized[k] if k.startswith('scene:') else v for k, v in old.items()}
        return old
    try:
        return step_digests(manifest['inputs'], without)
    except (KeyError, TypeError, AttributeError):
        return {}


def newly_excluded_art(inputs, without):
    return isinstance(inputs, dict) and set(inputs.get('without', '')) <= set(without) and \
        set(without) - set(inputs.get('without', '')) == {'z'}


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
        carry_instances_safely(output, staging)
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
        if step == 'lights':
            carry_instance_lights_safely(old, new)
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


def instance_staging(output):
    return output.with_name(output.name + INSTANCE_STAGING)


def side_folders(output):
    """The folders a run may leave beside or inside <output>: what an uninstall removes with it."""
    return [output.with_name(output.name + '.staging'), output.with_name(output.name + '.previous'),
            *(output / f'{s}.previous' for s in POST), instance_staging(output)]


def drop_previous(output):
    """Delete <output>.previous and <output>/<step>.previous left by a swap killed before its cleanup;
    called only when <output> is complete (its manifest exists). The instance staging is resumable: it stays."""
    for folder in side_folders(output)[1:-1]:
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


# ---- instance layer: dungeon and raid maps on top of the continent cache ----

def instance_digests(inputs, keys=None):
    """{group key: digest}: the archive chain (our art letters left out), the locale, the group and its maps, and the
    sources of the scene and lights builders, the validator and instance_maps. Independent of the continent digests."""
    chain = [[a['archive'].lower(), a['bytes'], a['mtime_ns']] for a in inputs['archives']
             if not is_art_layer(a['archive'], inputs.get('without', ''))]
    names = sorted({*STEP_SOURCES['scene'], *STEP_SOURCES['lights'], 'validate_world_cache.py', 'instance_maps.py'})
    own = {n: inputs['sources'].get(n) for n in names}
    return {k: _hash('instances', chain, inputs['locale'], k, instance_maps.maps_for([k]), own)
            for k in (instance_maps.KEYS if keys is None else keys)}


def instance_step_digests(inputs):
    """Track geometry separately so a light/validation update can reuse it."""
    chain = [[a['archive'].lower(), a['bytes'], a['mtime_ns']] for a in inputs['archives']
             if not is_art_layer(a['archive'], inputs.get('without', ''))]
    result = {}
    for key in instance_maps.KEYS:
        scene = _hash('instance-scene', chain, inputs['locale'], instance_maps.maps_for([key]),
                      {n: inputs['sources'].get(n) for n in STEP_SOURCES['scene']})
        result[key] = {'scene': scene, 'lights': _hash('instance-lights', scene,
                       {n: inputs['sources'].get(n) for n in STEP_SOURCES['lights']})}
    return result


def rebase_instance_scenes(output, without):
    """Keep geometry when a previously included art layer is now proven ours.
    The old combined digest must prove that the group used the recorded continent inputs.
    Lights still update, since old native-light edits may have affected their output.
    """
    recorded = read_json(output / INSTANCE_MANIFEST)
    inputs = (read_json(output / 'install-manifest.json') or {}).get('inputs')
    if not recorded or not newly_excluded_art(inputs, without):
        return recorded
    before = instance_digests(inputs)
    prior_steps = instance_step_digests(inputs)
    normalized = instance_step_digests(dict(inputs, without=without))
    for key, group in recorded.get('groups', {}).items():
        if group.get('digest') == before.get(key):
            group['digests'] = dict(prior_steps[key], scene=normalized[key]['scene'])
    return recorded


def stage_instance_files(output, staging, maps, lights=False):
    """Read-only staging inputs: hard links on the same drive, copies if unsupported."""
    def link(source, target):
        if target.exists():
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(source, target)
        except OSError:
            shutil.copy2(source, target)
    for name in maps:
        source_root = output / 'lights' / 'point-transmission' if lights else output
        for source in (source_root / name).glob('*.fg3'):
            link(source, staging / source.relative_to(output))
        for key in tile_keys(source_root / name):
            source = source_root / 'models' / f'{key}.fgs'
            link(source, staging / source.relative_to(output))
        if lights:
            for extension in ('fgl', 'fga'):
                source = output / 'lights' / f'{name}.{extension}'
                if source.is_file():
                    link(source, staging / source.relative_to(output))


def tile_keys(folder):
    """The model keys (first 32 bytes of each record, hex) the FGS3 tiles of a folder reference."""
    keys = set()
    for path in folder.glob('*.fg3'):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if data[:4] != b'FGS3' or len(data) < 12:
            continue
        for i in range(min(struct.unpack_from('<I', data, 8)[0], (len(data) - 12) // 116)):
            keys.add(data[12 + 116 * i:12 + 116 * i + 32].hex())
    return keys


def all_tile_keys(root):
    keys = set()
    for folder in root.iterdir() if root.is_dir() else []:
        if folder.is_dir() and folder.name not in NOT_MAPS:
            keys |= tile_keys(folder)
    return keys


def present_instance_maps(output, maps):
    """The maps of `maps` that have a folder or a .fgl in `output` (also leftovers no manifest records)."""
    return [m for m in maps if (output / m).exists() or (output / 'lights' / f'{m}.fgl').exists()]


def group_present(output, record):
    maps = record.get('maps_built') or []
    return bool(maps) and all(any((output / m).glob('*.fg3')) and
                             (output / 'lights' / f'{m}.fgl').is_file() for m in maps)


def instance_plan(digests, selected, recorded, output, force=False):
    """(build, keep, remove) group keys. Selected groups with a changed digest or missing folders are built; deselected
    ones that are recorded or have leftovers in `output` are removed; the rest is kept."""
    groups = (recorded or {}).get('groups') or {}
    build, keep, remove = [], [], []
    for key in instance_maps.KEYS:
        record = groups.get(key)
        if key in selected:
            ok = record and record.get('digest') == digests[key] and group_present(output, record)
            (keep if ok and not force else build).append(key)
        elif record or present_instance_maps(output, instance_maps.maps_for([key])):
            remove.append(key)
    return build, keep, remove


def carry_instances(old_root, new_root):
    """Move the instance layer of old_root into new_root (a continent swap replaces the folder): the instance map
    folders, their lights/<map>.fgl and lights/instances-manifest.json, the models/*.fgs their tiles reference that
    new_root lacks, and instances-manifest.json last. Returns the maps moved."""
    moved = []
    for name in instance_maps.ALL_MAPS:
        source, target = old_root / name, new_root / name
        if source.is_dir() and not target.exists() and any(source.glob('*.fg3')):
            new_root.mkdir(parents=True, exist_ok=True)
            retry(source.rename, target)
            moved.append(name)
    keys = set()
    for name in moved:
        keys |= tile_keys(new_root / name)
    for key in sorted(keys):
        source, target = old_root / 'models' / f'{key}.fgs', new_root / 'models' / f'{key}.fgs'
        if source.is_file() and not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            retry(os.replace, source, target)
    carry_instance_lights(old_root / 'lights', new_root / 'lights')
    if (old_root / INSTANCE_MANIFEST).is_file():
        retry(os.replace, old_root / INSTANCE_MANIFEST, new_root / INSTANCE_MANIFEST)
    return moved


def carry_instance_lights(old, new):
    """Move the instance .fgl files and their lights manifest from one lights/ folder to another."""
    for name in [f'{m}.{extension}' for m in instance_maps.ALL_MAPS for extension in ('fgl','fga')] + [INSTANCE_MANIFEST]:
        if (old / name).is_file() and not (new / name).exists():
            new.mkdir(parents=True, exist_ok=True)
            retry(os.replace, old / name, new / name)
    merge_point_transmission(old,new,instance_maps.ALL_MAPS)


def merge_point_transmission(old, new, maps, replace=False):
    """Keep auxiliary geometry with its map's light transaction and source models."""
    maps=tuple(maps)
    if any(name not in instance_maps.ALL_MAPS for name in maps):raise ValueError('Unknown transmission instance map')
    old_index,new_index=old/'world-emitters-v1.json',new/'world-emitters-v1.json'
    previous=json.loads(old_index.read_text()) if old_index.is_file() else None
    current=json.loads(new_index.read_text()) if new_index.is_file() else None
    for document in (previous,current):
        if document is not None and (not isinstance(document,dict) or type(document.get('version')) is not int or document['version']!=1 or document.get('owner')!='northlight-local-direct' or not isinstance(document.get('models'),dict)):
            raise ValueError('Transmission emitter index owner/version/models')
    source=old/'point-transmission';target=new/'point-transmission'
    needed=set()
    for name in maps:
        src,dst=source/name,target/name
        if replace and dst.exists():
            if not dst.resolve().is_relative_to(target.resolve()):raise ValueError('Transmission path escapes cache')
            retry(shutil.rmtree,dst)
        if src.is_dir() and not dst.exists():
            dst.parent.mkdir(parents=True,exist_ok=True);retry(src.rename,dst)
        if dst.is_dir():needed.update(tile_keys(dst))
    for key in needed:
        src,dst=source/'models'/(key+'.fgs'),target/'models'/(key+'.fgs')
        if src.is_file() and (replace or not dst.exists()):
            dst.parent.mkdir(parents=True,exist_ok=True);retry(os.replace,src,dst)
    if previous is not None:
        if current is None:current=previous
        elif replace:current['models'].update(previous['models'])
        else:
            for path,value in previous['models'].items():current['models'].setdefault(path,value)
        new_index.parent.mkdir(parents=True,exist_ok=True)
        new_index.write_text(json.dumps(current,sort_keys=True,separators=(',',':'),allow_nan=False),encoding='utf-8')
        digest=hashlib.sha256(new_index.read_bytes()).hexdigest()
        for name in ('manifest.json',INSTANCE_MANIFEST):
            path=new/name
            if path.is_file():
                manifest=json.loads(path.read_text())
                if 'world_emitter_index' in manifest:
                    manifest['world_emitter_index']['sha256']=digest
                    path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def drop_instance_record(root):
    """A failed carry-over: forget the layer's manifest so the next instance run builds what is missing."""
    for path in (root / INSTANCE_MANIFEST, root / 'lights' / INSTANCE_MANIFEST):
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass


def carry_instances_safely(old_root, new_root):
    """carry_instances that never fails the continent install: content that did not move is built again next time."""
    try:
        carry_instances(old_root, new_root)
    except (OSError, ValueError) as e:
        emit(event='instances_carry_failed', error=str(e))
        drop_instance_record(old_root)
        drop_instance_record(new_root)


def carry_instance_lights_safely(old, new):
    """The lights part of the above, for a swapped lights/ step. old may be missing (its replacement was interrupted)."""
    try:
        carry_instance_lights(old if old.is_dir() else old.with_name('lights.previous'), new)
    except (OSError, ValueError) as e:
        emit(event='instances_carry_failed', error=str(e))
        drop_instance_record(old.parent)


def instance_reports(staging, maps):
    """{map: report} of the newest build-<map>-<time>-<pid>.json of every map."""
    found = {}
    for m in maps:
        pattern = re.compile(r'build-' + re.escape(m) + r'-(\d+)-\d+\.json$')
        reports = sorted((int(g.group(1)), p) for p in staging.glob('build-*.json') if (g := pattern.fullmatch(p.name)))
        if reports:
            found[m] = read_json(reports[-1][1]) or {}
    return found


def instance_group_result(staging, key):
    """What a group's scene process built: (maps with tiles, tile failures per map, empty-by-design tile counts per
    map, clamped WMO groups, archive warnings)."""
    maps = instance_maps.maps_for([key])
    reports = instance_reports(staging, maps)
    built = [m for m in maps if any((staging / m).glob('*.fg3'))]
    failures = {m: r['failures'] for m, r in reports.items() if r.get('failures')}
    empty = {m: len(r['empty_source_tiles']) for m, r in reports.items() if r.get('empty_source_tiles')}
    clamped = sorted({g for r in reports.values() for g in r.get('clamped_wmo_groups', [])})
    warnings = {w['archive']: w for r in reports.values() for w in r.get('archive_warnings', [])}
    return built, failures, empty, clamped, list(warnings.values())


def instance_failed_logs(steps, bad):
    """The logs to show for a failed instance build: steps that exited non-zero, else those named in `bad`."""
    failed = [s for s in steps if s.proc.returncode] or [s for s in steps if s.name in bad] or steps
    return [str(s.log) for s in failed][:MAX_FAILED_LOGS]


def merge_lights_manifest(output, staging, affected):
    """lights/instances-manifest.json lists the maps of every group in <output>: the new run's entries replace the
    old ones of the maps it rebuilt, a removed map's entry goes."""
    old, new = output / 'lights' / INSTANCE_MANIFEST, staging / 'lights' / INSTANCE_MANIFEST
    fresh, base = read_json(new), read_json(old)
    kept = [e for e in (base or {}).get('maps', []) if e.get('map') not in affected]
    merged = dict(fresh or base or {}, maps=kept + list((fresh or {}).get('maps', [])))
    index=output/'lights'/'world-emitters-v1.json'
    if index.is_file() and 'world_emitter_index' in merged:
        merged['world_emitter_index']['sha256']=hashlib.sha256(index.read_bytes()).hexdigest()
    if merged['maps']:
        old.parent.mkdir(parents=True, exist_ok=True)
        tmp = old.with_name(old.name + f'.{os.getpid()}.tmp')
        tmp.write_text(json.dumps(merged, indent=2) + '\n')
        retry(os.replace, tmp, old)
    else:
        old.unlink(missing_ok=True)
    new.unlink(missing_ok=True)


def apply_instances(staging, output):
    """Move a finished staging folder's instance layer into output; every step can be repeated after a kill.
    staging/instances-ready.json holds what to move, drop and prune and the manifest to write last.
    Returns the number of models pruned."""
    ready = read_json(staging / INSTANCE_READY)
    models, lights = output / 'models', output / 'lights'
    models.mkdir(parents=True, exist_ok=True)
    lights.mkdir(parents=True, exist_ok=True)
    replace = set(ready.get('replace', ready['prune']))   # models only the replaced or removed tiles used
    for path in (staging / 'models').glob('*.fgs'):
        # A model file is named by a hash of the model's name (sha256 of 'FGS3-v1:' + name), not of its content. One this
        # layer's own old tiles used, and no tile outside the rebuilt groups uses, is replaced by the fresh build (the
        # sources may have changed its content); any other existing file (shared with a continent or a kept group) stays.
        if path.stem in replace or not (models / path.name).exists():
            retry(os.replace, path, models / path.name)
    for m in ready['drop_maps']:
        if (output / m).exists():
            retry(shutil.rmtree, output / m)
        (lights / f'{m}.fgl').unlink(missing_ok=True)
        (lights / f'{m}.fga').unlink(missing_ok=True)
    for m in ready['built_maps']:
        if m not in ready.get('reused_maps', []) and (staging / m).is_dir():
            if (output / m).exists():
                retry(shutil.rmtree, output / m)
            retry((staging / m).rename, output / m)
        if m not in ready.get('lights_maps', ready['built_maps']):
            continue
        if (staging / 'lights' / f'{m}.fgl').is_file():
            retry(os.replace, staging / 'lights' / f'{m}.fgl', lights / f'{m}.fgl')
        if (staging / 'lights' / f'{m}.fga').is_file():
            retry(os.replace, staging / 'lights' / f'{m}.fga', lights / f'{m}.fga')
    affected_lights = [*ready['drop_maps'], *ready.get('lights_maps', ready['built_maps'])]
    merge_point_transmission(staging/'lights',lights,affected_lights,replace=True)
    point_root=lights/'point-transmission'
    point_keys=all_tile_keys(point_root)
    for path in (point_root/'models').glob('*.fgs'):
        if path.stem not in point_keys:path.unlink()
    merge_lights_manifest(output, staging, set(affected_lights))
    if (staging / 'validation.json').is_file():   # the validator's report, beside the continents' validation.json
        retry(os.replace, staging / 'validation.json', output / 'instances-validation.json')
    (output / 'logs').mkdir(exist_ok=True)
    for log in (staging / 'logs').glob('*.log'):
        retry(os.replace, log, output / 'logs' / log.name)
    stale = set(ready['prune']) - all_tile_keys(output)   # models only the replaced or removed tiles used
    for key in stale:
        (models / f'{key}.fgs').unlink(missing_ok=True)
    manifest = output / INSTANCE_MANIFEST
    if ready['manifest']:
        tmp = manifest.with_name(manifest.name + f'.{os.getpid()}.tmp')
        tmp.write_text(json.dumps(ready['manifest'], indent=2) + '\n')
        retry(os.replace, tmp, manifest)
    else:
        manifest.unlink(missing_ok=True)
    retry(shutil.rmtree, staging)
    return len(stale)


def finish_instances(staging, output, digests):
    """A staging folder whose build was validated (instances-ready.json) but whose move was interrupted: complete it
    when its groups' digests are still today's, else it goes. One that did not get that far is left to the resume
    logic. Returns True when a move was completed."""
    ready = read_json(staging / INSTANCE_READY) if staging.is_dir() else None
    if ready is None:
        return False
    if any(digests.get(k) != d for k, d in ready['digests'].items()):
        retry(shutil.rmtree, staging)
        return False
    emit(event='finish_staging', staging=str(staging))
    apply_instances(staging, output)
    return True


def run_instances(args, client, locale, output, inputs, started):
    """main() with --instances: the instance layer only. Exit status as main()."""
    try:
        selected = instance_maps.resolve(args.instances)
    except ValueError as e:
        return final('failed', 2, stage='input', layer='instances', error=str(e))
    digests = instance_digests(inputs)
    digest = _hash(digests)
    staging = instance_staging(output)
    base = {'layer': 'instances', 'output': str(output)}
    recorded = rebase_instance_scenes(output, args.without)
    if args.instances_plan:   # a dry run for the installer's disk check
        build, keep, remove = instance_plan(digests, selected, recorded, output, args.force)
        print(json.dumps({'event': 'instances_plan', 'build': build, 'keep': keep, 'remove': remove}), flush=True)
        return 0
    has_cache = (output / 'install-manifest.json').is_file() or (output / 'northlight-cache.json').is_file()
    if selected and not has_cache:
        return final('failed', 2, stage='preflight', layer='instances',
                     error=f'{output} has no continent world cache; the instance maps are built on top of it. '
                           'Build the continents first (run without --instances).')
    emit(event='fingerprint', sha256=digest, locale=locale, view=args.archives, without=args.without,
         archives=len(inputs['archives']))
    if finish_instances(staging, output, digests):
        return final('done', 0, **base, fingerprint=digest, seconds=round(time.time() - started, 1),
                     completed_interrupted_swap=True)
    build, keep, remove = instance_plan(digests, selected, recorded, output, args.force)
    step_digests = instance_step_digests(inputs)
    # Upgrade a current legacy record without rebuilding its content. Dry runs never write.
    if recorded and any(recorded['groups'][k].get('digests') != step_digests[k] for k in keep):
        for k in keep:
            recorded['groups'][k]['digests'] = step_digests[k]
        temp = output / (INSTANCE_MANIFEST + '.tmp')
        temp.write_text(json.dumps(recorded, indent=2) + '\n', encoding='utf-8')
        retry(os.replace, temp, output / INSTANCE_MANIFEST)
    if not build and not remove:
        if staging.is_dir() and not (staging / 'install-state.json').is_file():
            retry(shutil.rmtree, staging)   # a leftover of a completed move
        return final('up_to_date', 0, **base, fingerprint=digest, kept=keep)
    old_groups = dict((recorded or {}).get('groups') or {})
    reuse = [k for k in build if not args.force and old_groups.get(k, {}).get('maps_built') and
             (old_groups.get(k, {}).get('digests') or {}).get('scene') == step_digests[k]['scene'] and
             all(any((output / m).glob('*.fg3')) and
                 all((output / 'models' / f'{key}.fgs').is_file() for key in tile_keys(output / m))
                 for m in old_groups[k]['maps_built'])]
    scene_build = [k for k in build if k not in reuse]
    light_build = [k for k in build if k not in reuse or
                   (old_groups[k].get('digests') or {}).get('lights') != step_digests[k]['lights'] or
                   not group_present(output, old_groups[k])]
    state = staging / 'install-state.json'
    wanted = {k: digests[k] for k in build}
    if staging.exists():
        if not args.force and (read_json(state) or {}).get('digests') == wanted:
            emit(event='resume', staging=str(staging), swept_tmp_files=sweep(staging))
        else:
            retry(shutil.rmtree, staging)
    (staging / 'logs').mkdir(parents=True, exist_ok=True)
    state.write_text(json.dumps({'digests': wanted, 'selected': selected}, indent=2) + '\n')
    common = ['--client', client, '--archives', args.archives, '--locale', locale]
    common += ['--without', args.without] if args.without else []
    jobs = args.jobs or max(1, (os.cpu_count() or 2) - 1)
    budget = float('inf') if args.jobs else available_memory() * 0.8
    emit(event='plan', mode='instances', steps=['scene', 'lights', 'validate'] if build else [], groups=build, remove=remove,
         keep=keep, jobs=min(jobs, len(build) or 1), maps=instance_maps.maps_for(build),
         available_memory_gb=round(available_memory() / (1 << 30), 1), total_memory_gb=round(total_memory() / (1 << 30), 1))

    ran, results = [], {}
    if build:
        if reuse:
            emit(event='instances_reuse', groups=reuse)
        reuse_maps = [m for k in reuse for m in old_groups[k]['maps_built']]
        by_size = sorted(scene_build, key=lambda k: -next(g['est_gb'] for g in instance_maps.GROUPS if g['key'] == k))
        need = (args.job_memory_gb or INSTANCE_MEMORY_GB) * (1 << 30)
        ran = run_parallel([(f'instances {k}', [fp.tracked('world_scene_builder.py'), '--instanced', '--map',
                                                *instance_maps.maps_for([k]), '--output', staging, *common],
                             staging / 'logs' / f'instances-scene-{k}.log', None, need) for k in by_size], jobs, budget)
        results = {k: instance_group_result(staging, k) for k in scene_build}
        for k in reuse:
            r = old_groups[k]
            results[k] = (r['maps_built'], r.get('tile_failures', {}), r.get('empty_source_tiles', {}),
                          r.get('clamped_wmo_groups', 0), r.get('archive_warnings', []))
        problems = [f'{s.name} exit {s.proc.returncode}' for s in ran if s.proc.returncode]
        for k, (built, failures, _, _, _) in results.items():
            problems += [f'instances {k}: no tiles built'] if not built else []
            problems += [f'instances {k} {m}: {len(f)} tile failures' for m, f in failures.items()]
        if problems:
            return final('failed', 1, **base, problems=problems, staging=str(staging),
                         logs=instance_failed_logs(ran, [f'instances {k}' for k in build]))
        stage_instance_files(output, staging, reuse_maps)
        stage_instance_files(output, staging, [m for k in reuse if k not in light_build for m in old_groups[k]['maps_built']], lights=True)
        built_maps = [m for k in build for m in results[k][0]]
        light_maps = [m for k in light_build for m in results[k][0]]
        if light_maps:
            ran += run_parallel([('instances lights', [fp.tracked('world_lights_builder.py'), '--cache', staging, '--output',
                                                       staging / 'lights', '--maps', *light_maps, '--manifest', INSTANCE_MANIFEST, *common],
                                  staging / 'logs' / 'instances-lights.log', None, POST_MEMORY_GB * (1 << 30))], 1)
        ran += run_parallel([('instances validate', [fp.tracked('validate_world_cache.py'), '--cache', staging, '--report',
                                                     staging / 'validation.json', '--maps', *built_maps, *common],
                              staging / 'logs' / 'instances-validate.log', None, POST_MEMORY_GB * (1 << 30))], 1)
        problems = [f'{s.name} exit {s.proc.returncode}' for s in ran if s.proc.returncode]
        problems += [f'missing lights/{m}.fgl' for m in built_maps if not (staging / 'lights' / f'{m}.fgl').is_file()]
        if problems:
            return final('failed', 1, **base, problems=problems, staging=str(staging), logs=instance_failed_logs(ran, []))
        for k in build:
            for warning in results[k][4]:
                warn_archive(warning)
        clamped = {g for k, r in results.items() if k not in reuse for g in r[3]}
        if clamped:
            emit(event='tolerated', clamped_wmo_groups=len(clamped), fog_unreadable_wmos=0)
    # the new record: kept groups as they were, built ones from this run, removed ones gone
    groups = {k: v for k, v in old_groups.items() if k in keep}
    for k, (built, failures, empty, clamped, _) in results.items():
        groups[k] = {'digest': digests[k], 'maps_built': built, 'tile_failures': failures, 'empty_source_tiles': empty,
                     'clamped_wmo_groups': clamped if isinstance(clamped, int) else len(clamped),
                     'archive_warnings': results[k][4], 'digests': step_digests[k]}
    built_maps = [m for k in build for m in results[k][0]]
    drop = [m for k in [*build, *remove]
            for m in dict.fromkeys([*((old_groups.get(k) or {}).get('maps_built') or []),
                                    *present_instance_maps(output, instance_maps.maps_for([k]))]) if m not in built_maps]
    manifest = {'format': INSTANCE_FORMAT, 'groups': groups,
                'lights': [f'{m}.fgl' for g in groups.values() for m in g['maps_built']],
                'locale': locale, 'built_unix': time.time(), 'seconds': round(time.time() - started, 1),
                'steps': [s.record() for s in ran]} if groups else None
    prune = set()   # models the replaced or removed tiles used; the move deletes those no tile references any more
    reused_maps = [m for k in reuse for m in results[k][0]]
    for m in [*drop, *(m for m in built_maps if m not in reused_maps and (output / m).is_dir())]:
        prune |= tile_keys(output / m)
    outside = set()   # models a tile of the continents or of a kept group uses: shared, never replaced
    for folder in output.iterdir() if output.is_dir() else []:
        if folder.is_dir() and folder.name not in NOT_MAPS and \
                (folder.name in reused_maps or folder.name not in (*drop, *built_maps)):
            outside |= tile_keys(folder)
    (staging / INSTANCE_READY).write_text(json.dumps({'digests': wanted, 'built_maps': built_maps, 'drop_maps': drop,
                                                       'reused_maps': reused_maps,
                                                       'lights_maps': [m for k in light_build for m in results[k][0]],
                                                       'prune': sorted(prune), 'replace': sorted(prune - outside),
                                                       'manifest': manifest}) + '\n')
    state.unlink(missing_ok=True)
    pruned = apply_instances(staging, output)
    if remove:
        emit(event='instances_removed', groups=remove)
    return final('done', 0, **base, fingerprint=digest, built=build, kept=keep, removed=remove, pruned_models=pruned,
                 seconds=round(time.time() - started, 1))


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
    ap.add_argument('--instances', metavar='SPEC', help='only the instance layer on top of the cache in --output: group keys or '
                    'numbers (see renderer/instance_maps.py), "all" or "none" (removes it)')
    ap.add_argument('--instances-plan', action='store_true',
                    help='with --instances: print {"build": [...], "keep": [...], "remove": [...]} and change nothing')
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
    if args.instances is not None:
        return run_instances(args, client, locale, output, inputs, started)
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

    # Save the proof before the continent manifest is replaced by the post-step update.
    instance_record = rebase_instance_scenes(output, args.without)
    if instance_record != read_json(output / INSTANCE_MANIFEST):
        temp = output / (INSTANCE_MANIFEST + '.tmp')
        temp.write_text(json.dumps(instance_record, indent=2) + '\n', encoding='utf-8')
        retry(os.replace, temp, output / INSTANCE_MANIFEST)
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
                         problems=[f'scene {m} failed' for m in failed or args.maps], logs=failed_logs(ran))
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
        return final('failed', 1, problems=problems, staging=str(staging), logs=failed_logs(ran, tile_failures))
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
