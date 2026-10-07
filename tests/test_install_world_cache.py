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
- stale *.<pid>.tmp files are swept; Windows lock retries.
With NORTHLIGHT_STOCK_CLIENT set (read only): the stock test client's install-manifest.json from before
the digests migrates to exactly the digests its chain has today (patch-z installed since then), so
only builder source changes can make it stale."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import contextlib, io, json, os, shutil
import install_world_cache as iwc

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


v183 = {n: h for n, equal in iwc.SOURCE_EQUIVALENTS.items() for h, clamp in equal.items() if clamp}
v192 = {n: h for n, equal in iwc.SOURCE_EQUIVALENTS.items() for h, clamp in equal.items() if not clamp}
assert set(v183) == {'world_scene_builder.py', 'regional_fog_builder.py'} and set(v192) == {'world_scene_builder.py', 'mpq.py'}
# 0.3.192's scene builder and mpq.py: up to date without any report check (they stopped where 0.3.193 differs).
for kw in [{}, {'report': False}, {'overrun': True}]:
    assert iwc.plan(base, built_with(v192, **kw), cache, False, 'z') == ('up_to_date', []), kw
assert iwc.plan(base, built_with(dict(v192, **{'client_archives.py': 'f' * 64})), cache, False, 'z')[0] == 'full'
mixed = dict(v192, **{'world_scene_builder.py': v183['world_scene_builder.py']})   # 0.3.183 needs clamp_free
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

# 6. The stock test client's manifest from before the digests (read only).
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
print('install_world_cache: digests, plan, migration, finished-staging swap, memory budgets, exit codes OK')
