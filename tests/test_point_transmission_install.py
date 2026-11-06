#!/usr/bin/env python3
# northlight-test:
"""Point-transmission cache transactions and package paths, using temporary fixtures only."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_packages as packages
import install_world_cache as install
import instance_maps

REBUILT, REMOVED, KEPT = 'Shadowfang', 'StormwindJail', "Zul'gurub"
INDEX = 'world-emitters-v1.json'
MODEL = 'world\\generic\\human\\passive doodads\\candelabras\\generalcandelabra01.m2'


def key(name):
    return hashlib.sha256(('FGS3-point-transmission-v1:world\\fixture\\' + name + '.m2').encode()).digest()


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def document(path, data):
    return put(path, (json.dumps(data, indent=2) + '\n').encode())


def tile(path, *keys):
    # FGS3/v3: exact 116-byte records, UID/category, identity transform and finite bounds.
    transform = (1, 0, 0, 0, 1, 0, 0, 0, 1, 4, 5, 6)
    bounds = (4, 5, 6, 5, 6, 6)
    return put(path, struct.pack('<4sII', b'FGS3', 3, len(keys)) + b''.join(
        struct.pack('<32sQI18f', value, 1000 + i, 1, *bounds, *transform) for i, value in enumerate(keys)))


def point_tile(lights, map_name, *keys, name='1_2.fg3'):
    assert map_name in [*install.MAPS, *instance_maps.ALL_MAPS]
    return tile(lights / 'point-transmission' / map_name / name, *keys)


def model(lights, value, colour=128):
    vertices = ((0, 0, 0, 0, 0, 1, 0, 0), (1, 0, 0, 0, 0, 1, 1, 0), (0, 1, 0, 0, 0, 1, 0, 1))
    rgba = bytes((colour, colour, colour, 255, colour, colour, colour, 0)) * 2
    payload = (struct.pack('<4s4I', b'FGS2', 2, 3, 1, 1) +
               b''.join(struct.pack('<8f', *v) for v in vertices) + struct.pack('<4I', 0, 1, 2, 0) +
               struct.pack('<3fIIfI', 1, 1, 1, 2, 2, .5, len(rgba)) + rgba)
    return put(lights / 'point-transmission/models' / (value.hex() + '.fgs'), payload)


def profile(tag):
    return {'asset_sha256': hashlib.sha256(tag.encode()).hexdigest(), 'basis': 'reviewed',
            'emitters': [{'position': [0, 0, 1], 'rgb': [1, .5, .2], 'intensity': .8,
                          'start': .5, 'end': 12, 'emission': 'candle'}]}


def index(lights, models, **fields):
    return document(lights / INDEX, {'version': 1, 'owner': 'northlight-local-direct', 'models': models, **fields})


class PointTransmissionInstall(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=fp.output_dir())
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue({REBUILT, REMOVED, KEPT} <= set(instance_maps.ALL_MAPS))

    def test_merge_preserves_current_map_and_models_and_moves_missing_map(self):
        old, new = self.root / 'old', self.root / 'new'
        old_key, fresh_key, shared = key('old'), key('fresh'), key('shared')
        source_kept = point_tile(old, REBUILT, old_key)
        source_missing = point_tile(old, REMOVED, shared)
        current = point_tile(new, REBUILT, fresh_key)
        unrelated = point_tile(new, 'Northrend', shared)
        current_bytes, unrelated_bytes = current.read_bytes(), unrelated.read_bytes()
        model(old, old_key)
        model(new, fresh_key)
        source_model = model(old, shared, 64)
        existing_model = model(new, shared, 192)
        existing_bytes = existing_model.read_bytes()
        install.merge_point_transmission(old, new, [REBUILT, REMOVED])
        self.assertEqual(current.read_bytes(), current_bytes)
        self.assertEqual(unrelated.read_bytes(), unrelated_bytes)
        self.assertTrue(source_kept.exists())
        self.assertFalse(source_missing.exists())
        self.assertEqual(install.tile_keys(new / 'point-transmission' / REMOVED), {shared.hex()})
        self.assertEqual(existing_model.read_bytes(), existing_bytes)
        self.assertTrue(source_model.exists(), 'A current shared model must not be overwritten on carry')

    def test_replace_removes_absent_map_and_preserves_unrelated_tiles(self):
        old, new = self.root / 'staged', self.root / 'installed'
        fresh, stale, kept = key('fresh'), key('stale'), key('kept')
        point_tile(old, REBUILT, fresh, name='3_4.fg3')
        fresh_model = model(old, fresh)
        fresh_bytes = fresh_model.read_bytes()
        point_tile(new, REBUILT, stale)
        point_tile(new, REMOVED, stale)
        untouched = [point_tile(new, name, kept) for name in ('Northrend', KEPT)]
        model(new, stale)
        model(new, kept)
        before = [p.read_bytes() for p in untouched]
        install.merge_point_transmission(old, new, [REBUILT, REMOVED], replace=True)
        self.assertEqual(install.tile_keys(new / 'point-transmission' / REBUILT), {fresh.hex()})
        self.assertFalse((new / 'point-transmission' / REBUILT / '1_2.fg3').exists())
        self.assertFalse((new / 'point-transmission' / REMOVED).exists())
        self.assertEqual([p.read_bytes() for p in untouched], before)
        self.assertEqual((new / 'point-transmission/models' / fresh_model.name).read_bytes(), fresh_bytes)

    def test_carry_instance_lights_keeps_new_continents_and_is_repeatable(self):
        old, new = self.root / 'old-lights', self.root / 'new-lights'
        value = key('carried')
        for name in (REBUILT, REMOVED, 'Northrend'):
            put(old / (name + '.fgl'), ('old-' + name).encode())
            point_tile(old, name, value)
        current = put(new / (REBUILT + '.fgl'), b'fresh-instance')
        continent = put(new / 'Northrend.fgl', b'fresh-continent')
        point_tile(new, REBUILT, key('fresh'))
        point_tile(new, 'Northrend', key('continent'))
        model(old, value)
        model(new, key('fresh'))
        model(new, key('continent'))
        manifest = document(old / install.INSTANCE_MANIFEST, {'maps': [{'map': REMOVED}]})
        for _ in range(2):
            install.carry_instance_lights(old, new)
            self.assertEqual(current.read_bytes(), b'fresh-instance')
            self.assertEqual(continent.read_bytes(), b'fresh-continent')
            self.assertEqual((new / (REMOVED + '.fgl')).read_bytes(), ('old-' + REMOVED).encode())
            self.assertEqual(install.tile_keys(new / 'point-transmission' / REMOVED), {value.hex()})
        self.assertFalse(manifest.exists())
        self.assertTrue((new / install.INSTANCE_MANIFEST).is_file())
        self.assertTrue((old / 'Northrend.fgl').exists())
        self.assertTrue((old / 'point-transmission/Northrend').is_dir())

    def test_index_merge_is_canonical_preserves_current_profile_and_updates_digests(self):
        old, new = self.root / 'old', self.root / 'new'
        old_only, new_only = 'world\\fixture\\old.m2', 'world\\fixture\\new.m2'
        index(old, {MODEL: profile('old'), old_only: profile('old-only')})
        index(new, {MODEL: profile('new'), new_only: profile('new-only')})
        for name in ('manifest.json', install.INSTANCE_MANIFEST):
            document(new / name, {'maps': [{'map': KEPT}], 'world_emitter_index': {'sha256': 'before', 'models': 3}})
        install.merge_point_transmission(old, new, [])
        expected = {'version': 1, 'owner': 'northlight-local-direct',
                    'models': {MODEL: profile('new'), old_only: profile('old-only'), new_only: profile('new-only')}}
        payload = (new / INDEX).read_bytes()
        self.assertEqual(payload, json.dumps(expected, sort_keys=True, separators=(',', ':'), allow_nan=False).encode())
        for name in ('manifest.json', install.INSTANCE_MANIFEST):
            manifest = json.loads((new / name).read_text())
            self.assertEqual(manifest['world_emitter_index']['sha256'], hashlib.sha256(payload).hexdigest())
            self.assertEqual(manifest['maps'], [{'map': KEPT}])

    def test_replacement_index_uses_fresh_profile(self):
        old, new = self.root / 'fresh', self.root / 'installed'
        index(old, {MODEL: profile('fresh-asset')})
        index(new, {MODEL: profile('stale-asset')})
        install.merge_point_transmission(old, new, [REBUILT], replace=True)
        self.assertEqual(json.loads((new / INDEX).read_text())['models'][MODEL], profile('fresh-asset'))

    def test_both_index_owners_and_versions_are_validated(self):
        for side in ('source', 'destination'):
            for fields in ({'owner': 'another-owner'}, {'version': 2}):
                with self.subTest(side=side, fields=fields):
                    root = self.root / (side + '-' + next(iter(fields)))
                    old, new = root / 'old', root / 'new'
                    index(old, {MODEL: profile('old')}, **(fields if side == 'source' else {}))
                    index(new, {MODEL: profile('new')}, **(fields if side == 'destination' else {}))
                    with self.assertRaises(ValueError):
                        install.merge_point_transmission(old, new, [])

    def transaction(self):
        staging, output = self.root / 'staging', self.root / 'cache'
        old, new = staging / 'lights', output / 'lights'
        values = {name: key(name) for name in ('shared', 'continent', 'kept', 'stale', 'deleted', 'orphan', 'fresh')}
        point_tile(new, 'Northrend', values['shared'], values['continent'])
        point_tile(new, KEPT, values['shared'], values['kept'])
        point_tile(new, REBUILT, values['stale'])
        point_tile(new, REMOVED, values['shared'], values['deleted'])
        point_tile(old, REBUILT, values['fresh'], values['shared'], name='3_4.fg3')
        for name, value in values.items():
            model(old if name == 'fresh' else new, value)
        for name in ('Northrend', KEPT, REBUILT, REMOVED):
            tile(output / name / '1_2.fg3', key('native-' + name))
            put(new / (name + '.fgl'), ('light-' + name).encode())
        tile(staging / REBUILT / '3_4.fg3', key('native-fresh'))
        put(old / (REBUILT + '.fgl'), b'fresh-light')
        index(new, {MODEL: profile('kept')})
        index(old, {'world\\fixture\\fresh.m2': profile('fresh')})
        for lights, maps in ((new, [REBUILT, REMOVED, KEPT]), (old, [REBUILT])):
            payload = (lights / INDEX).read_bytes()
            document(lights / install.INSTANCE_MANIFEST, {'maps': [{'map': m} for m in maps],
                     'world_emitter_index': {'sha256': hashlib.sha256(payload).hexdigest()}})
        document(staging / install.INSTANCE_READY,
                 {'drop_maps': [REMOVED], 'built_maps': [REBUILT], 'prune': [],
                  'manifest': {'format': install.INSTANCE_FORMAT, 'groups': {
                      'classic-dungeons': {'maps_built': [REBUILT]}, 'classic-raids': {'maps_built': [KEPT]}}}})
        return staging, output, values

    def test_apply_instances_replaces_deletes_preserves_shared_and_prunes_orphans(self):
        staging, output, values = self.transaction()
        untouched = [output / name / '1_2.fg3' for name in ('Northrend', KEPT)]
        untouched += [output / 'lights/point-transmission' / name / '1_2.fg3' for name in ('Northrend', KEPT)]
        untouched += [output / 'lights' / (name + '.fgl') for name in ('Northrend', KEPT)]
        before = [p.read_bytes() for p in untouched]
        shared = output / 'lights/point-transmission/models' / (values['shared'].hex() + '.fgs')
        shared_bytes = shared.read_bytes()
        install.apply_instances(staging, output)
        self.assertFalse(staging.exists())
        self.assertFalse((output / REMOVED).exists())
        self.assertFalse((output / 'lights' / (REMOVED + '.fgl')).exists())
        self.assertFalse((output / 'lights/point-transmission' / REMOVED).exists())
        self.assertEqual([p.read_bytes() for p in untouched], before)
        self.assertEqual(shared.read_bytes(), shared_bytes)
        self.assertEqual(install.tile_keys(output / 'lights/point-transmission' / REBUILT),
                         {values['fresh'].hex(), values['shared'].hex()})
        actual = {p.stem for p in (output / 'lights/point-transmission/models').glob('*.fgs')}
        self.assertEqual(actual, {values[name].hex() for name in ('shared', 'continent', 'kept', 'fresh')})
        self.assertEqual((output / 'lights' / (REBUILT + '.fgl')).read_bytes(), b'fresh-light')

    def test_apply_instances_manifest_uses_final_merged_index_digest(self):
        staging, output, _ = self.transaction()
        install.apply_instances(staging, output)
        payload = (output / 'lights' / INDEX).read_bytes()
        manifest = json.loads((output / 'lights' / install.INSTANCE_MANIFEST).read_text())
        self.assertEqual(manifest['world_emitter_index']['sha256'], hashlib.sha256(payload).hexdigest())
        self.assertEqual({row['map'] for row in manifest['maps']}, {REBUILT, KEPT})

    def test_unknown_and_traversal_maps_are_rejected(self):
        sentinel = put(self.root / 'outside/keep.txt', b'unchanged')
        for name in ('../outside', '..\\outside', REBUILT + '/../../outside', 'UnknownInstance', 'Northrend'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                install.merge_point_transmission(self.root / 'old', self.root / 'new', [name], replace=True)
        self.assertEqual(sentinel.read_bytes(), b'unchanged')

    def cache_zip(self, entries):
        files = {name: {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)} for name, data in entries.items()}
        rows = ''.join(f"{name}\0{meta['sha256']}\0{meta['bytes']}\n" for name, meta in sorted(files.items()))
        manifest = {'format': 'northlight-cache-variant/1', 'variant': 'fixture', 'files': files,
                    'cache_digest': hashlib.sha256(rows.encode()).hexdigest()}
        path = self.root / 'cache.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
            archive.writestr('northlight-cache.json', json.dumps(manifest))
        return path, manifest

    def test_package_allowlist_accepts_point_cache_and_index(self):
        names = [f'lights/point-transmission/{name}/1_2.fg3' for name in ('Northrend', REBUILT, KEPT)]
        names += ['lights/point-transmission/models/' + key('table').hex() + '.fgs', 'lights/' + INDEX]
        for name in names:
            self.assertIsNotNone(packages.CACHE_MEMBER.match(name), name)
        packages.check_cache_zip(*self.cache_zip({name: b'fixture' for name in names}))

    def test_package_allowlist_rejects_traversal_unknown_and_noncanonical_entries(self):
        valid = 'lights/point-transmission/models/' + key('table').hex() + '.fgs'
        bad = [valid + '\n', valid + '/extra', './' + valid, '/' + valid, valid.replace('/', '\\'),
               'lights/point-transmission/../Azeroth/1_2.fg3',
               'lights/point-transmission/Northrend/../../outside.fg3',
               'lights/point-transmission/models/' + 'a' * 63 + '.fgs',
               'lights/point-transmission/models/' + 'A' * 64 + '.fgs',
               'lights/point-transmission/report.json', 'lights/point-transmission/Northrend/1_2.exe',
               'lights/unknown-cache.bin']
        for name in bad:
            with self.subTest(name=name):
                self.assertIsNone(packages.CACHE_MEMBER.match(name), name)

    def test_cache_zip_rejects_unknown_and_newline_members_before_manifest_checks(self):
        for name in ('lights/point-transmission/report.json', 'lights/point-transmission/Shadowfang/1_2.fg3\n'):
            with self.subTest(name=name), self.assertRaisesRegex(SystemExit, 'outside the cache allowlist'):
                packages.check_cache_zip(*self.cache_zip({name: b'fixture'}))


if __name__ == '__main__':
    unittest.main()
