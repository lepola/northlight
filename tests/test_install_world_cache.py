#!/usr/bin/env python3
# northlight-test:
"""scripts/install_world_cache.py without a client (stat-only fake archives):
- per-step digests: our art layer (patch-z, patch-<loc>-z) and name case leave every digest
  unchanged; a new archive changes them all; a lights-only, fog-only or celestial-only source change
  changes only that step; the plan rebuilds only the stale post step;
- a manifest from before the digests is migrated (up to date when its chain and sources match, also
  when it recorded patch-z);
- a run killed after its build finished completes the swap on rerun without building (whole cache
  and single step folders), and recover() restores <name>.previous;
- memory: largest map first, per-map budgets bound the running jobs, below 7.0 GiB of installed
  memory a full build (never a lights/fog/celestial update) is refused with exit 2; bad input
  exits 2; the last stdout line is JSON;
- --without '' (a foreign z) makes patch-z client content, end to end; a complete cache loses a
  leftover <output>.previous and <output>/<step>.previous; <output>.extract is never touched;
- stale *.<pid>.tmp files are swept; Windows lock retries;
- a failed build names its failed steps' logs and, in human mode, prints their last lines;
- a cache recorded with main's scene, lights and placement builders (SOURCE_EQUIVALENTS) stays up to date;
- the instance layer (--instances, fake builders): digests independent of the continents, plan build/keep/remove,
  model pruning, carry_instances in swap() and swap_steps('lights'), staging leftovers.
With NORTHLIGHT_STOCK_CLIENT set (read only): the stock test client's install-manifest.json from before
the digests migrates to exactly the digests its chain has today (patch-z installed since then), so
only builder source changes can make it stale."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import contextlib, io, json, os, shutil, struct
import install_world_cache as iwc
import instance_maps

out = fp.output_dir()
client = out / 'client'
shutil.rmtree(client, ignore_errors=True)
for name in ['Wow.exe', 'Data/common.MPQ', 'Data/patch.MPQ', 'Data/patch-3.MPQ', 'Data/enUS/locale-enUS.MPQ',
             'Data/enUS/patch-enUS.MPQ', 'Data/deDE/locale-deDE.MPQ']:
    (client / name).parent.mkdir(parents=True, exist_ok=True)
    (client / name).write_bytes(b'x')


def digests(view='all', locale='enUS', maps=iwc.MAPS, tiles=None, without='z', sources=None):
    inputs = iwc.gather_inputs(client, view, locale, maps, tiles, without)
    if sources:
        inputs['sources'] = dict(inputs['sources'], **sources)
    return iwc.step_digests(inputs)


def run(argv):
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = iwc.main([*argv, '--locale', 'enUS'])
    return code, json.loads(buffer.getvalue().splitlines()[-1])


# 1. Digests: stable; our art layer and name case do not count; each input changes its steps only.
base = digests()
assert base == digests() and set(base) == {f'scene:{m}' for m in iwc.MAPS} | set(iwc.POST)
inputs = iwc.gather_inputs(client, 'all', 'enUS', iwc.MAPS, None, 'z')
assert [a['archive'] for a in inputs['archives']] == ['data/common.mpq', 'data/patch.mpq', 'data/patch-3.mpq',
                                                       'data/enus/locale-enus.mpq', 'data/enus/patch-enus.mpq']
assert set(inputs['sources']) == set(iwc.SOURCES)
for name in ['Data/patch-z.MPQ', 'Data/enUS/patch-enUS-z.MPQ']:
    (client / name).write_bytes(b'our art layer')
assert digests() == base                                                       # --without z
assert digests(without='') != base                                             # it was the trigger
os.rename(client / 'Data/patch-3.MPQ', client / 'Data/PATCH-3.mpq')
assert digests() == base                                                       # case
changed = {k for k, v in digests(sources={'outdoor-light-profiles.json': 'x'}).items() if base[k] != v}
assert changed == {'lights'}, changed
assert {k for k,v in digests(sources={'wotlk-emitter-profiles.json':'x'}).items() if base[k]!=v} == {'lights'}
assert {k for k, v in digests(sources={'forest_regions.inc': 'x'}).items() if base[k] != v} == {'fog'}
assert {k for k, v in digests(sources={'build_celestial_disc_assets.py': 'x'}).items() if base[k] != v} == {'celestial'}
assert {k for k, v in digests(sources={'world_scene_builder.py': 'x'}).items() if base[k] != v} == set(base)
assert all(o['scene:Azeroth'] != base['scene:Azeroth'] for o in [digests(locale='deDE'), digests(tiles=[31, 48, 33, 50])])
azeroth = digests(maps=['Azeroth'])                                             # a map's scene is its own
assert set(azeroth) == {'scene:Azeroth', *iwc.POST} and azeroth['scene:Azeroth'] == base['scene:Azeroth']
assert azeroth['lights'] != base['lights']
assert digests('stock') == base                                                # the view is not an input, the chain is
(client / 'Data/patch-x.MPQ').write_bytes(b'hd pack')
assert all(v != base[k] for k, v in digests().items())                         # a new archive: everything
(client / 'Data/patch-x.MPQ').unlink()


# 2. Plan against an installed cache, and a manifest from before the digests.
def fake_cache(folder, digests_or_inputs):
    shutil.rmtree(folder, ignore_errors=True)
    for name in [*(f'{m}/1_1.fg3' for m in iwc.MAPS), *(f'lights/{m}.fgl' for m in iwc.MAPS), 'fog/manifest.json',
                 'celestial/sun.fct', 'celestial/moon.fct']:
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_text(f'old {name}')
    manifest = {'format': iwc.FORMAT, 'problems': [], 'maps_built': iwc.MAPS}
    manifest.update(digests_or_inputs)
    (folder / 'install-manifest.json').write_text(json.dumps(manifest))
    return manifest


cache = out / 'world-cache'
recorded = fake_cache(cache, {'digests': base})
assert iwc.plan(base, recorded, cache, False, 'z') == ('up_to_date', [])
assert iwc.plan(base, recorded, cache, True, 'z') == ('full', iwc.POST)
lights_only = digests(sources={'outdoor-light-profiles.json': 'x'})
assert iwc.plan(lights_only, recorded, cache, False, 'z') == ('partial', ['lights'])
(cache / 'celestial/moon.fct').unlink()
assert iwc.plan(base, recorded, cache, False, 'z') == ('partial', ['celestial'])
assert iwc.plan(digests(maps=['Azeroth']), recorded, cache, False, 'z')[0] == 'full'
old_inputs = iwc.gather_inputs(client, 'all', 'enUS', iwc.MAPS, None, '')     # recorded patch-z, no 'without'
del old_inputs['without']
old = fake_cache(cache, {'inputs': old_inputs})
assert any('patch-z' in a['archive'] for a in old['inputs']['archives'])
assert iwc.recorded_digests(old, 'z') == base and iwc.plan(base, old, cache, False, 'z') == ('up_to_date', [])
# A falsely foreign art layer is now proven ours: preserve geometry, refresh post steps.
included = iwc.gather_inputs(client, 'all', 'enUS', iwc.MAPS, None, '')
misidentified = {'inputs': included, 'digests': iwc.step_digests(included)}
assert iwc.plan(base, misidentified, cache, False, 'z') == ('partial', iwc.POST)
assert misidentified['inputs']['without'] == ''  # planning never changes the record
extract, previous = out / 'world-cache.extract', out / 'world-cache.previous'
for folder in (extract, previous, cache / 'lights.previous'):
    folder.mkdir(exist_ok=True)
    (folder / 'keep').write_text('x')
code, last = run(['--client', str(client), '--output', str(cache), '--progress', 'human'])
assert (code, last['event']) == (0, 'up_to_date'), (code, last)
assert not previous.exists() and not (cache / 'lights.previous').exists() and (extract / 'keep').is_file()
assert iwc.side_folders(cache)[:2] == [out / 'world-cache.staging', previous]
(cache / 'install-manifest.json').write_text(json.dumps({'digests': base, 'problems': [], 'maps_built': iwc.MAPS}))
real_total = iwc.total_memory
iwc.total_memory = lambda: 4 << 30                                              # stops a full build at preflight
assert run(['--client', str(client), '--output', str(cache)])[1]['event'] == 'up_to_date'
code, last = run(['--client', str(client), '--output', str(cache), '--without', ''])
iwc.total_memory = real_total
assert (code, last['event'], last['stage']) == (2, 'failed', 'preflight'), last  # the foreign z is an input

# 2b. A cache built with the 0.3.183 scene/fog builders stays up to date when its reports cover every
# tile and record no oversized MOGP; anything else is a full rebuild, other stale inputs still count.
def built_with(sources, overrun=False, report=True):
    old_inputs = iwc.gather_inputs(client, 'all', 'enUS', iwc.MAPS, None, 'z')
    old_inputs['sources'] = dict(old_inputs['sources'], **sources)
    recorded = fake_cache(cache, {'inputs': old_inputs, 'digests': iwc.step_digests(old_inputs)})
    (cache / 'fog/manifest.json').write_text(json.dumps({'unsupported': {}}))
    for m in iwc.MAPS if report else []:
        unsupported = ["wmo:x.wmo:Chunk exceeds file: b'PGOM'"] if overrun and m == 'Azeroth' else []
        (cache / f'build-{m}-1-1.json').write_text(json.dumps({'generated': [{'tile': [1, 1]}], 'failures': [],
                                                                'unsupported_assets': unsupported}))
    return recorded


MAIN = {'world_scene_builder.py': 'c612cf39a6166cd33d815a9fbcca8b65ae545058d02b156b57edaf39b7d9b551',   # main before the instance maps
        'world_lights_builder.py': '5ba2844340b271a3d7671b98d98a899379e92493f0cf82a8062d9b46452850a5',
        'world_light_placements.py': '651c5766dde74f945ebdff268973ff1c87e35c203fccca484bce4a75886adad8'}
v183 = {n: h for n, equal in iwc.SOURCE_EQUIVALENTS.items() for h, clamp in equal.items() if clamp}
v193 = {n: h for n, equal in iwc.SOURCE_EQUIVALENTS.items() for h, clamp in equal.items() if not clamp and h != MAIN.get(n)}
assert set(v183) == {'world_scene_builder.py', 'regional_fog_builder.py'} and set(v193) == {'world_scene_builder.py', 'mpq.py', 'client_archives.py'}
# A continent cache recorded with main's scene, lights and placement builders (before the instance maps) is up to
# date: lights, fog and celestial come out the same, so none of their digests goes stale, and no report check is needed.
assert all(iwc.SOURCE_EQUIVALENTS[n][h] is False for n, h in MAIN.items())
assert iwc.plan(base, built_with(MAIN, report=False), cache, False, 'z') == ('up_to_date', [])
assert iwc.plan(base, built_with(dict(MAIN, **{'m2_visibility.py': 'f' * 64})), cache, False, 'z')[0] == 'full'
assert iwc.plan(lights_only, built_with(MAIN), cache, False, 'z') == ('partial', ['lights'])
# 0.3.193's scene builder, mpq.py and client_archives.py: up to date without any report check (they stopped where 0.3.194 differs).
for kw in [{}, {'report': False}, {'overrun': True}]:
    assert iwc.plan(base, built_with(v193, **kw), cache, False, 'z') == ('up_to_date', []), kw
assert iwc.plan(base, built_with(dict(v193, **{'m2_visibility.py': 'f' * 64})), cache, False, 'z')[0] == 'full'
mixed = dict(v193, **{'world_scene_builder.py': v183['world_scene_builder.py']})   # 0.3.183 needs clamp_free
assert iwc.plan(base, built_with(mixed), cache, False, 'z') == ('up_to_date', [])
assert iwc.plan(base, built_with(mixed, overrun=True), cache, False, 'z')[0] == 'full'
assert iwc.plan(base, built_with(v183), cache, False, 'z') == ('up_to_date', [])
assert iwc.plan(base, built_with(v183, overrun=True), cache, False, 'z')[0] == 'full'
assert iwc.plan(base, built_with(v183, report=False), cache, False, 'z')[0] == 'full'
assert iwc.plan(base, built_with(dict(v183, **{'world_scene_builder.py': 'f' * 64})), cache, False, 'z')[0] == 'full'
assert iwc.plan(lights_only, built_with(v183), cache, False, 'z') == ('partial', ['lights'])
recorded = built_with(v183)
(cache / 'Azeroth/2_2.fg3').write_text('resumed after a kill')                    # no report covers it
assert iwc.plan(base, recorded, cache, False, 'z')[0] == 'full'
# A few unreadable WMOs are tolerated and reported; more than 5% (or a tile short) is a problem.
fog_staging = out / 'fog-staging'
for unreadable, tiles, problem in [(1, 1, False), (6, 1, True), (0, 0, True)]:
    (fog_staging / 'fog').mkdir(parents=True, exist_ok=True)
    (fog_staging / 'fog/manifest.json').write_text(json.dumps({
        'maps': {'Azeroth': {'tiles': tiles, 'expected': 1}}, 'stats': {'wmo_roots': 100},
        'unreadable_wmos': {f'w{i}.wmo': 'Chunk exceeds file' for i in range(unreadable)}, 'clamped_wmo_groups': ['w_000.wmo']}))
    assert bool(iwc.post_problems(fog_staging, ['fog'], ['Azeroth'])) == problem, (unreadable, tiles)
assert iwc.tolerated(fog_staging, [], ['fog']) == {'clamped_wmo_groups': ['w_000.wmo'], 'fog_unreadable_wmos': {},
                                                   'archive_warnings': []}
# A run without scene steps keeps the archive warnings its installed scenes were built with.
unlisted = {'archive': 'Data/patch-R.mpq', 'problem': 'unlisted'}
assert iwc.tolerated(fog_staging, [], ['fog'], {'tolerated': {'archive_warnings': [unlisted]}})['archive_warnings'] == [unlisted]
assert iwc.tolerated(fog_staging, [], ['fog'], {'tolerated': {}})['archive_warnings'] == []
assert iwc.human({'event': 'archive_warning', **unlisted}).startswith('Warning: Data/patch-R.mpq has no (listfile)')
shutil.rmtree(fog_staging)

# 3. A run killed after its build finished completes the swap on rerun, without building.
staging = out / 'world-cache.staging'
fake_cache(staging, {'digests': base, 'swap': 'full'})
(staging / 'Azeroth/1_1.fg3').write_text('new')
shutil.rmtree(cache)
os.makedirs(out / 'world-cache.previous', exist_ok=True)                        # killed between the two renames
code, last = run(['--client', str(client), '--output', str(cache)])
assert (code, last['event'], last.get('completed_interrupted_swap')) == (0, 'done', True), last
assert (cache / 'Azeroth/1_1.fg3').read_text() == 'new' and not staging.exists()
assert not (out / 'world-cache.previous').exists()
# ... and a partial update killed after lights/ moved but before the manifest.
fake_cache(cache, {'digests': base})
(staging / 'logs').mkdir(parents=True)
(staging / 'logs/lights.log').write_text('log')
(staging / 'install-manifest.json').write_text(json.dumps({'digests': lights_only, 'problems': [], 'swap': ['lights']}))
(cache / 'lights').rename(cache / 'lights.previous')
(cache / 'lights').mkdir()
(cache / 'lights/Azeroth.fgl').write_text('new')
assert iwc.finish_staging(staging, cache, lights_only, False)
assert json.loads((cache / 'install-manifest.json').read_text())['digests'] == lights_only
assert not (cache / 'lights.previous').exists() and not staging.exists() and (cache / 'logs/lights.log').is_file()
# A finished staging for other digests is not swapped in; a leftover without a manifest goes.
fake_cache(staging, {'digests': base, 'swap': 'full'})
assert not iwc.finish_staging(staging, cache, lights_only, False) and staging.exists()
(staging / 'install-manifest.json').unlink()
assert not iwc.finish_staging(staging, cache, lights_only, False) and not staging.exists()
(cache / 'fog').rename(cache / 'fog.previous')                                   # killed inside swap_steps
iwc.recover(cache)
assert (cache / 'fog/manifest.json').is_file() and not (cache / 'fog.previous').exists()

# 4. Memory: largest map first; budgets bound the jobs; too little memory refuses; bad input exits 2.
GB = 1 << 30
assert [m for m, _ in iwc.map_jobs(iwc.MAPS)] == ['Northrend', 'Azeroth', 'Expansion01', 'Kalimdor']
needs = [need for _, need in iwc.map_jobs(iwc.MAPS)]
assert iwc.admit(needs, 0, 0, 4, 1 * GB) == 0                                  # the first one always starts
assert iwc.admit(needs[1:], 6.5 * GB, 1, 4, 12 * GB) == 0                      # Azeroth fits next to Northrend
assert iwc.admit(needs[2:], 11 * GB, 2, 4, 12 * GB) is None                    # nothing else fits
assert iwc.admit(needs[2:], 4.5 * GB, 1, 4, 9 * GB) == 0 and iwc.admit(needs[3:], 4.5 * GB, 1, 1, 99 * GB) is None
assert iwc.admit([6.5 * GB, 3.5 * GB], 4.5 * GB, 1, 4, 8.5 * GB) == 1           # a smaller one fills the gap
assert iwc.memory_refusal('partial', 4 * GB) is None and iwc.memory_refusal('full', 7.2 * GB) is None
assert '8 GB' in iwc.memory_refusal('full', 6.9 * GB)
if sys.platform == 'darwin':                                                     # installed RAM, hw.memsize
    import subprocess
    assert iwc.total_memory() == int(subprocess.run(['sysctl', '-n', 'hw.memsize'], capture_output=True, text=True).stdout)
shutil.rmtree(cache)
iwc.total_memory = lambda: 4 * GB
code, last = run(['--client', str(client), '--output', str(cache)])
iwc.total_memory = real_total
assert (code, last['event'], last['stage']) == (2, 'failed', 'preflight') and '8 GB' in last['error'], last
assert not staging.exists() and not cache.exists() and (extract / 'keep').is_file()
code, last = run(['--client', str(out / 'no-such-client'), '--output', str(cache)])
assert (code, last['event'], last['stage']) == (2, 'failed', 'input'), last

# 5. Sweep: only the builders' <name>.<pid>.tmp files go. Retries: a PermissionError is retried.
shutil.rmtree(staging, ignore_errors=True)
for name in ['models/ab.12345.tmp', 'Azeroth.fgl.9.tmp', 'models/ab.fgs', 'keep.tmp']:
    (staging / name).parent.mkdir(parents=True, exist_ok=True)
    (staging / name).write_bytes(b'')
assert iwc.sweep(staging) == 2
assert sorted(p.relative_to(staging).as_posix() for p in staging.rglob('*') if p.is_file()) == ['keep.tmp', 'models/ab.fgs']
shutil.rmtree(staging)
calls = []


def locked_once():
    calls.append(1)
    if len(calls) == 1:
        raise PermissionError('held by a scanner')
    return 'ok'


iwc.LOCK_RETRIES = (0, 0)
assert iwc.retry(locked_once) == 'ok' and len(calls) == 2

# 6. A failed build names the failed steps' logs; --progress human prints their last lines (tile records left out).
logs = out / 'failed-logs'
shutil.rmtree(logs, ignore_errors=True); logs.mkdir()
scene = logs / 'scene-Azeroth.log'
scene.write_text(''.join(f'{{"tile": [32, {i}]}}\n' for i in range(40)) + 'Traceback (most recent call last):\n'
                 '  File "mpq.py", line 12, in <module>\nOSError: libstorm.so: cannot open shared object file\n')
(logs / 'scene-Kalimdor.log').write_text('fine\n')
steps = [type('S', (), {'proc': type('P', (), {'returncode': rc})(), 'log': logs / n, 'map': m, 'name': k})()
         for rc, n, m, k in [(1, 'scene-Azeroth.log', 'Azeroth', 'scene'), (0, 'scene-Kalimdor.log', 'Kalimdor', 'scene'),
                             (0, 'scene-Northrend.log', 'Northrend', 'scene'), (0, 'lights.log', None, 'lights'),
                             (0, 'validate.log', None, 'validate')]]
assert iwc.failed_logs(steps) == [str(scene)]
for s_ in steps:
    s_.proc.returncode = 0
assert iwc.failed_logs(steps, {'Kalimdor': ['32_48']}) == [str(logs / 'scene-Kalimdor.log'), str(logs / 'validate.log')]
assert iwc.failed_logs(steps[:4]) == [str(scene), str(logs / 'scene-Kalimdor.log'), str(logs / 'scene-Northrend.log')]   # capped
tail = iwc.log_tail(scene)
assert tail[-1] == 'OSError: libstorm.so: cannot open shared object file' and not any(t.startswith('{"tile"') for t in tail)
buffer = io.StringIO()
iwc.PROGRESS = 'human'
with contextlib.redirect_stdout(buffer):
    assert iwc.final('failed', 1, problems=['scene Azeroth failed'], logs=[str(scene)]) == 1
iwc.PROGRESS = 'json'
lines = buffer.getvalue().splitlines()
assert lines[0] == 'World cache build FAILED: scene Azeroth failed' and lines[1] == f'--- last lines of {scene} ---'
assert '  | OSError: libstorm.so: cannot open shared object file' in lines and json.loads(lines[-1])['logs'] == [str(scene)]
assert iwc.log_tail(logs / 'missing.log')[0].startswith('(cannot read the log')

# 7. The stock test client's manifest from before the digests (read only).
stock = os.environ.get('NORTHLIGHT_STOCK_CLIENT')
if stock:
    stock = Path(stock)
    manifest = json.loads((stock / 'world-cache/install-manifest.json').read_text())
    assert 'digests' not in manifest and manifest['inputs']['view'] == 'stock'
    now = iwc.gather_inputs(stock, 'all', manifest['inputs']['locale'], iwc.MAPS, None, 'z')
    assert (stock / 'data/patch-z.mpq').is_file()                                  # installed after the build
    assert not any('patch-z' in a['archive'] or 'enus-z' in a['archive'] for a in now['archives'])
    assert [a['archive'] for a in now['archives']] == [a['archive'] for a in manifest['inputs']['archives']]
    as_built = iwc.step_digests(dict(now, sources=manifest['inputs']['sources']))
    assert iwc.recorded_digests(manifest, 'z') == as_built                          # the chain alone is not stale
    stale = sorted(n for n, h in manifest['inputs']['sources'].items() if now['sources'].get(n) != h)
    print('stock test client: chain unchanged under patch-z; sources changed since its build:', ', '.join(stale))
else:
    print('NORTHLIGHT_STOCK_CLIENT not set: the stock manifest migration check did not run')

# 8. The instance layer: digests, plan, build (fake builders), removal, model pruning, carry-over, staging leftovers.
inputs = iwc.gather_inputs(client, 'all', 'enUS', [], None, 'z')
inst = iwc.instance_digests(inputs)
assert list(inst) == instance_maps.KEYS and len(set(inst.values())) == 6 and inst == iwc.instance_digests(inputs)
assert iwc.instance_digests(dict(inputs, locale='deDE')) != inst
# Independent of the continents: fog, celestial and tile-range inputs leave them alone; lights and scene sources count.
def inst_with(**sources):
    return iwc.instance_digests(dict(inputs, sources=dict(inputs['sources'], **sources)))
assert inst_with(**{'regional_fog_builder.py': 'x', 'forest_regions.inc': 'x', 'build_celestial_disc_assets.py': 'x'}) == inst
assert all(inst_with(**{n: 'x'}) != inst for n in ['world_scene_builder.py', 'world_lights_builder.py', 'world_light_placements.py',
                                                    'validate_world_cache.py', 'instance_maps.py'])
assert digests() == base                                                       # the continents never see the layer
os.rename(client / 'Data/PATCH-3.mpq', client / 'Data/patch-3.MPQ')
assert iwc.instance_digests(iwc.gather_inputs(client, 'all', 'enUS', [], None, 'z')) == inst   # name case, our art layer
(client / 'Data/patch-x.MPQ').write_bytes(b'hd pack')
assert all(v != inst[k] for k, v in iwc.instance_digests(iwc.gather_inputs(client, 'all', 'enUS', [], None, 'z')).items())
(client / 'Data/patch-x.MPQ').unlink()


def tile(*keys):
    return b'FGS3' + struct.pack('<II', 3, len(keys)) + b''.join(
        struct.pack('<32sQI18f', k, i, 1, *range(18)) for i, k in enumerate(keys))


def key_of(name):
    return bytes([len(name)]) + name.encode().ljust(31, b'.')


def model_file(folder, key):
    (folder / 'models').mkdir(parents=True, exist_ok=True)
    (folder / 'models' / (key.hex() + '.fgs')).write_text('model ' + key.hex() + BUILD)


BUILD = ''                                                                      # the "content" a build writes into its models
CONTINENT_ONLY = key_of('continent-only')
SHARED = key_of('shared')                                                       # a model a continent tile and instance tiles use


class FakeStep:
    def __init__(self, name, log):
        self.name, self.log, self.map, self.seconds, self.peak_rss = name, log, None, 0.1, None
        self.proc = type('P', (), {'returncode': 0})()

    def record(self):
        return {'step': self.name, 'exit': 0}


built_groups, fail_group, called_steps = [], [], []


def fake_run_parallel(specs, jobs, budget=float('inf')):
    """Stand-ins for the builders: a tile and a unique model per map, a .fgl per map, a validation report."""
    steps = []
    for name, args, log, _, _ in specs:
        called_steps.append(name)
        args = [str(a) for a in args]
        staging = Path(args[args.index('--output') + 1]) if '--output' in args else None
        if name.startswith('instances ') and name.split()[1] in instance_maps.KEYS:
            key = name.split()[1]
            built_groups.append(key)
            for m in args[args.index('--map') + 1:args.index('--output')]:
                (staging / m).mkdir(parents=True, exist_ok=True)
                (staging / m / '1_1.fg3').write_bytes(tile(SHARED, key_of('unique-' + m)))
                model_file(staging, SHARED), model_file(staging, key_of('unique-' + m)), model_file(staging, CONTINENT_ONLY)
                (staging / f'build-{m}-100-1.json').write_text(json.dumps({
                    'failures': [{'tile': [1, 1], 'error': 'x'}] if key in fail_group else [], 'empty_source_tiles': [],
                    'clamped_wmo_groups': [], 'archive_warnings': []}))
        elif name == 'instances lights':
            cache_ = Path(args[args.index('--cache') + 1])
            maps = args[args.index('--maps') + 1:args.index('--manifest')]
            for m in maps:
                assert (cache_ / m / '1_1.fg3').is_file()
                (staging / f'{m}.fgl').parent.mkdir(parents=True, exist_ok=True)
                (staging / f'{m}.fgl').write_text('lights ' + m)
            (staging / args[args.index('--manifest') + 1]).write_text(json.dumps({'maps': [{'map': m} for m in maps]}))
        elif name == 'instances validate':
            Path(args[args.index('--report') + 1]).write_text('{}')
            assert (Path(args[args.index('--cache') + 1]) / 'models' / (SHARED.hex() + '.fgs')).is_file()
        steps.append(FakeStep(name, log))
    return steps


real_run_parallel = iwc.run_parallel
iwc.run_parallel = fake_run_parallel
icache = out / 'inst-cache'
istaging = out / 'inst-cache.instmp'
shutil.rmtree(icache, ignore_errors=True)
shutil.rmtree(istaging, ignore_errors=True)


def inst_run(spec, *extra):
    built_groups.clear()
    called_steps.clear()
    return run(['--client', str(client), '--output', str(icache), '--instances', spec, '--jobs', '2', *extra])


def models_of(folder):
    return sorted(p.stem for p in (folder / 'models').glob('*.fgs') if p.stem != CONTINENT_ONLY.hex())   # that one always stays


raids = instance_maps.maps_for(['classic-raids'])
wotlk = instance_maps.maps_for(['wotlk-raids'])
# a missing continent cache is refused; "none" without any layer is a no-op
code, last = inst_run('classic-raids')
assert (code, last['event'], last['stage']) == (2, 'failed', 'preflight') and 'continent' in last['error'], last
assert inst_run('none')[1]['event'] == 'up_to_date' and not icache.exists() and not istaging.exists()
fake_cache(icache, {'digests': base})
model_file(icache, SHARED)
(icache / 'Azeroth/3_3.fg3').write_bytes(tile(SHARED, CONTINENT_ONLY))
model_file(icache, CONTINENT_ONLY)
continents = sorted(p.relative_to(icache).as_posix() for p in icache.rglob('*') if p.is_file())
assert inst_run('bogus')[0] == 2 and inst_run('none')[1]['event'] == 'up_to_date' and not (icache / iwc.INSTANCE_MANIFEST).exists()
# 8a. one group: tiles, lights and the models it needs move in; the shared model is not duplicated; staging is gone
code, last = inst_run('classic-raids')
assert (code, last['event'], last['built'], last['kept'], last['removed']) == (0, 'done', ['classic-raids'], [], []), last
assert built_groups == ['classic-raids'] and not istaging.exists()
assert all((icache / m / '1_1.fg3').is_file() and (icache / 'lights' / f'{m}.fgl').is_file() for m in raids)
record = json.loads((icache / iwc.INSTANCE_MANIFEST).read_text())
assert record['format'] == iwc.INSTANCE_FORMAT and list(record['groups']) == ['classic-raids']
assert record['groups']['classic-raids']['maps_built'] == raids and record['groups']['classic-raids']['digest'] == inst['classic-raids']
assert record['lights'] == [f'{m}.fgl' for m in raids]
assert models_of(icache) == sorted([SHARED.hex(), *(key_of('unique-' + m).hex() for m in raids)])
assert (icache / 'models' / (SHARED.hex() + '.fgs')).read_text() == 'model ' + SHARED.hex()
assert (icache / 'models' / (CONTINENT_ONLY.hex() + '.fgs')).read_text() == 'model ' + CONTINENT_ONLY.hex()
assert [m['map'] for m in json.loads((icache / 'lights' / iwc.INSTANCE_MANIFEST).read_text())['maps']] == raids
assert (icache / 'instances-validation.json').is_file() and (icache / 'logs/instances-lights.log').parent.is_dir()
assert all(f in sorted(p.relative_to(icache).as_posix() for p in icache.rglob('*') if p.is_file()) for f in continents)
# the continents see no change: their plan stays up to date, the layer is not part of their digests
recorded = json.loads((icache / 'install-manifest.json').read_text())
assert iwc.plan(base, recorded, icache, False, 'z') == ('up_to_date', [])
# 8b. nothing changes: up to date; another group is added, the first is kept
code, last = inst_run('classic-raids')
assert (code, last['event'], last['kept']) == (0, 'up_to_date', ['classic-raids']) and built_groups == []
code, last = inst_run('classic-raids,wotlk-raids')
assert (code, last['built'], last['kept']) == (0, ['wotlk-raids'], ['classic-raids']) and built_groups == ['wotlk-raids']
assert list(json.loads((icache / iwc.INSTANCE_MANIFEST).read_text())['groups']) == ['classic-raids', 'wotlk-raids']
assert [m['map'] for m in json.loads((icache / 'lights' / iwc.INSTANCE_MANIFEST).read_text())['maps']] == raids + wotlk
assert iwc.plan(base, recorded, icache, False, 'z') == ('up_to_date', [])
# --instances-plan is a dry run: it names what would be built and changes nothing
before_plan = sorted(p.relative_to(icache).as_posix() for p in icache.rglob('*'))
code, last = inst_run('classic-raids,tbc-raids', '--instances-plan')
assert (code, last['event'], last['build'], last['keep'], last['remove']) == (0, 'instances_plan', ['tbc-raids'], ['classic-raids'], ['wotlk-raids']), last
assert before_plan == sorted(p.relative_to(icache).as_posix() for p in icache.rglob('*')) and built_groups == []
# Model files are named by a hash of the model's name, not its content: a rebuilt group replaces the models its old
# tiles used (the sources may have changed their content) but leaves a model a continent tile (SHARED) or only a
# continent (CONTINENT_ONLY) uses byte-identical.
BUILD = ' rebuilt'
assert inst_run('classic-raids,wotlk-raids', '--force')[1]['built'] == ['classic-raids', 'wotlk-raids']
unique = icache / 'models' / (key_of('unique-' + raids[0]).hex() + '.fgs')
assert unique.read_text().endswith(' rebuilt') and (icache / 'models' / (SHARED.hex() + '.fgs')).read_text() == 'model ' + SHARED.hex()
assert not (icache / 'models' / (CONTINENT_ONLY.hex() + '.fgs')).read_text().endswith(' rebuilt')
BUILD = ''
# Light-only changes reuse every geometry/model file; validation-only changes run no builder.
real_sources = iwc.source_hashes
original_sources = real_sources()
unchanged = {p: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ino)
             for m in raids + wotlk for p in (icache / m).glob('*.fg3')}
unchanged[unique] = (unique.read_bytes(), unique.stat().st_mtime_ns, unique.stat().st_ino)
shared_path = icache / 'models' / (SHARED.hex() + '.fgs')
unchanged[shared_path] = (shared_path.read_bytes(), shared_path.stat().st_mtime_ns, shared_path.stat().st_ino)
try:
    iwc.source_hashes = lambda: dict(original_sources, **{'world_lights_builder.py': 'new lights'})
    assert inst_run('2 6')[1]['built'] == ['classic-raids', 'wotlk-raids']
    assert built_groups == [] and called_steps == ['instances lights', 'instances validate'], called_steps
    assert all((p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ino) == value for p, value in unchanged.items())
    light_sources = iwc.source_hashes()
    iwc.source_hashes = lambda: dict(light_sources, **{'validate_world_cache.py': 'new validation'})
    light_times = {p: p.stat().st_mtime_ns for p in (icache / 'lights').glob('*.fgl')}
    assert inst_run('2 6')[1]['built'] == ['classic-raids', 'wotlk-raids']
    assert built_groups == [] and called_steps == ['instances validate'], called_steps
    assert all(p.stat().st_mtime_ns == t for p, t in light_times.items())
    assert all((p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ino) == value for p, value in unchanged.items())
    # Adding a new group alongside a light update must preserve shared models of the reused groups.
    iwc.source_hashes = lambda: dict(light_sources, **{'world_lights_builder.py': 'next lights'})
    assert inst_run('2 4 6')[1]['built'] == ['classic-raids', 'tbc-raids', 'wotlk-raids']
    assert built_groups == ['tbc-raids']
    assert all((p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ino) == value for p, value in unchanged.items())
finally:
    iwc.source_hashes = real_sources
assert inst_run('2 6')[0] == 0 and not built_groups
# Correcting ownership must also preserve instance meshes after continent inputs are replaced.
continent_record = iwc.read_json(icache / 'install-manifest.json')
with_art = dict(inputs, without='', archives=[*inputs['archives'], {'archive': 'Data/patch-z.mpq', 'bytes': 1, 'mtime_ns': 1}])
(icache / 'install-manifest.json').write_text(json.dumps({'inputs': with_art, 'digests': iwc.step_digests(with_art)}))
misidentified = iwc.read_json(icache / iwc.INSTANCE_MANIFEST)
for k, r in misidentified['groups'].items():
    r['digest'] = iwc.instance_digests(with_art)[k]
    r.pop('digests', None)
(icache / iwc.INSTANCE_MANIFEST).write_text(json.dumps(misidentified))
rebased = iwc.rebase_instance_scenes(icache, 'z')
assert all(r['digests']['scene'] == iwc.instance_step_digests(inputs)[k]['scene'] for k, r in rebased['groups'].items())
(icache / iwc.INSTANCE_MANIFEST).write_text(json.dumps(rebased))
(icache / 'install-manifest.json').write_text(json.dumps(continent_record))
assert inst_run('2 6')[0] == 0 and not built_groups
assert called_steps == ['instances lights', 'instances validate'], called_steps
# Upgrade legacy metadata on a current cache, without any builder or validator.
legacy = iwc.read_json(icache / iwc.INSTANCE_MANIFEST)
for record_ in legacy['groups'].values():
    record_.pop('digests')
(icache / iwc.INSTANCE_MANIFEST).write_text(json.dumps(legacy))
assert inst_run('2 6')[1]['event'] == 'up_to_date' and not called_steps
assert all('digests' in r for r in iwc.read_json(icache / iwc.INSTANCE_MANIFEST)['groups'].values())
# a group with missing folders is built again; --force rebuilds the selected ones
shutil.rmtree(icache / raids[0])
assert inst_run('classic-raids,wotlk-raids')[1]['built'] == ['classic-raids']
assert inst_run('2 6', '--force')[1]['built'] == ['classic-raids', 'wotlk-raids']
(icache / 'lights' / f'{wotlk[0]}.fgl').unlink()                                # a missing .fgl counts as missing too
assert inst_run('2 6')[1]['built'] == ['wotlk-raids']
# 8c. removing a group: its folders, lights and the models only it used go; shared models stay; the continents are untouched
code, last = inst_run('wotlk-raids')
assert (code, last['built'], last['removed'], last['kept']) == (0, [], ['classic-raids'], ['wotlk-raids']), last
assert not any((icache / m).exists() or (icache / 'lights' / f'{m}.fgl').exists() for m in raids)
assert all((icache / m / '1_1.fg3').is_file() for m in wotlk)
assert models_of(icache) == sorted([SHARED.hex(), *(key_of('unique-' + m).hex() for m in wotlk)]) and last['pruned_models'] >= len(raids)
assert [m['map'] for m in json.loads((icache / 'lights' / iwc.INSTANCE_MANIFEST).read_text())['maps']] == wotlk
assert list(json.loads((icache / iwc.INSTANCE_MANIFEST).read_text())['groups']) == ['wotlk-raids']
# "none" removes the rest, the manifests included; the models of the continents stay
code, last = inst_run('none')
assert (code, last['removed']) == (0, ['wotlk-raids']), last
assert models_of(icache) == [SHARED.hex()] and not (icache / iwc.INSTANCE_MANIFEST).exists()
assert not (icache / 'lights' / iwc.INSTANCE_MANIFEST).exists() and not any((icache / m).exists() for m in wotlk)
assert sorted(p.relative_to(icache).as_posix() for p in icache.rglob('*') if p.is_file() and 'logs' not in p.parts
              and p.name != 'instances-validation.json') == continents
assert inst_run('none')[1]['event'] == 'up_to_date'
# leftovers of a failed carry-over (folders without a manifest) are removed when deselected
inst_run('classic-raids')
(icache / iwc.INSTANCE_MANIFEST).unlink()
assert inst_run('none')[1]['removed'] == ['classic-raids'] and not any((icache / m).exists() for m in raids)
# 8d. a group with tile failures fails the run and keeps its staging folder for a rerun
fail_group.append('tbc-raids')
code, last = inst_run('tbc-raids')
assert (code, last['event']) == (1, 'failed') and any('tile failures' in p for p in last['problems']) and istaging.is_dir(), last
assert not (icache / instance_maps.maps_for(['tbc-raids'])[0]).exists() and not (icache / iwc.INSTANCE_MANIFEST).exists()
fail_group.clear()
code, last = inst_run('tbc-raids')                                              # the digests match: the staging folder resumes
assert (code, last['built']) == (0, ['tbc-raids']) and not istaging.exists()
inst_run('none')
# 8e. staging leftovers: another selection's state goes; a validated one completes its move; a stale one goes
(istaging / 'logs').mkdir(parents=True)
(istaging / 'install-state.json').write_text(json.dumps({'digests': {'classic-raids': 'other'}}))
(istaging / 'Karazahn').mkdir()
assert inst_run('classic-raids')[1]['built'] == ['classic-raids'] and not istaging.exists() and not (icache / 'Karazahn').exists()
inst_run('none')
ready_map = raids[0]
for digest_, completes in [(inst['classic-raids'], True), ('stale', False)]:
    shutil.rmtree(istaging, ignore_errors=True)
    (istaging / ready_map).mkdir(parents=True)
    (istaging / ready_map / '1_1.fg3').write_bytes(tile(SHARED))
    (istaging / 'lights').mkdir()
    (istaging / 'lights' / f'{ready_map}.fgl').write_text('lights')
    group_record = {'digest': digest_, 'maps_built': [ready_map], 'tile_failures': {}, 'empty_source_tiles': {}}
    (istaging / iwc.INSTANCE_READY).write_text(json.dumps({
        'digests': {'classic-raids': digest_}, 'built_maps': [ready_map], 'drop_maps': [], 'prune': [],
        'manifest': {'format': iwc.INSTANCE_FORMAT, 'groups': {'classic-raids': group_record}, 'lights': []}}))
    code, last = inst_run('none')
    assert code == 0 and not istaging.exists()
    assert (icache / ready_map).is_dir() == completes and bool(last.get('completed_interrupted_swap')) == completes, (completes, last)
    shutil.rmtree(icache / ready_map, ignore_errors=True)
    (icache / 'lights' / f'{ready_map}.fgl').unlink(missing_ok=True)
    (icache / iwc.INSTANCE_MANIFEST).unlink(missing_ok=True)
(icache / 'lights' / iwc.INSTANCE_MANIFEST).unlink(missing_ok=True)
iwc.run_parallel = real_run_parallel
# the instance staging is a side folder beside the continents' ones, never dropped as a "previous" folder
assert iwc.side_folders(icache)[-1] == istaging and len(istaging.name) <= len('inst-cache.staging')
istaging.mkdir(exist_ok=True)
iwc.drop_previous(icache)
assert istaging.is_dir()
shutil.rmtree(istaging)

# 8f. carry_instances: a full swap and a swapped lights/ step keep the layer; a failing carry never fails the swap.
def instance_cache(folder):
    fake_cache(folder, {'digests': base})
    model_file(folder, SHARED)
    (folder / 'Azeroth/3_3.fg3').write_bytes(tile(SHARED))
    for m in raids:
        (folder / m).mkdir()
        (folder / m / '1_1.fg3').write_bytes(tile(SHARED, key_of('unique-' + m)))
        model_file(folder, key_of('unique-' + m))
        (folder / 'lights' / f'{m}.fgl').write_text('lights ' + m)
    (folder / 'lights' / iwc.INSTANCE_MANIFEST).write_text('{"maps": []}')
    (folder / iwc.INSTANCE_MANIFEST).write_text(json.dumps({'groups': {'classic-raids': {'maps_built': raids}}}))


instance_cache(icache)
new = out / 'inst-new'
fake_cache(new, {'digests': base, 'swap': 'full'})
(new / 'Azeroth/1_1.fg3').write_text('new continents')
model_file(new, SHARED)                                                          # the new build has the shared model
iwc.swap(new, icache, False)
assert (icache / 'Azeroth/1_1.fg3').read_text() == 'new continents' and not new.exists()
assert all((icache / m / '1_1.fg3').is_file() and (icache / 'lights' / f'{m}.fgl').read_text() == 'lights ' + m for m in raids)
assert models_of(icache) == sorted([SHARED.hex(), *(key_of('unique-' + m).hex() for m in raids)])
assert (icache / iwc.INSTANCE_MANIFEST).is_file() and (icache / 'lights' / iwc.INSTANCE_MANIFEST).is_file()
assert not (out / 'inst-cache.previous').exists()
# swap_steps('lights'): the new lights/ keeps the instance .fgl files; a replay after a kill in the middle does too
staging_steps = out / 'inst-steps'
fake_cache(staging_steps, {'digests': base, 'swap': ['lights']})
for keep in [staging_steps / n for n in ('Azeroth', 'Kalimdor', 'Expansion01', 'Northrend', 'fog', 'celestial')]:
    shutil.rmtree(keep)
(staging_steps / 'lights/Azeroth.fgl').write_text('new lights')
iwc.swap_steps(staging_steps, icache, ['lights'])
assert (icache / 'lights/Azeroth.fgl').read_text() == 'new lights' and not (icache / 'lights.previous').exists()
assert all((icache / 'lights' / f'{m}.fgl').read_text() == 'lights ' + m for m in raids)
assert (icache / 'lights' / iwc.INSTANCE_MANIFEST).is_file() and (icache / raids[0] / '1_1.fg3').is_file()
fake_cache(staging_steps, {'digests': base, 'swap': ['lights']})                 # killed after lights -> lights.previous
(icache / 'lights').rename(icache / 'lights.previous')
iwc.swap_steps(staging_steps, icache, ['lights'])
assert all((icache / 'lights' / f'{m}.fgl').read_text() == 'lights ' + m for m in raids)
# a failing carry is reported, drops the manifest and does not fail the swap
instance_cache(icache)
new = out / 'inst-new'
fake_cache(new, {'digests': base, 'swap': 'full'})
real_carry = iwc.carry_instances
iwc.carry_instances = lambda *a: (_ for _ in ()).throw(PermissionError('held by a scanner'))
buffer = io.StringIO()
with contextlib.redirect_stdout(buffer):
    iwc.swap(new, icache, False)
iwc.carry_instances = real_carry
assert 'instances_carry_failed' in buffer.getvalue() and (icache / 'Azeroth/1_1.fg3').read_text() == 'old Azeroth/1_1.fg3'
assert not (icache / iwc.INSTANCE_MANIFEST).exists()
shutil.rmtree(icache, ignore_errors=True)
print('install_world_cache instance layer: digests, plan, build, removal, pruning, carry-over, staging leftovers OK')
print('install_world_cache: digests, plan, migration, finished-staging swap, memory budgets, exit codes OK')
