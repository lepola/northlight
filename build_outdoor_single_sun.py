"""Remove dedicated HD solar batches from clear-weather outdoor sky clones.

No runtime geometry, lighting curves, fog, clouds, moons or original models
are changed. Input is the installed lighting patch, including city fixes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from mpq import Archive
from build_lighting import DBC, u, putu, require

CLIENT = Path(__file__).resolve().parent.parent
MAPS = {0, 1, 530, 571}
# Exact texture basenames, verified from the active assets. Never substring
# match "sun": sunny/sunset clouds, azsuna and moon flares must survive.
ORGRI = {'orgrisunray01.blp', 'orgrisunflare01.blp', 'orgrilensflare02.blp'}
POGOST = {'arathisunflare.blp', 'pogost_sun.blp'}
AZSUNA = {'uldumsunflare.blp', 'lazur_sun.blp', 'lazur_sunset_rays01.blp'}
MULGORE = {'mulgor_sun.blp', 'mulgor_lensflare01.blp', 'mulgor_sunflare01.blp',
           'mulgor_sunflare02.blp', 'mulgor_sunray01.blp'}
OMIT = {
    23: POGOST,
    95: POGOST | {'nagrandsunset_rays01.blp', 'arathi_sunray03.blp'},
    155: AZSUNA, 156: AZSUNA,
    157: {'if_sun_sun_sun1.blp'},
    158: {'uldumsunflare.blp', 'veb_sundisk.blp'},
    165: ORGRI,
    170: {'stormwind_sun.blp', 'stormwind_sun_sunset.blp'},
    172: MULGORE,
    178: {'orgrisunray01.blp', 'mulgor_sun.blp', 'orgrilensflare02.blp'},
    179: ORGRI, 180: ORGRI,
    # The .bl spelling is present in the original model (missing texture).
    181: {'arathi_sunflare02.blp', 'arathi_sun.blp', 'arathisunflare.bl',
          'arathi_sunray01.blp', 'arathi_sunray02.blp', 'arathi_sunray03.blp'},
    184: POGOST, 185: POGOST, 186: POGOST, 187: POGOST,
    188: POGOST, 189: POGOST, 190: POGOST,
}


def sky_name(table, row):
    return table.strings[u(row, 1):].split(b'\0', 1)[0].decode()


def texture_names(model):
    count, offset = struct.unpack_from('<II', model, 80)
    require(offset + 16*count <= len(model), 'sky texture table extends beyond the model')
    textures = []
    for i in range(count):
        kind, flags, size, pos = struct.unpack_from('<4I', model, offset+16*i)
        require(kind != 0 or pos + size <= len(model),
                'unrecognised sky texture name')
        textures.append(model[pos:pos+size].rstrip(b'\0').decode().lower()
                        if kind == 0 else '')
    count, offset = struct.unpack_from('<II', model, 128)
    require(offset + 2*count <= len(model), 'sky texture lookup extends beyond the model')
    lookup = struct.unpack_from(f'<{count}H', model, offset)
    return textures, lookup


def batch_names(raw, textures, lookup):
    batch = struct.unpack('<2B11H', raw)
    count, start = batch[8:10]
    return [textures[lookup[k]].split('\\')[-1]
            for k in range(start, start+count)]


def separate_solar_batches(before, textures, lookup, omit, skin_id):
    count, offset = struct.unpack_from('<II', before, 36)
    require(before[:4] == b'SKIN' and 48 <= offset <= len(before) - 24*count,
            'unrecognised sky batches')
    kept, retained, removed = [], [], []
    for j in range(count):
        raw = before[offset+24*j:offset+24*(j+1)]
        names = batch_names(raw, textures, lookup)
        record = dict(skin=skin_id, batch=j, textures=names)
        if omit.intersection(names):
            require(set(names) <= omit, 'sun and other artwork share a batch')
            removed.append(record)
        else:
            kept.append(raw)
            retained.append(record)
    require(kept, 'no independent sky artwork to retain')
    skin = bytearray(before)
    putu(skin, 9, len(kept))
    skin[offset:offset+24*count] = b''.join(kept) + bytes(24*len(removed))
    return bytes(skin), removed, retained


def build(source, output, assets=None, kept_sky=None):
    """assets: a world_scene_builder.Assets of the client (default: the configured client).
    Raises build_lighting.Skip when an HD sky of OMIT is absent."""
    with Archive(source) as archive:
        original = {n: archive.read(n) for n in archive.names() if not n.startswith('(')}
    payload = dict(original)
    lower = {n.lower(): data for n, data in original.items()}
    keys = {n: 'DBFilesClient\\'+n+'.dbc'
            for n in ('Light', 'LightParams', 'LightSkybox')}
    require(set(keys.values()) <= set(original), 'lighting input lacks LightSkybox')
    light, params, sky = [DBC(original[keys[n]]) for n in keys]
    owned = assets is None
    if owned:
        sys.path.insert(0, str(Path(__file__).resolve().parent/'renderer'))   # this repository's renderer/
        from world_scene_builder import Assets
        assets = Assets()

    def read(name):
        return lower[name.lower()] if name.lower() in lower else assets.read(name)

    refs = {}
    for row in light.rows:
        profile = u(row, 7)
        if u(row, 1) in MAPS and profile in params.index:
            sid = u(params.index[profile], 2)
            if sid:
                refs.setdefault(sid, []).append((u(row, 0), profile))
    solar = {sid: names for sid, names in OMIT.items() if sid != kept_sky}
    try:
        require(set(solar) <= set(refs), f'HD outdoor skies absent: {sorted(set(solar) - set(refs))}')
        for sid, omit in solar.items():
            stem = sky_name(sky, sky.index[sid])[:-4]
            require(stem.lower()+'.m2' in lower or stem.lower()+'.m2' in assets.providers, f'sky {sid} model absent')
            textures, _ = texture_names(read(stem+'.m2'))
            names = {t.split('\\')[-1] for t in textures}
            require(bool(omit & names) if omit == MULGORE else omit <= names,
                    f'sky {sid} has no HD solar batches')
    except BaseException:
        if owned:
            assets.close()
        raise
    output.mkdir(parents=True, exist_ok=True)
    profiles = {p for sid in solar for _, p in refs[sid]}
    # No selected profile may also control a dungeon, underwater/weather slot,
    # or other map. Fail instead of silently expanding the scope.
    for row in light.rows:
        for col in range(7, 15):
            if u(row, col) in profiles:
                assert col == 7 and u(row, 1) in MAPS

    changes = []
    try:
        for sid, omit in sorted(solar.items()):
            name = sky_name(sky, sky.index[sid])
            stem = name[:-4]
            custom = f'Environments\\Stars\\fr_outdoor_single_sun_{sid}'
            assert custom.lower()+'.m2' not in lower
            model = read(stem+'.m2')
            payload[custom+'.m2'] = model
            textures, lookup = texture_names(model)
            skins = struct.unpack_from('<I', model, 68)[0]
            assert 1 <= skins <= 4
            removed, retained = [], []
            for skinid in range(skins):
                before = read(stem+f'{skinid:02d}.skin')
                skin, dropped, kept = separate_solar_batches(before, textures, lookup, omit, skinid)
                if omit != MULGORE:
                    require({t for b in dropped for t in b['textures']} == omit,
                            f'sky {sid} lacks its expected solar artwork')
                removed.extend(dropped)
                retained.extend(kept)
                payload[custom+f'{skinid:02d}.skin'] = skin
            require(removed, f'sky {sid} has no dedicated sun artwork')
            # Retain external animation dependencies under the clone name.
            for asset in assets.providers:
                if asset.startswith(stem.lower()) and asset.endswith('.anim'):
                    payload[custom+asset[len(stem):]] = read(asset)
            row = bytearray(sky.index[sid])
            new_id = max(sky.index)+1
            putu(row, 0, new_id)
            putu(row, 1, len(sky.strings))
            sky.strings += (custom+'.mdx').encode()+b'\0'
            sky.add(row)
            affected = sorted({p for _, p in refs[sid]})
            for profile in affected:
                putu(params.index[profile], 2, new_id)
            changes.append(dict(original_sky=sid, cloned_sky=new_id, original_model=name,
                                clone=custom, profiles=affected,
                                lights=sorted(i for i, _ in refs[sid]),
                                removed_batches=removed, retained_batches=retained))
    finally:
        if owned:
            assets.close()
    for table, current in [('LightParams', params), ('LightSkybox', sky)]:
        baseline = DBC(original[keys[table]])
        for id, old in baseline.index.items():
            new = current.index[id]
            if table == 'LightParams' and id in profiles:
                assert new[:8] == old[:8] and new[12:] == old[12:]
            else:
                assert old == new
        payload[keys[table]] = current.bytes()
    changed = sorted(n for n in original if original[n] != payload[n])
    assert set(changed) == {keys['LightParams'], keys['LightSkybox']}
    archive = output/'patch-z.mpq'
    assert not archive.exists(), 'Use a fresh output directory'
    with Archive(archive, create=True, capacity=max(16, len(payload)*2)) as target:
        for i, (name, data) in enumerate(payload.items()):
            temp = output/f'payload-{i}.bin'
            temp.write_bytes(data)
            target.add(temp, name)
            temp.unlink()
    with Archive(archive) as target:
        for name, data in payload.items():
            assert target.read(name) == data
    report = dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                  inspected_outdoor_sky_ids=sorted(refs), modified_profiles=sorted(profiles),
                  changes=changes, changed_existing_assets=changed,
                  added_assets=sorted(set(payload)-set(original)),
                  unrelated_assets_and_lighting_identical=True,
                  original_models_and_retained_batches_identical=True,
                  game_launched=False, visual_verified=False)
    (output/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Checked {len(refs)} outdoor skies; cloned {len(changes)}; '
          f'remapped {len(profiles)} profiles; removed '
          f'{sum(len(c["removed_batches"]) for c in changes)} solar batches.')
    print('MPQ SHA256:', report['archive_sha256'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
