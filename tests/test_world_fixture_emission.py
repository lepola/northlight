#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""World flame means preserve raw ownership and reject unproven motion/clocks."""
from pathlib import Path
import collections
import hashlib
import math
import struct
import sys
import unittest
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
from test_world_emitter_coverage import particle_fixture
from world_fixture_emission import native_fixture_offsets
from world_emitter_coverage import flame_height_centroid
from build_equipment_emitters import lifetime_track
from world_lights_builder import extract_m2
import northlight_paths as fp


def track(blob,at,fmt,values,times=(0,),mode=1,clock=-1):
    def add(data):
        offset=len(blob);blob.extend(data);return offset
    stamps=add(struct.pack('<%dI'%len(times),*times));keys=add(b''.join(struct.pack(fmt,*v) for v in values))
    ts=add(struct.pack('<II',len(times),stamps));ks=add(struct.pack('<II',len(values),keys))
    struct.pack_into('<hh4I',blob,at,mode,clock,1,ts,1,ks)


def rotation(degrees):
    q=(0.,math.sin(math.radians(degrees)/2),0.,math.cos(math.radians(degrees)/2))
    return tuple(round(v*32767)+(-32767 if v>=0 else 32767) for v in q)


def fixture(degrees=(0,),times=(0,)):
    assets=particle_fixture();blob=bytearray(assets.files['world\\anonymous.m2']);p=struct.unpack_from('<I',blob,300)[0]
    bone=struct.unpack_from('<I',blob,48)[0];struct.pack_into('<I',blob,bone+4,0x200);struct.pack_into('<3f',blob,bone+76,0,0,3)
    struct.pack_into('<B',blob,p+41,1);track(blob,p+52,'<f',[(2.,)])
    track(blob,bone+36,'<4h',[rotation(d) for d in degrees],times)
    sequence=len(blob);blob.extend(bytes(64));struct.pack_into('<I',blob,sequence+4,1000);struct.pack_into('<I',blob,sequence+12,0x20)
    struct.pack_into('<II',blob,28,1,sequence)
    light=len(blob);blob.extend(bytes(156));struct.pack_into('<Hh3f',blob,light,1,0,0,0,3);struct.pack_into('<II',blob,264,1,light)
    source=((0.,0.,3.),(1.,.5,.2),1.,12.,0,2,2)
    return assets,blob,p,bone,sequence,[source]


class WorldFixtureEmission(unittest.TestCase):
    def test_local_physics_is_rotated_and_does_not_bypass_fixed_axis_wrapper(self):
        assets,blob,p,bone,_,sources=fixture((90,));before=bytes(blob)
        record=native_fixture_offsets(assets,'world\\renamed.m2',blob,sources)[0]
        # Uniform radiance, speed2, lifetime1 gives mean travel1 along rotated +Z.
        for actual,wanted in zip(record['offset'],(1,0,0)):self.assertAlmostEqual(actual,wanted,places=4)
        self.assertEqual(record['anchor'],sources[0][0]);self.assertEqual(blob,before)
        self.assertIsNone(flame_height_centroid(blob,p,0,0,1,1.,lifetime_track(blob,p+260,'<3f'),
            lifetime_track(blob,p+276,'<h'),lifetime_track(blob,p+292,'<2f')))
        self.assertFalse(native_fixture_offsets(assets,'item\\renamed.m2',blob,sources))

    def test_wobble_uses_authored_time_and_has_an_independent_geometric_envelope(self):
        assets,blob,_,_,_,sources=fixture((-10,10,-10),(0,500,1000))
        record=native_fixture_offsets(assets,'world\\renamed.m2',blob,sources)[0]
        self.assertAlmostEqual(record['offset'][0],0,places=6)
        self.assertGreater(record['offset'][2],.99);self.assertLess(record['offset'][2],1)
        # Every possible angle on this short arc lies within +/-10 degrees.
        self.assertAlmostEqual(record['centroid_low'][0],-math.sin(math.radians(10)),places=4)
        self.assertAlmostEqual(record['centroid_high'][0],math.sin(math.radians(10)),places=4)
        self.assertAlmostEqual(record['centroid_low'][2],3+math.cos(math.radians(10)),places=4)
        self.assertAlmostEqual(record['centroid_high'][2],4,places=4)
        self.assertLess(record['motion_radius'],.18)

    def test_motion_clocks_nonrigid_roots_and_ambiguous_ownership_are_rejected(self):
        for problem in ('moving','off-pivot','billboard','scale','external','multiple-clock','nonlinear','orbit','ambiguous'):
            with self.subTest(problem=problem):
                assets,blob,p,bone,seq,sources=fixture((-10,10,-10),(0,500,1000))
                if problem=='moving':track(blob,bone+16,'<3f',[(0,0,0),(1,0,0)],(0,1000))
                if problem=='off-pivot':struct.pack_into('<3f',blob,bone+76,0,0,2)
                if problem=='billboard':struct.pack_into('<I',blob,bone+4,0x208)
                if problem=='scale':track(blob,bone+56,'<3f',[(2,2,2)])
                if problem=='external':struct.pack_into('<I',blob,seq+12,0)
                if problem=='multiple-clock':struct.pack_into('<II',blob,20,1,len(blob));blob.extend(struct.pack('<I',1000))
                if problem=='nonlinear':struct.pack_into('<h',blob,bone+36,2)
                if problem=='orbit':track(blob,bone+36,'<4h',[rotation(-80),rotation(80),rotation(-80)],(0,500,1000))
                if problem=='ambiguous':sources*=2
                self.assertFalse(native_fixture_offsets(assets,'world\\renamed.m2',blob,sources))

    @unittest.skipUnless(fp.client_root(required=False),'Client assets are optional')
    def test_actual_orc_flames_have_bounded_means_without_asset_or_energy_changes(self):
        from world_scene_builder import Assets
        expected={
            r'world\generic\orc\passive doodads\lampposts\orcbrazierstreetlamp.m2':('42cd1873932fe319e612fb40e3aee6ec8b50a419ab50442f9203b8b798587fce',(.003057478,.013775270,.239024202),.057),
            r'world\generic\orc\passive doodads\lanterns\orchanginglantern.m2':('03d7d36350fa65074abda66a680a30f1d12657ea7a68cd85ffafa2138ba62481',(.002220191,.006122953,.178786724),.072)}
        assets=Assets()
        try:
            checked=0
            for path,(digest,offset,radius) in expected.items():
                blob=assets.read(path)
                if hashlib.sha256(blob).hexdigest()!=digest:continue # Other mod packs have independent conservative qualification.
                sources=extract_m2(blob,collections.Counter());original=list(sources)
                record=native_fixture_offsets(assets,path,blob,sources)[0]
                for a,b in zip(record['offset'],offset):self.assertAlmostEqual(a,b,places=6)
                self.assertLess(record['motion_radius'],radius);self.assertEqual(sources,original)
                self.assertEqual(record,native_fixture_offsets(assets,'world\\anonymous.m2',blob,sources)[0])
                self.assertEqual(hashlib.sha256(blob).hexdigest(),digest);checked+=1
            if not checked:self.skipTest('Known Orc fixture revisions are not installed')
        finally:assets.close()


if __name__=='__main__':unittest.main()
