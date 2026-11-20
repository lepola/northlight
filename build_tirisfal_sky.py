"""Import the tested Tirisfal cloud dome from an optional local HD archive."""
import hashlib
import struct

import client_archives
from build_lighting import DBC, Skip, copy_profile, profile_complete, putu, require, u
from build_outdoor_single_sun import sky_name, texture_names
from mpq import Archive

MODEL = r'Environments\Stars\legionnexus_volcanoskybox01.mdx'
# This reviewed model uses two skins, embedded animations and five local textures.
MODEL_SHA = 'e8f026fd8c8539b2ac68a333e9c879e5309a1813fdcbc085cb3e01d9a18b6fd2'
DEPENDENCIES = {
    'legionnexus_volcanoskybox0100.skin': '9c8436bc8ecade3a20d6a0698b439d18b2d75fb9c0011684fc94ff950e52a1f8',
    'legionnexus_volcanoskybox0101.skin': 'b773f8bd0091b5070be04d8278a4a20fd9a1e518418f9d2c97c1c6b93b42d59f',
    '7xp_felvolcano_cloud01.blp': 'bf7be972ba0846b6004ab97b228343d8f28189fe8e2546e30d620ee41a861263',
    '7xp_felvolcano_cloud02.blp': 'e2f0832031e70e08f073901d770b2e1691d2ea4dfe24938128b3d8226b9f3838',
    '7xp_felvolcano_dome01.blp': '9895788e5c1d0bb7ca66b7eb42c962b0ea2ea4688b2f2305cbeda91d2ee57639',
    '6tj_patch_fel_lightning_01.blp': 'bcfefa535da81e1563115931222ec441b728b4459c42b3898dd88ef99d5eccf4',
    'sky_bottom.blp': 'af25cc6fffeb9ed3287014b344f64cd268221f0abcf715a284746e9390869921',
}
SKY_TABLE = r'DBFilesClient\LightSkybox.dbc'


def import_dome(pack, tables, payload, assets):
    def dbc(name):
        try:
            table = DBC(pack.read('DBFilesClient\\' + name + '.dbc'))
        except (AssertionError, struct.error) as error:
            raise Skip(f'malformed donor {name}.dbc') from error
        required = {'Light': 15, 'LightParams': 3, 'LightSkybox': 3}
        require(table.fields >= required[name], f'incomplete donor {name}.dbc')
        return table

    light = tables['Light']
    params = tables['LightParams']
    donor_light, donor_params, donor_skies = dbc('Light'), dbc('LightParams'), dbc('LightSkybox')
    sky = DBC(payload[SKY_TABLE])
    slots = []
    flags = set()
    for rid in (27, 29, 53):
        r, donor = light.index.get(rid), donor_light.index.get(rid)
        require(r and donor and u(r, 1) == 0 and r[:20] == donor[:20],
                f'Tirisfal light volume {rid} does not match')
        for col in (7, 9):
            pid = u(r, col)
            source = donor_params.index.get(u(donor, col))
            donor_sky = donor_skies.index.get(u(source, 2)) if source else None
            require(donor_sky and sky_name(donor_skies, donor_sky).lower() == MODEL.lower(),
                    'archive does not assign the reviewed HD sky to these volumes')
            flags.add(u(donor_sky, 2))
            require(profile_complete(tables, pid), f'incomplete lighting profile {pid}')
            current = u(params.index[pid], 2)
            if current:
                require(current in sky.index and sky_name(sky, sky.index[current]).lower() == MODEL.lower(),
                        'Tirisfal already uses other custom sky artwork')
            else:
                slots.append((r, col, pid))
    require(flags == {3}, 'unrecognised HD sky flags')
    if not slots:
        return 'Tirisfal already uses the HD sky'

    stem = MODEL[:-4]
    model = pack.read(stem + '.m2')
    require(hashlib.sha256(model).hexdigest() == MODEL_SHA, 'unrecognised Tirisfal sky model')
    names, _ = texture_names(model)
    skins = [stem + f'{i:02}.skin' for i in range(struct.unpack_from('<I', model, 68)[0])]
    added = {}
    staged = {k.lower(): v for k, v in payload.items()}
    for name in [stem + '.m2', *skins, *names]:
        data = pack.read(name)
        if name != stem + '.m2':
            expected = DEPENDENCIES.get(name.lower().split('\\')[-1])
            require(hashlib.sha256(data).hexdigest() == expected, f'unrecognised HD asset {name}')
        existing = staged.get(name.lower())
        if existing is None:
            try:
                existing = assets.read(name)
            except FileNotFoundError:
                added[name] = data
                continue
        require(existing == data, f'conflicting local asset {name}')

    sid = next((i for i, row in sky.index.items()
                if sky_name(sky, row).lower() == MODEL.lower() and u(row, 2) == 3), None)
    if sid is None:
        sid = max(max(sky.index, default=0), max(u(row, 2) for row in params.rows)) + 1
        row = bytearray(donor_sky)
        putu(row, 0, sid)
        putu(row, 1, len(sky.strings))
        sky.strings += MODEL.encode() + b'\x00'
        sky.add(row)
    # Some Tirisfal storm profiles are shared with Outland. Change only these slots.
    next_id = max(max(params.index), (max(tables['LightIntBand'].index) + 17) // 18,
                  (max(tables['LightFloatBand'].index) + 5) // 6,
                  max(u(row, i) for row in light.rows for i in range(7, 15))) + 1
    clones = {}
    for row, col, pid in slots:
        if pid not in clones:
            new = next_id + len(clones)
            copy_profile(tables, pid, new)
            putu(params.index[new], 2, sid)
            clones[pid] = new
        putu(row, col, clones[pid])
    payload.update(added)
    payload[SKY_TABLE] = sky.bytes()
    return 'applied local HD sky to Tirisfal'


def apply(client, locale, tables, payload, assets):
    # A disabled archive is only a donor. It never enters the game's archive chain.
    candidates = ((client_archives.locale_dir(client, locale), f'patch-{locale.lower()}-x.mpq.disabled'),
                  (client_archives.data_dir(client), 'patch-x.mpq.disabled'))
    issues = []
    for folder, name in candidates:
        path = client_archives._entries(folder).get(name)
        if path and path.is_file():
            try:
                with Archive(path) as pack:
                    return import_dome(pack, tables, payload, assets)
            except (Skip, OSError, UnicodeDecodeError) as error:
                issues.append(f'{name}: {error}')
    if issues:
        return 'kept current Tirisfal sky (' + '; '.join(issues) + ')'
    return 'kept active Tirisfal artwork (no optional HD archive found)'
