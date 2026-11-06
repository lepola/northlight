#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Point-only coverage transport: independent alpha means and actual Dalaran rays."""
import hashlib
import collections
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'renderer')]
import northlight_paths as fp
import point_transmission_builder as transmission
from test_world_emitter_coverage import fixture
from dalaran_candle_audit import CITY, audit, segment_hits
from world_scene_builder import Assets

PATH = 'world\\anonymous.m2'


def physical(opacity=10485, flags=16, blend=2, animated=False):
    assets = fixture(opacity=opacity,animated=animated); blob = bytearray(assets.read(PATH))
    assets.stats = collections.Counter()
    offset = struct.unpack_from('<I',blob,116)[0]; struct.pack_into('<2H',blob,offset,flags,blend)
    texture_offset = struct.unpack_from('<I',blob,84)[0]; struct.pack_into('<I',blob,texture_offset+4,3)
    vertex_offset = struct.unpack_from('<I',blob,64)[0]
    for i,uv in enumerate(((0.,0.),(1.,0.),(0.,1.))):
        struct.pack_into('<2f',blob,vertex_offset+i*48+32,*uv)
    skin = bytearray(assets.read(PATH[:-3]+'00.skin'))
    unit_offset = struct.unpack_from('<I',skin,40)[0]; struct.pack_into('<H',skin,unit_offset+22,65535)
    assets.files[PATH] = bytes(blob); assets.files[PATH[:-3]+'00.skin'] = bytes(skin)
    texture = bytearray(assets.read('world\\textures\\blue.blp')[:148])
    struct.pack_into('<II',texture,12,8,8); struct.pack_into('<I',texture,84,8*8*4)
    texture.extend(bytes((255,127,63,255))*64)
    assets.files['world\\textures\\blue.blp'] = bytes(texture)
    return assets


def decode_fgs(blob):
    magic,version,nv,nt,nm = struct.unpack_from('<4s4I',blob)
    assert (magic,version) == (b'FGS2',2)
    offset = 20
    vertices = list(struct.iter_unpack('<8f',blob[offset:offset+nv*32])); offset += nv*32
    triangles = list(struct.iter_unpack('<4I',blob[offset:offset+nt*16])); offset += nt*16
    materials = []
    for _ in range(nm):
        r,g,b,w,h,cutoff,size = struct.unpack_from('<3fIIfI',blob,offset); offset += 28
        assert size == w*h*4 and cutoff == .5
        materials.append((w,h,blob[offset:offset+size])); offset += size
    assert offset == len(blob)
    assert all(max(t[:3]) < nv and t[3] < nm for t in triangles)
    return vertices,triangles,materials


def uv_at(point, vertices, triangle):
    a,b,c = [vertices[i] for i in triangle[:3]]
    u = tuple(b[k]-a[k] for k in range(3)); v = tuple(c[k]-a[k] for k in range(3)); q = tuple(point[k]-a[k] for k in range(3))
    dot = lambda x,y: sum(a*b for a,b in zip(x,y))
    uu,uv,vv,qu,qv = dot(u,u),dot(u,v),dot(v,v),dot(q,u),dot(q,v)
    denominator = uu*vv-uv*uv
    y,z = (vv*qu-uv*qv)/denominator,(uu*qv-uv*qu)/denominator
    return tuple((1-y-z)*a[k]+y*b[k]+z*c[k] for k in (6,7))


class Coverage(unittest.TestCase):
    def test_bayer_has_correct_mean_and_repeat_phase(self):
        self.assertEqual(sorted(v for row in transmission.BAYER for v in row),list(range(64)))
        source = b''.join(bytes((30,60,90,255 if x < 4 else 128)) for y in range(8) for x in range(8))
        width,height,rgba = transmission.coverage_texture(8,8,source,.32)
        expected = .32*(1+128/255)/2
        self.assertAlmostEqual(sum(rgba[3::4])/255/64,expected,delta=.02)
        self.assertEqual(rgba[:3],source[:3])
        self.assertEqual(set(rgba[3::4]),{0,255})
        sample = lambda u,v: rgba[(int((v%1)*height)*width+int((u%1)*width))*4+3]
        self.assertEqual(sample(.21,.37),sample(1.21,2.37))
        self.assertEqual((width,height),(8,8))

    def test_only_static_physical_alpha_missing_from_native_casters_qualifies(self):
        mesh = transmission.model(physical(),PATH)
        self.assertEqual(len(mesh['triangles']),1)
        self.assertTrue(0 < mesh['evidence'][0]['mean_coverage'] < .5)
        for flags,blend in ((17,2),(16,3),(16,4),(0,2)):
            self.assertIsNone(transmission.model(physical(flags=flags,blend=blend),PATH))
        with self.assertRaisesRegex(ValueError,'bone_billboard'):
            transmission.model(physical(animated=True),PATH)
        self.assertIsNone(transmission.model(physical(opacity=28000,flags=16|128),PATH))
        self.assertIsNone(transmission.model(physical(opacity=0),PATH))

    def test_unstable_opacity_and_texture_transform_fail_closed(self):
        assets = physical(); blob = bytearray(assets.read(PATH)); nt,off = struct.unpack_from('<II',blob,88)
        keys = struct.unpack_from('<I',blob,off+16)[0]
        count,key = struct.unpack_from('<II',blob,keys)
        key_data = len(blob); blob.extend(struct.pack('<2h',10000,20000))
        time_data = len(blob); blob.extend(struct.pack('<2I',0,100))
        times = struct.unpack_from('<I',blob,off+8)[0]
        struct.pack_into('<II',blob,keys,2,key_data); struct.pack_into('<II',blob,times,2,time_data)
        assets.files[PATH] = bytes(blob)
        with self.assertRaisesRegex(ValueError,'animated_track'):
            transmission.model(assets,PATH)
        assets = physical(); skin = bytearray(assets.read(PATH[:-3]+'00.skin'))
        unit = struct.unpack_from('<I',skin,40)[0]; struct.pack_into('<H',skin,unit+22,0)
        assets.files[PATH[:-3]+'00.skin'] = bytes(skin)
        with self.assertRaises(ValueError):
            transmission.model(assets,PATH)

    def test_shared_formats_identity_transform_budget_and_empty_output(self):
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            folder = Path(folder); cache = folder/'cache'; (cache/'Northrend').mkdir(parents=True)
            key = hashlib.sha256(('FGS3-v1:'+PATH).encode()).digest()
            record = (key,42,3,0.,0.,0.,100.,100.,100.,1.,0.,0.,0.,1.,0.,0.,0.,1.,10.,20.,30.)
            source = struct.pack('<4sII',b'FGS3',3,1)+transmission.INSTANCE.pack(*record)
            (cache/'Northrend/31_21.fg3').write_bytes(source)
            report = transmission.build(cache,folder/'lights',['Northrend'],physical())
            self.assertEqual((report['counts']['models'],report['counts']['placements']),(1,1))
            output = folder/'lights/point-transmission'
            data = (output/'Northrend/31_21.fg3').read_bytes()
            actual, = transmission.INSTANCE.iter_unpack(data[12:])
            self.assertEqual(actual[1:3],record[1:3]); self.assertEqual(actual[9:],record[9:])
            self.assertNotEqual(actual[0],key)
            vertices,triangles,materials = decode_fgs((output/'models'/(actual[0].hex()+'.fgs')).read_bytes())
            self.assertEqual(actual[3:9],(10.,20.,32.,11.,21.,32.))
            self.assertEqual((len(vertices),len(triangles)),(3,1))
            self.assertEqual((cache/'Northrend/31_21.fg3').read_bytes(),source)
            self.assertEqual(report['counts']['bytes'],sum(p.stat().st_size for p in output.rglob('*') if p.is_file()))
            with patch.object(transmission,'MAX_TOTAL_BYTES',1):
                with self.assertRaisesRegex(RuntimeError,'total output'):
                    transmission.build(cache,folder/'too-large',['Northrend'],physical())
            self.assertFalse((folder/'too-large/point-transmission').exists())
            empty = transmission.build(cache,folder/'empty',['Northrend'],physical(flags=17))
            self.assertEqual(empty['counts']['models'],0)
            self.assertFalse((folder/'empty/point-transmission').exists())


class ActualDalaran(unittest.TestCase):
    def test_real_table_mask_partially_occludes_all_72_actual_geometry_rays(self):
        client = fp.client_root(required=False)
        if client is None:
            self.skipTest('Requires configured 3.3.5 client with actual Dalaran assets')
        assets = Assets(client,without='y')
        try:
            if assets.provider(CITY) is None:
                self.skipTest('Configured client lacks Dalaran WMO prerequisite')
            geometry = audit(assets)
            self.assertEqual(geometry['counts']['rays'],72)
            table = geometry['cases'][0]['table']; mesh = transmission.model(assets,table)
            self.assertEqual((len(mesh['vertices']),len(mesh['triangles'])),(57,34))
            self.assertEqual(len(mesh['materials']),1)  # base blend2, no Mod2X overlay
            serialized = transmission.encode_fgs(mesh); vertices,triangles,materials = decode_fgs(serialized)
            width,height,rgba = materials[0]; samples = []; binary_hits = 0
            for case in geometry['cases']:
                for ray in case['under_table_rays']:
                    hits = segment_hits(case['source_table_local'],ray['target_table_local'],vertices,triangles)
                    self.assertTrue(hits)
                    hit = hits[0]; u,v = uv_at(hit['point'],vertices,triangles[hit['triangle']])
                    x,y = int((u%1)*width),int((v%1)*height)
                    binary_hits += rgba[(y*width+x)*4+3] == 255
                    x,y = x//8*8,y//8*8
                    coverage = sum(rgba[((y+dy)*width+x+dx)*4+3] for dy in range(8) for dx in range(8))/255/64
                    self.assertTrue(0 < coverage < .4)
                    samples.append(coverage)
            self.assertEqual(len(samples),72)
            self.assertTrue(0 < binary_hits < 72)
            expected = mesh['evidence'][0]['expected_mean_coverage']; actual = mesh['evidence'][0]['mean_coverage']
            self.assertAlmostEqual(expected,actual,delta=.02)
            proof = mesh['evidence'][0]
            self.assertAlmostEqual(proof['used_uv_mean_coverage'],proof['expected_used_uv_coverage'],delta=.02)
            result = {'rays':72,'binary_blocked_rays':binary_hits,'phase_averaged_coverage':sum(samples)/72,
                      'mask_expected_mean':expected,'mask_actual_mean':actual,'mesh_bytes':len(serialized),
                      'native_caster_rays':geometry['counts']['opaque_cache_blocked_rays'],'source':mesh['evidence']}
            (fp.output_dir()/'dalaran-point-transmission-proof.json').write_text(json.dumps(result,indent=2)+'\n')
            print('Dalaran partial transmission:',json.dumps(result))
        finally:
            assets.close()


if __name__ == '__main__':
    unittest.main()
