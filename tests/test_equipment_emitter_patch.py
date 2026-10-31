#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Native animated-bone light append and strict source qualification."""
from pathlib import Path
import struct
import sys
import unittest
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
from equipment_emitter_patch import append_bone_lights
from build_equipment_emitters import sources, item_visuals, lifetime_colour, validate_lights


class EquipmentPatch(unittest.TestCase):
    def model(self):
        model = bytearray(512)
        model[:4] = b'MD20'
        struct.pack_into('<I', model, 4, 264)
        struct.pack_into('<II', model, 44, 1, 304)
        # Original bone has the animated transform flag. The append must keep
        # this bone, not replace it by a static identity at model origin.
        struct.pack_into('<iIh', model, 304, -1, 0x200, -1)
        return bytes(model)

    def emitter(self, **values):
        return {'bone': 0, 'position': (1., -2., 3.), 'rgb': (.2, .4, 1.), 'intensity': .35, 'start': .5, 'end': 8., **values}

    def test_original_animation_and_exact_light_records(self):
        original = self.model()
        patched = append_bone_lights(original, [self.emitter()])
        validate_lights(original, patched, [self.emitter()])
        preserved = bytearray(patched[:len(original)])
        for start, end in ((20, 28), (264, 272)):
            preserved[start:end] = original[start:end]
        self.assertEqual(bytes(preserved), original)
        self.assertEqual(struct.unpack_from('<II', patched, 44), (1, 304))
        count, offset = struct.unpack_from('<II', patched, 264)
        self.assertEqual((count, *struct.unpack_from('<HH3f', patched, offset)), (1, 1, 0, 1., -2., 3.))
        for relative, fmt, wanted in ((56, '<3f', (.2, .4, 1.)), (76, '<f', (.35,)), (96, '<f', (.5,)), (116, '<f', (8.,)), (136, '<B', (1,))):
            interpolation, sequence, nt, ot, nk, ok = struct.unpack_from('<hh4I', patched, offset+relative)
            self.assertEqual((interpolation, sequence, nt, nk), (0, 0, 1, 1))
            n, key = struct.unpack_from('<II', patched, ok)
            self.assertEqual(n, 1)
            for actual, expected in zip(struct.unpack_from(fmt, patched, key), wanted):
                self.assertAlmostEqual(actual, expected, places=6)
        with self.assertRaisesRegex(ValueError, 'Preserve existing'):
            append_bone_lights(patched, [self.emitter()])
        bad = bytearray(patched)
        struct.pack_into('<H', bad, offset+2, 1)
        with self.assertRaisesRegex(ValueError, 'anchor mismatch'):
            validate_lights(original, bytes(bad), [self.emitter()])

    def test_invalid_inputs_refuse(self):
        for values in ({'bone': 1}, {'bone': -1}, {'rgb': (0, 0, 0)}, {'rgb': (float('nan'), 0, 1)}, {'position': (101, 0, 0)}, {'intensity': 5}, {'end': 32}):
            with self.assertRaises(ValueError):
                append_bone_lights(self.model(), [self.emitter(**values)])
        for n in (0, 17):
            with self.assertRaises(ValueError):
                append_bone_lights(self.model(), [self.emitter()] * n)
        with self.assertRaises(ValueError):
            append_bone_lights(self.model()[:303], [self.emitter()])

    def test_sources_never_invent_white_lights(self):
        class NoAssets:
            def read(self, path):
                raise FileNotFoundError(path)
        with self.assertRaisesRegex(ValueError, 'No proven'):
            sources(NoAssets(), self.model())
        # Original native records must not be replaced, even if other source
        # parsing would fail later.
        model = bytearray(self.model())
        struct.pack_into('<I', model, 264, 1)
        with self.assertRaisesRegex(ValueError, 'Existing native'):
            sources(NoAssets(), bytes(model))

    def test_item_visual_reference_schema(self):
        strings = b'\0Spells\\enchant.mdx\0'
        blob = struct.pack('<4s4I', b'WDBC', 1, 2, 8, len(strings)) + struct.pack('<II', 7, 1) + strings
        class Assets:
            def read(self, path):
                return blob
        self.assertEqual(item_visuals(Assets()), {'spells\\enchant.m2'})

    def test_lifetime_uses_time_intervals_and_independent_alpha_curve(self):
        rgb, opacity = lifetime_colour([(0, (255,0,0)), (3277, (0,0,255)), (32767, (0,0,255))], [(0,(32767,))])
        self.assertAlmostEqual(rgb[0], .05, places=4)
        self.assertAlmostEqual(rgb[2], .95, places=4)
        self.assertEqual(opacity, 1)
        rgb, opacity = lifetime_colour([(0, (0,255,0))], [(0,(0,)), (16384,(32767,)), (32767,(0,))])
        self.assertEqual(rgb, (0,1,0))
        self.assertAlmostEqual(opacity, .5)
        with self.assertRaises(ValueError):
            lifetime_colour([(0,(1,0,0))], [(0,(0,))])

    def test_actual_texture_and_particle_blend_not_material_enum(self):
        blob = bytearray(self.model())
        particle = len(blob)
        blob.extend(bytes(476))
        def add(data):
            offset = len(blob)
            blob.extend(data)
            return offset
        struct.pack_into('<II', blob, 296, 1, particle)
        struct.pack_into('<3f', blob, particle+8, 1, 2, 3)
        struct.pack_into('<BBh', blob, particle+40, 4, 1, -1)
        name = b'particles\\own-colour.blp\0'
        name_offset = add(name)
        texture_offset = add(struct.pack('<4I', 0, 0, len(name), name_offset))
        struct.pack_into('<II', blob, 80, 1, texture_offset)
        timestamp, rate = add(struct.pack('<I',0)), add(struct.pack('<f',10))
        ot, ok = add(struct.pack('<II',1,timestamp)), add(struct.pack('<II',1,rate))
        struct.pack_into('<hh4I', blob, particle+176, 0, -1, 1, ot, 1, ok)
        times, color, alpha = add(struct.pack('<H',0)), add(struct.pack('<3f',255,255,255)), add(struct.pack('<h',32767))
        struct.pack_into('<4I', blob, particle+260, 1, times, 1, color)
        struct.pack_into('<4I', blob, particle+276, 1, times, 1, alpha)
        blp = bytearray(148+4)
        blp[:4] = b'BLP2'
        struct.pack_into('<4BII', blp, 8, 3, 8, 0, 0, 1, 1)
        struct.pack_into('<I', blp, 20, 148)
        struct.pack_into('<I', blp, 84, 4)
        blp[148:] = bytes((255,0,0,255)) # Native BGRA blue, not a name-based colour.
        class ActualTexture:
            def read(self, name):
                return bytes(blp)
        emitters = sources(ActualTexture(), bytes(blob))
        self.assertEqual(emitters[0]['rgb'], (0,0,1))
        self.assertEqual(emitters[0]['position'], (1,2,3))
        self.assertEqual(emitters[0]['bone'], 0)
        self.assertAlmostEqual(emitters[0]['intensity'], .35)
        # Native 12340 accepts NoAlphaAdd(3) too; ordinary alpha blend(2)
        # must never be treated as additive emission.
        no_alpha = bytearray(blob)
        struct.pack_into('<B', no_alpha, particle+40, 3)
        self.assertEqual(sources(ActualTexture(), bytes(no_alpha))[0]['rgb'], (0,0,1))
        half = bytearray(blob)
        struct.pack_into('<h', half, alpha, 16384)
        self.assertAlmostEqual(sources(ActualTexture(), bytes(half))[0]['intensity'], .35*16384/32767)
        struct.pack_into('<B', half, particle+40, 3)
        self.assertAlmostEqual(sources(ActualTexture(), bytes(half))[0]['intensity'], .35)
        local = bytearray(blob)
        struct.pack_into('<I', local, particle+4, 0x19)
        following = sources(ActualTexture(), bytes(local))
        self.assertEqual(following[0]['rgb'], (0,0,1))
        self.assertEqual(following[0]['position'], (1,2,3))
        self.assertEqual(following[0]['bone'], 0)
        self.assertEqual(following[0]['particle_flags'], [0x19])
        # Newly supported local particles must not exclude a whole model
        # merely because it has more than two independent source anchors.
        many = bytearray(blob)
        array = len(many)
        for index in range(3):
            record = bytearray(blob[particle:particle+476])
            struct.pack_into('<3f', record, 8, 1+index, 2, 3)
            many.extend(record)
        struct.pack_into('<II', many, 296, 3, array)
        self.assertEqual([e['particles'] for e in sources(ActualTexture(), many)], [[0], [1], [2]])
        with self.assertRaisesRegex(ValueError, 'none silently omitted'):
            sources(ActualTexture(), many, max_sources=2)
        self.assertEqual(len(sources(ActualTexture(), many, max_sources=None)), 3)
        for field, value, fmt in ((40,2,'<B'), (42,11,'<h'), (4,0x8000,'<I')):
            bad = bytearray(blob)
            struct.pack_into(fmt, bad, particle+field, value)
            with self.assertRaisesRegex(ValueError, 'No proven'):
                sources(ActualTexture(), bytes(bad))


if __name__ == '__main__':
    unittest.main()
