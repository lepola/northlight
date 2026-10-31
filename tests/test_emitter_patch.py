#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""WotLK light append: binary preservation, real parser, malformed inputs."""
import collections
from pathlib import Path
import struct
import sys
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'renderer')]
from m2_emitter_patch import append_light, append_lights, verify_preserved
from world_lights_builder import extract_m2
from outdoor_light_profiles import fallback
import hashlib


class LightPatch(unittest.TestCase):
    def model(self, globals=()):
        blob = bytearray(512)
        blob[:4] = b'MD20'
        struct.pack_into('<I', blob, 4, 264)
        struct.pack_into('<II', blob, 20, len(globals), 400 if globals else 0)
        for i, value in enumerate(globals):
            struct.pack_into('<I', blob, 400 + i * 4, value)
        return bytes(blob)

    def patch(self, blob, **values):
        return append_light(blob, **{'position': (1., -2., 3.), 'rgb': (.2, .5, 1.),
                                    'intensity': 1.15, 'start': 1., 'end': 14., **values})

    def test_round_trip_and_preservation(self):
        for globals in ((), (1000,), (1, 500, 6000)):
            original = self.model(globals)
            patched = self.patch(original)
            verify_preserved(original, patched)
            count, offset = struct.unpack_from('<II', patched, 20)
            self.assertEqual(count, len(globals) + 1)
            self.assertEqual(struct.unpack_from('<%dI' % count, patched, offset), (*globals, 1))
            lights = extract_m2(patched, collections.Counter())
            self.assertEqual(len(lights), 1)
            position, rgb, start, end, index, kind, flags = lights[0]
            self.assertEqual(position, (1., -2., 3.))
            for a, b in zip(rgb, (.23, .575, 1.15)):
                self.assertAlmostEqual(a, b, places=6)
            self.assertEqual((start, end, index, kind, flags), (1., 14., 0, 2, 2))
            light_offset = struct.unpack_from('<I', patched, 268)[0]
            for relative in (16, 36, 56, 76, 96, 116, 136):
                self.assertEqual(struct.unpack_from('<h', patched, light_offset + relative + 2)[0], len(globals))
            with self.assertRaises(ValueError):
                self.patch(patched)

    def test_bad_data(self):
        original = self.model()
        for blob in (original[:303], b'MD21' + original[4:], original[:4] + struct.pack('<I', 256) + original[8:]):
            with self.assertRaises(ValueError):
                self.patch(blob)
        for values in ({'position': (float('nan'), 0, 0)}, {'position': (101, 0, 0)},
                       {'rgb': (0, 0, 0)}, {'rgb': (-1, 0, 1)}, {'intensity': 5}, {'end': 0}):
            with self.assertRaises(ValueError):
                self.patch(original, **values)
        blob = bytearray(original)
        struct.pack_into('<II', blob, 20, 2, 508)
        with self.assertRaises(ValueError):
            self.patch(blob)

    def test_multiple_sources_share_valid_identity_bone(self):
        original=self.model()
        emitters=[dict(position=(float(i),0,2),rgb=(1,.5,.2),intensity=1.15,start=1,end=12) for i in range(3)]
        patched=append_lights(original,emitters)
        verify_preserved(original,patched,3)
        lights=extract_m2(patched,collections.Counter())
        self.assertEqual([p[0] for p in lights],[(float(i),0.,2.) for i in range(3)])
        for count in (0,17):
            with self.assertRaises(ValueError):append_lights(original,emitters[:1]*count)
        for bad in (float('nan'),float('inf')):
            with self.assertRaises(ValueError):append_lights(original,[{**emitters[0],'intensity':bad}])

    def test_preservation_guard(self):
        original = self.model()
        patched = bytearray(self.patch(original))
        patched[68] ^= 1
        with self.assertRaises(ValueError):
            verify_preserved(original, patched)

    def test_authored_identity_survives_native_append(self):
        blob=self.model();patched=self.patch(blob);sha=hashlib.sha256(patched).hexdigest()
        p={'asset_sha256':hashlib.sha256(blob).hexdigest(),'position':[1.,-2.,3.],
           'rgb':[.2,.5,1.],'intensity':1.15,'start':1.,'end':14.,'injected_sha256':sha,
           'variants':{sha:{'position':[1.,-2.,3.]}}}
        catalog={'world\\lamp.m2':p};native=extract_m2(patched,collections.Counter())
        self.assertEqual(fallback(catalog,'world\\lamp.m2',blob,[]),fallback(catalog,'world\\lamp.m2',patched,native))
        self.assertIs(fallback(catalog,'world\\lamp.m2',b'other native revision',native),native)


if __name__ == '__main__':
    unittest.main()
