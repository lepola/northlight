#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""WORLD discovery uses real luminous triangles, absolute energy and safe anchors."""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'renderer')]
from world_emitter_coverage import inspect_model, consolidate, calibrated_intensity, map_records, census
from m2_emitter_patch import append_lights
from northlight_paths import output_dir


class Assets:
    def __init__(self, files):
        self.files = files
        self.providers = dict.fromkeys(files)
    def read(self, path):
        if path not in self.files:
            raise FileNotFoundError(path)
        return self.files[path]
    def origin(self, path):
        return 'fixture.mpq'
    def fingerprint(self):
        return {}
    def warnings(self):
        return []
    def probe_maps(self, maps):
        pass


def fixture(brightness=255, opacity=32767, blend=3, centers=(0.,), animated=False):
    blob = bytearray(304)
    blob[:4] = b'MD20'; struct.pack_into('<I', blob, 4, 264)
    def add(data):
        offset = len(blob); blob.extend(data); return offset
    empty = struct.pack('<hh4I', 0, -1, 0, 0, 0, 0)
    bone = struct.pack('<iIhHI', -1, 8 if animated else 0, -1, 0, 0) + empty*3 + struct.pack('<3f', 0, 0, 0)
    struct.pack_into('<II', blob, 44, 1, add(bone))
    verts = []
    for x in centers:
        for point in ((x,0,2), (x+1,0,2), (x,1,2)):
            verts.append(struct.pack('<3f4B4B3f4f', *point, 255,0,0,0, 0,0,0,0, 0,0,1, 0,0,0,0))
    struct.pack_into('<II', blob, 60, len(verts), add(b''.join(verts)))
    name = b'world\\textures\\blue.blp\0'; name_offset = add(name)
    struct.pack_into('<II', blob, 80, 1, add(struct.pack('<4I', 0,0,len(name),name_offset)))
    struct.pack_into('<II', blob, 112, 1, add(struct.pack('<2H', 1, blend)))
    struct.pack_into('<II', blob, 128, 1, add(struct.pack('<H', 0)))
    # A real constant texture-weight track at a non-default opacity.
    stamp = add(struct.pack('<I', 0)); value = add(struct.pack('<h', opacity))
    stamps = add(struct.pack('<II', 1, stamp)); values = add(struct.pack('<II', 1, value))
    track = add(struct.pack('<hh4I', 0,-1,1,stamps,1,values))
    struct.pack_into('<II', blob, 88, 1, track)
    struct.pack_into('<II', blob, 144, 1, add(struct.pack('<H', 0)))
    skin = bytearray(48); skin[:4] = b'SKIN'
    def skinadd(data):
        offset = len(skin); skin.extend(data); return offset
    count = len(verts)
    struct.pack_into('<II', skin, 4, count, skinadd(struct.pack('<%dH' % count, *range(count))))
    struct.pack_into('<II', skin, 12, count, skinadd(struct.pack('<%dH' % count, *range(count))))
    subset = struct.pack('<10H7f', 0,0,0,count,0,count,0,0,0,0, *([0.]*7))
    struct.pack_into('<II', skin, 28, 1, skinadd(subset))
    unit = struct.pack('<2B11H', 0,0, 0,0,0,65535,0,0,1,0,0,0,0)
    struct.pack_into('<II', skin, 36, 1, skinadd(unit))
    texture = bytearray(152); texture[:4] = b'BLP2'
    struct.pack_into('<4B', texture, 8, 3,8,0,1); struct.pack_into('<II', texture, 12, 1,1)
    struct.pack_into('<I', texture, 20, 148); struct.pack_into('<I', texture, 84, 4)
    texture[148:] = bytes((brightness,brightness//2,brightness//4,255))
    return Assets({'world\\anonymous.m2': bytes(blob), 'world\\anonymous00.skin': bytes(skin),
                   'world\\textures\\blue.blp': bytes(texture)})


def particle_fixture(rate=2., enabled=1):
    assets = fixture(); blob = bytearray(assets.read('world\\anonymous.m2'))
    struct.pack_into('<II', blob, 112, 0,0)  # only the particle emits
    offset = len(blob); blob.extend(bytes(476)); struct.pack_into('<II', blob, 296, 1,offset)
    struct.pack_into('<3f', blob, offset+8, 0,0,3)
    struct.pack_into('<hH', blob, offset+20, 0,0)
    struct.pack_into('<BBh', blob, offset+40, 4,2,-1)
    def add(data):
        start = len(blob); blob.extend(data); return start
    def track(relative, fmt, value):
        stamp = add(struct.pack('<I',0)); key = add(struct.pack(fmt,*value))
        stamps = add(struct.pack('<II',1,stamp)); keys = add(struct.pack('<II',1,key))
        struct.pack_into('<hh4I',blob,offset+relative,0,-1,1,stamps,1,keys)
    def lifetime(relative, fmt, value):
        stamp = add(struct.pack('<H',0)); key = add(struct.pack(fmt,*value))
        struct.pack_into('<4I',blob,offset+relative,1,stamp,1,key)
    track(152,'<f',(1.,)); track(176,'<f',(rate,)); track(456,'<B',(enabled,))
    lifetime(260,'<3f',(255.,255.,255.)); lifetime(276,'<h',(32767,)); lifetime(292,'<2f',(.5,.5))
    assets.files['world\\anonymous.m2'] = bytes(blob)
    return assets


class Discovery(unittest.TestCase):
    def test_unnamed_model_and_above_table_triangle_centroid(self):
        result = inspect_model(fixture(), 'world\\anonymous.m2')
        self.assertEqual(result['status'], 'addable')
        light, = result['emitters']
        self.assertEqual(light['position'], (1/3, 1/3, 2.))
        self.assertEqual(light['emission'], 'energy')
        self.assertGreater(light['rgb'][2], light['rgb'][0])
        self.assertAlmostEqual(light['intensity'], .5*.35)
        self.assertEqual(light['evidence'][0]['bones'], [0])

    def test_brightness_opacity_and_emitting_area_change_energy(self):
        def power(assets):
            return inspect_model(assets, 'world\\anonymous.m2')['emitters'][0]['intensity']
        full = power(fixture())
        self.assertLess(power(fixture(brightness=128)), full*.3)
        self.assertAlmostEqual(power(fixture(opacity=16384))/full, 16384/32767)
        self.assertAlmostEqual(power(fixture(centers=(0., .1)))/full, 2.)

    def test_compact_candles_conserve_energy_and_separated_sources_survive(self):
        def source(x, z=2.):
            return {'position': (x,0.,z), 'flux': (1.,.5,.1), 'emission': 'candle',
                    'area': .5, 'position_basis': 'actual candle flame'}
        one, = consolidate([source(0), source(.5), source(1.)])
        self.assertEqual(one['position'], (.5,0.,2.))
        self.assertAlmostEqual(one['raw_intensity'], 1.05)
        for actual,expected in zip(one['relative_flux'],(3.,1.5,.3)):
            self.assertAlmostEqual(actual,expected)
        self.assertAlmostEqual(one['intensity'], calibrated_intensity(1.05))
        separate = consolidate([source(0),source(7)])
        self.assertEqual([e['position'][0] for e in separate], [0,7])
        self.assertAlmostEqual(sum(e['intensity'] for e in separate), .7)
        measured = inspect_model(fixture(centers=(0.,7.)), 'world\\anonymous.m2')
        self.assertEqual(len(measured['emitters']), 2)

    def test_disabled_dark_modulate_and_animated_variants_fail_closed(self):
        for name in ('torch_off.m2', 'lanternoff01.m2', 'brokenlamp.m2', 'candle_unlit.m2'):
            assets = fixture(); assets.files['world\\'+name] = assets.files['world\\anonymous.m2']
            self.assertEqual(inspect_model(assets, 'world\\'+name)['status'], 'not_emissive', name)
        for assets in (fixture(brightness=0), fixture(opacity=0), fixture(blend=4)):
            self.assertEqual(inspect_model(assets, 'world\\anonymous.m2')['status'], 'not_emissive')
        result = inspect_model(fixture(animated=True), 'world\\anonymous.m2')
        self.assertEqual(result['status'], 'unsupported')
        self.assertIn('bone_billboard', str(result['refusals']))

    def test_native_records_preserved_and_missing_reported(self):
        assets = fixture(); original = assets.read('world\\anonymous.m2')
        native = append_lights(original, [{'position': (0,0,2), 'rgb': (1,.5,.1), 'intensity': 1, 'start': 1, 'end': 5}])
        assets.files['world\\anonymous.m2'] = native
        result = inspect_model(assets, 'world\\anonymous.m2')
        self.assertEqual((result['status'], result['native_records']), ('native',1))
        self.assertEqual(assets.read('world\\anonymous.m2'), native)
        self.assertEqual(inspect_model(assets, 'world\\missing.m2')['status'], 'missing')

    def test_particle_population_area_and_enabled_state_are_real_evidence(self):
        low = inspect_model(particle_fixture(), 'world\\anonymous.m2')
        high = inspect_model(particle_fixture(rate=4), 'world\\anonymous.m2')
        light, = low['emitters']
        self.assertEqual(light['position'], (0.,0.,3.))
        self.assertAlmostEqual(light['evidence'][0]['area'],2.)
        self.assertAlmostEqual(light['intensity'],.7)
        self.assertAlmostEqual(high['emitters'][0]['raw_intensity'],1.4)
        self.assertAlmostEqual(high['emitters'][0]['intensity'],calibrated_intensity(1.4))
        self.assertEqual(inspect_model(particle_fixture(enabled=0),'world\\anonymous.m2')['status'],'not_emissive')

    def test_soft_knee_is_monotonic_continuous_and_preserves_weak_response(self):
        for value in (0.,1e-12,.01,.175,.7,1.):
            self.assertEqual(calibrated_intensity(value),value)
        values = [calibrated_intensity(i/100) for i in range(10001)]
        self.assertEqual(values,sorted(values))
        self.assertTrue(all(0 <= value <= 4 for value in values))
        self.assertAlmostEqual(calibrated_intensity(1+1e-8),1+1e-8,places=14)
        self.assertGreater(calibrated_intensity(51.60051776361073),3.99)
        self.assertEqual(calibrated_intensity(1e300),4.)
        for bad in (float('nan'),float('inf'),-1.):
            with self.assertRaises(ValueError):calibrated_intensity(bad)
        source = {'position':(0.,0.,2.),'flux':(200.,100.,10.),'emission':'fire','position_basis':'real additive particle'}
        emitter, = consolidate([source])
        self.assertEqual(emitter['raw_intensity'],70.)
        self.assertEqual(emitter['relative_flux'],source['flux'])
        self.assertTrue(0 < emitter['intensity'] <= 4)

    def test_metadata_region_policy_keeps_real_separated_sources_and_native_limit(self):
        sources = [{'position':(float(i%8)*3,float(i//8)*3,2.),'flux':(.01,.005,.001),
                    'emission':'energy','position_basis':f'actual star {i}'} for i in range(63)]
        with self.assertRaisesRegex(ValueError,'16 independent'):
            consolidate(sources)
        emitters = consolidate(sources,max_regions=64)
        self.assertEqual(len(emitters),63)
        self.assertEqual({tuple(e['position']) for e in emitters},{s['position'] for s in sources})
        self.assertAlmostEqual(sum(e['raw_intensity'] for e in emitters),63*.0035)
        with self.assertRaises(ValueError):
            append_lights(fixture().read('world\\anonymous.m2'),emitters)
        for bad in (0,65,True):
            with self.assertRaises(ValueError):consolidate(sources,max_regions=bad)

    def test_large_anchor_requires_explicit_policy_and_actual_static_particle_evidence(self):
        assets = particle_fixture(); blob = bytearray(assets.read('world\\anonymous.m2'))
        offset = struct.unpack_from('<I',blob,300)[0]
        struct.pack_into('<3f',blob,offset+8,670.5,-235.,266.)
        assets.files['world\\anonymous.m2'] = bytes(blob)
        self.assertEqual(inspect_model(assets,'world\\anonymous.m2')['status'],'unsupported')
        result = inspect_model(assets,'world\\anonymous.m2',max_regions=64,position_limit=1024)
        self.assertEqual(result['status'],'addable')
        emitter, = result['emitters']
        self.assertEqual(emitter['position'],(670.5,-235.,266.))
        self.assertEqual(emitter['evidence'][0]['local_position'],(670.5,-235.,266.))
        self.assertEqual(emitter['evidence'][0]['bones'],[0])
        for value in (1025.,float('nan'),float('inf')):
            broken = bytearray(blob); struct.pack_into('<f',broken,offset+8,value)
            assets.files['world\\anonymous.m2'] = bytes(broken)
            self.assertEqual(inspect_model(assets,'world\\anonymous.m2',max_regions=64,position_limit=1024)['status'],'unsupported')
        for bad in (float('nan'),float('inf'),1025.,0.,True):
            with self.assertRaises(ValueError):consolidate([],position_limit=bad)
        for bad in (float('nan'),float('inf'),-1.):
            with self.assertRaises(ValueError):
                consolidate([{'position':(0,0,0),'flux':(bad,1.,1.),'emission':'fire','position_basis':'bad flux'}])

    def test_census_denominator_is_every_world_model(self):
        assets = fixture(); assets.files['world\\dark.m2'] = fixture(opacity=0).read('world\\anonymous.m2')
        assets.files['world\\dark00.skin'] = assets.read('world\\anonymous00.skin')
        assets.providers = dict.fromkeys(assets.files)
        result = census(assets, catalog={'world\\missing.m2': {}}, with_placements=False)
        self.assertEqual(result['counts']['model_denominator'], 3)
        self.assertEqual(sum(result['counts'][s] for s in ('native','addable','not_emissive','unsupported','missing')), 3)
        self.assertEqual(result['counts']['missing'], 1)
        self.assertEqual(result['counts']['not_emissive'], 1)

    def test_map_attribution_uses_dbc_identity_and_includes_unlisted_instance_groups(self):
        strings = b'\0Azeroth\0CustomCrystalRaid\0'
        rows = []
        for identity, offset, kind in ((0,1,0), (777,9,2)):
            row = [0]*66; row[0:3] = identity,offset,kind; row[59] = 530; row[63] = 2
            rows.append(struct.pack('<66I', *row))
        assets = Assets({'dbfilesclient\\map.dbc': struct.pack('<4s4I',b'WDBC',2,66,264,len(strings))+b''.join(rows)+strings})
        continent, raid = map_records(assets)
        self.assertEqual(continent['continent'], 'Eastern Kingdoms')
        self.assertEqual((raid['id'], raid['directory'], raid['instance_type']), (777,'customcrystalraid',2))
        self.assertIsNone(raid['continent'])
        self.assertEqual(raid['corpse_map_id'],530)

    def test_default_builder_stages_unnamed_model_and_pins_measured_energy(self):
        from build_emitter_patch import build
        from outdoor_light_profiles import load
        with tempfile.TemporaryDirectory(dir=output_dir()) as folder:
            folder = Path(folder); catalog = folder/'empty.json'
            catalog.write_text(json.dumps({'version':1,'profiles':{}}))
            result = build(fixture(), folder/'staged', catalog)
            self.assertEqual(result['counts']['patched_models'],1)
            self.assertEqual(result['coverage']['world\\anonymous.m2']['status'],'addable')
            model = load(folder/'staged/outdoor-light-profiles.json')['world\\anonymous.m2']
            self.assertAlmostEqual(model['emitters'][0]['intensity'],.175)
            self.assertIn(model['injected_sha256'],model['variants'])


if __name__ == '__main__':
    unittest.main()
