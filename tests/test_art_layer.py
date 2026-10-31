#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""build_art_layer on the configured client (reads its MPQs and world-cache/fog; writes only to
the test output):
- stock view: the relighting plus the Mulgore and Stormwind bands. Mulgore (Light 201/202/234)
  changes colour bands 1-7 and 12 and fog bands 0-1 of its profile; Stormwind (Light 51/52/77)
  the fog end of its profiles. Both record their sky clone as skipped, every other HD sky step is
  skipped with a reason (not a KeyError), and the archive holds the four band tables, no sky clone
  and no moon02, plus weather textures, native attached-model lights and renderer-owned emitter
  metadata. World/selected Spells metadata and native model patches have disjoint owners; source
  hashes, canonical schema and the index digest match both archives and the manifest. Harvestable
  herbs and other supported gameobjects use native model lifetime, disjoint from world metadata. Against the relighting plus the retime, exactly
  those band rows differ, and every outdoor Light row differs in column 9 only (its private storm
  profile, see build_lighting.stormify; the storm step runs after the sky steps).
- both views (stock and the client's own chain), against the relighting alone: no band whose key
  times lie within {00:00, 12:00} changes, every changed band belongs to a profile the relighting
  created, every band holds <= 16 keys after all steps, and every band the retime moved without a
  skipped insert is night-like at 21:30 and 03:30.
- every view: each outdoor row's storm profile is private, unshared with any other slot, has <= 16
  keys, a fog end within the source storm's (x0.85, never below 350 yards unless shorter) and untouched water bands, and the skybox rule holds;
  the five BLPs in both archives are the generated ones (uncompressed, 1:16 rain, 1:2 snow) and the client's mist puffs at 1:4.
- the client's own chain: rebuilding twice without installed patch-Z produces identical
  payloads and archives; an older patch-Z may contain world-model lights that must be removed."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib, json, shutil, struct
from types import SimpleNamespace
import build_art_layer, build_lighting, build_weather_textures, client_archives
from build_lighting import DBC, u, f
from mpq import Archive
from world_scene_builder import Assets, decode_blp
import world_emitter_index

client = fp.client_root()
out = fp.output_dir()
cache = client / 'world-cache'
tables = {f'DBFilesClient\\{n}.dbc' for n in ('Light', 'LightParams', 'LightIntBand', 'LightFloatBand')}
blps = build_weather_textures.weather_textures()
_mist_assets = Assets(client, 'stock', client_archives.detect_locale(client, None), without='z')
blps.update(build_weather_textures.mist_textures(_mist_assets.read, decode_blp))   # 0.3.199 (rain mist): the client's puffs at 1:4
_mist_assets.close()
assert set(build_weather_textures.MIST) <= set(blps), 'the client has both mist puffs'
files = tables | set(blps)
report = {}
model_steps = {'world_model_lights', 'equipment_model_lights', 'spell_model_lights',
               'creature_model_lights', 'glue_model_lights', 'mount_model_lights', 'gameobject_model_lights'}
metadata = {world_emitter_index.ARCHIVE_PATH, build_art_layer.OWNER_FILE}


def model_checks(view, folder, result):
    """MPQ metadata owns World/selected Spells; native patches own disjoint attached models."""
    assets = Assets(client, view, result['locale'], without='z')
    view_textures = build_weather_textures.weather_textures()
    view_textures.update(build_weather_textures.mist_textures(assets.read, decode_blp))
    view_files = tables | set(view_textures)
    models = set(result['files']) - view_files - metadata
    assert metadata <= set(result['files']), result['files']
    assert all(n.lower().startswith(('item\\', 'spells\\', 'creature\\', 'interface\\', 'world\\')) and n.lower().endswith('.m2')
               for n in models), sorted(models)
    assert 'creature_model_lights' in {s['step'] for s in result['steps']}
    assert 'glue_model_lights' in {s['step'] for s in result['steps']}
    assert sum(s.get('models', 0) for s in result['steps'] if s['step'] in model_steps) == len(models)
    step, = [s for s in result['steps'] if s['step'] == 'world_emitter_index']
    try:
        payloads = []
        for target in result['targets']:
            with Archive(folder / target) as archive:
                dropped=set(result['dropped_unchanged'])
                assert {n for n in archive.names() if not n.startswith('(')} == ((view_files | models | metadata)-dropped) | {build_art_layer.OWNER_FILE}
                assert build_art_layer.owns_archive(folder / target)
                for name, data in view_textures.items():
                    assert (assets.read(name) if name in dropped else archive.read(name)) == data
                payload = archive.read(world_emitter_index.ARCHIVE_PATH)
                value = world_emitter_index.Index(SimpleNamespace(read=lambda _: payload))
                assert value.payload() == payload, 'Metadata must use canonical JSON'
                assert value.digest == step['sha256'] == result['files'][world_emitter_index.ARCHIVE_PATH]
                assert step['owner'] == 'northlight-local-direct'
                assert not set(value.models) & {n.lower() for n in models}, 'An emitter has two lighting owners'
                assert sum(len(v['emitters']) for v in value.models.values()) == step.get('emitters', 0)
                for path in value.models:
                    original = assets.read(path)
                    value.get(path, original)
                    assert not struct.unpack_from('<I', original, 264)[0], path
                for path in models:
                    blob = archive.read(path)
                    assert len(blob) >= 304 and blob[:4] == b'MD20', path
                    assert struct.unpack_from('<I', blob, 4)[0] == 264 and struct.unpack_from('<I', blob, 264)[0] > 0, path
                    assert hashlib.sha256(blob).hexdigest() == result['files'][path]
                payloads.append(payload)
        assert payloads[0] == payloads[1]
        return {'native_models': len(models), 'metadata_files': len(metadata),
                'metadata_models': len(value.models), 'metadata_emitters': step.get('emitters', 0),
                'metadata_sha256': value.digest}
    finally:
        assets.close()

stock = out / 'stock'
shutil.rmtree(stock, ignore_errors=True)
r = build_art_layer.build(client, stock, cache, archives='stock')
stock_models = model_checks('stock', stock, r)
skipped = {s['step']: s['skipped'] for s in r['steps'] if 'skipped' in s and s['step'] not in model_steps}
assert set(skipped) == {'orgrimmar', 'outdoor_sun', 'outdoor_moon'}, r['steps']
sky_skipped = {s['step']: s['sky_clone_skipped'] for s in r['steps'] if 'sky_clone_skipped' in s}
assert set(sky_skipped) == {'mulgore', 'stormwind'}, r['steps']
assert r['steps'][0]['profiles'] > 0 and not r['steps'][0]['skipped_incomplete_profiles']
for target in r['targets']:
    with Archive(stock / target) as a:
        for n, data in blps.items():   # the generated textures, ARGB at the aspect ratios the renderer detects
            assert a.read(n) == data
            w, h = decode_blp(data, 4096)[:2]
            if n in build_weather_textures.MIST:   # 0.3.199 (rain mist): 128x512, the renderer's mist signature
                assert data[8:11] == bytes((1, 8, 8)) and (w, h) == (128, 512), (n, w, h)
                continue
            assert data[8:11] == bytes((1, 8, 8)) and w <= 32 and h == (2*w if 'snow' in n.lower() else 16*w), (n, w, h)
def storm_checks(view, folder, r):
    """The storm profiles of one built view, on its real tables (new ids have no `before`, so check them here)."""
    assets = Assets(client, view, r['locale'], without='z')
    try:
        original = {n: DBC(assets.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
    finally:
        assets.close()
    with Archive(folder / r['targets'][1]) as a:
        built = {n: DBC(a.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
    step = next(s for s in r['steps'] if s['step'] == 'storm')
    light, params, ints, floats = [built[n] for n in build_lighting.TABLES]
    outdoor = [row for row in light.rows if u(row, 1) in build_lighting.MAPS]
    storm_ids = {u(row, 9) for row in outdoor}
    assert len(storm_ids) == step['profiles'] and len(outdoor) == step['rows'] and not step['rows_skipped'], step
    others = {u(row, c) for row in light.rows for c in range(7, 15) if c != 9} | {u(row, 9) for row in light.rows if u(row, 1) not in build_lighting.MAPS}
    assert min(storm_ids) >= step['first_id'] > max(original['LightParams'].index) and not storm_ids & others
    sky_rule = 0
    for row in outdoor:
        pid, clear = u(row, 9), u(row, 7)
        stock_row = original['Light'].index[u(row, 0)]
        assert u(row, 8) == u(stock_row, 8) and u(row, 10) == u(stock_row, 10)
        for table, n in ((ints, 18), (floats, 6)):
            assert all(0 <= u(table.index[(pid-1)*n+c+1], 1) <= 16 for c in range(n)), pid
        # Fog end: derived from the source storm only (a stock storm profile's own values, or the final clear's when stock storm == stock
        # clear), never from the row's clear: <= source at every key and >= min(source, 350 yards) x0.85-ish (bounds over the source's keys).
        end = floats.index[(pid-1)*6+1]
        if u(stock_row, 9) == u(stock_row, 7): src = [v for _, v in build_lighting.band_pairs(floats.index[(clear-1)*6+1], True)]
        else: src = [v for _, v in build_lighting.band_pairs(original['LightFloatBand'].index[(u(stock_row, 9)-1)*6+1], True)]
        lo, hi = min(min(src), build_lighting.STORM_FOG_END_FLOOR), max(src)
        for i in range(u(end, 1)):
            assert lo-1e-3 <= f(end, 18+i) <= hi+1e-3, (u(row, 0), pid, f(end, 18+i), lo, hi)
        if u(stock_row, 9) == u(stock_row, 7):   # a copy of the final clear profile
            assert u(params.index[pid], 2) == u(params.index[clear], 2)
            for ch in build_lighting.STORM_WATER:
                assert bytes(ints.index[(pid-1)*18+ch+1])[4:] == bytes(ints.index[(clear-1)*18+ch+1])[4:]
            sky_rule += 1
    return {'profiles': len(storm_ids), 'rows': len(outdoor), 'first_id': step['first_id'], 'from_clear_rows': sky_rule,
            'from_clear': step['from_clear'], 'from_stock': step['from_stock']}


# The relighting alone, from the same stock view, against the archive: only the two zones' bands.
assets = Assets(client, 'stock', r['locale'], without='z')
try:
    relit = {n: DBC(assets.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
finally:
    assets.close()
changes, _, _ = build_lighting.relight(relit)
build_lighting.retime(relit, [c['new'] for c in changes.values()])
with Archive(stock / r['targets'][1]) as a:
    built = {n: DBC(a.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
mulgore = {u(relit['Light'].index[i], 7) for i in (201, 202, 234)}
stormwind = {u(relit['Light'].index[i], 7) for i in (51, 52, 77)}
assert len(mulgore) == 1 and len(stormwind) == 2 and not mulgore & stormwind, (mulgore, stormwind)
(m,) = mulgore
want = {'Light': set(), 'LightParams': set(),
        'LightIntBand': {(m-1)*18+c+1 for c in (1, 2, 3, 4, 5, 6, 7, 12)},
        'LightFloatBand': {(m-1)*6+1, (m-1)*6+2} | {(p-1)*6+1 for p in stormwind}}
for n, table in relit.items():
    assert table.strings == built[n].strings and set(table.index) <= set(built[n].index), n
    changed = {i for i, row in table.index.items() if built[n].index[i] != row}
    if n == 'Light':   # the storm slot only, on every outdoor row
        outdoor = {i for i, row in table.index.items() if u(row, 1) in build_lighting.MAPS}
        assert changed == outdoor and all(table.index[i][:36] == built[n].index[i][:36] and table.index[i][40:] == built[n].index[i][40:] for i in changed)
    else:
        assert changed == want[n], (n, sorted(changed), sorted(want[n]))
report['stock'] = {'storm': storm_checks('stock', stock, r), 'profiles': r['steps'][0]['profiles'], 'skipped': skipped, 'sky_clone_skipped': sky_skipped,
                   'mulgore_profile': m, 'stormwind_profiles': sorted(stormwind),
                   'changed_band_rows': {n: sorted(v) for n, v in want.items() if v}, 'files': sorted(r['files']), **stock_models}


def night_like(pairs, floating, time):
    night = build_lighting.band_value(pairs, floating, 0)
    spread = max(build_lighting.band_difference(v if floating else build_lighting.rgb(v), night, floating) for _, v in pairs)
    tolerance = max(1e-3, .1*spread) if floating else max(3, .1*spread)
    return build_lighting.band_difference(build_lighting.band_value(pairs, floating, time), night, floating) <= tolerance


def real_tables(view, folder, r):
    """The retime's scope on the client's real tables, for one archive view."""
    assets = Assets(client, view, r['locale'], without='z')
    try:
        relit = {n: DBC(assets.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
    finally:
        assets.close()
    changes, _, _ = build_lighting.relight(relit)
    created = {c['new'] for c in changes.values()}
    with Archive(folder / r['targets'][1]) as a:
        built = {n: DBC(a.read(f'DBFilesClient\\{n}.dbc')) for n in build_lighting.TABLES}
    retime = next(s for s in r['steps'] if s['step'] == 'retime')
    skipped_insert = {f"{s['table']}:{s['id']}" for s in retime['insert_skipped']}
    result = {'retimed': retime['retimed'], 'unchanged': retime['unchanged'], 'inserted_keys': retime['inserted_keys'],
              'insert_skipped': len(skipped_insert)}
    for name, floating, channels in (('LightIntBand', False, 18), ('LightFloatBand', True, 6)):
        for id, row in built[name].index.items():
            assert 0 <= u(row, 1) <= 16, (name, id)
            before = relit[name].index.get(id)
            if before is None or before == row:   # a new id: the storm profiles, see storm_checks
                continue
            times = {u(before, 2+i) for i in range(u(before, 1))}
            assert not times <= {0, 1440}, (name, id, sorted(times))   # two-key zones (Tirisfal) unchanged
            assert (id-1)//channels+1 in created, (name, id)         # only the relighting's private profiles
        for key in r['retimed_rows']:
            table, id = key.split(':')
            if table != name or key in skipped_insert:
                continue
            pairs = build_lighting.band_pairs(built[name].index[int(id)], floating)
            assert night_like(pairs, floating, 2580) and night_like(pairs, floating, 420), (key, pairs)
    return result


report['stock']['retime'] = real_tables('stock', stock, r)
own = out / 'own'
shutil.rmtree(own, ignore_errors=True)
r_own = build_art_layer.build(client, own, cache)
own_models = model_checks('all', own, r_own)
report['own_retime'] = real_tables('all', own, r_own)
report['own_storm'] = storm_checks('all', own, r_own)
# A new installer removes world-model light injection from an older patch-Z.
# Rebuilding from the same original archive chain must still be reproducible.
again = out / 'own-again'
again.mkdir(exist_ok=True)
r_again = build_art_layer.build(client, again, cache)
assert model_checks('all', again, r_again) == own_models
assert r_again['files'] == r_own['files']
assert any(s['step'] == 'gameobject_model_lights' and s.get('models', 0) > 0 for s in r_own['steps'])
got = [hashlib.sha256((own / t).read_bytes()).hexdigest() for t in r_own['targets']]
assert got == [r_again['archive_sha256']] * 2
report['own'] = {'rebuilt_sha256': got, **own_models}
# Optional sky resources may be absent, but skipped steps must explain why.
assert all(isinstance(s[k], str) and s[k] for s in r_own['steps'] for k in ('skipped', 'sky_clone_skipped') if k in s)
(out / 'art-layer.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
