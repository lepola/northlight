#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Mixed and dedicated moon materials in the same art-layer build."""
from pathlib import Path
from tempfile import TemporaryDirectory
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_art_layer as art
import build_outdoor_single_moon as moon
from build_lighting import DBC, Skip
from build_outdoor_single_sun import sky_name


def table(rows, strings=b'\0'):
    size = len(rows[0])
    data = b''.join(struct.pack('<' + 'I' * size, *row) for row in rows)
    return struct.pack('<4s4I', b'WDBC', len(rows), size, 4 * size, len(strings)) + data + strings


def skin(batches):
    data = bytearray(48)
    data[:4] = b'SKIN'
    struct.pack_into('<2I', data, 36, len(batches), 48)
    for count, start in batches:
        record = [0] * 13
        record[8:10] = [count, start]
        data.extend(struct.pack('<2B11H', *record))
    return bytes(data)


class Assets:
    def __init__(self, mixed, ordinary):
        model = bytearray(256)
        model[:4] = b'MD20'
        struct.pack_into('<I', model, 68, 2)
        struct.pack_into('<II', model, 80, 2, 160)
        struct.pack_into('<II', model, 128, 2, 192)
        struct.pack_into('<2H', model, 192, 0, 1)
        for i, name in enumerate((b'neksys_stars.blp\0', b'arathi_moon.blp\0')):
            struct.pack_into('<4I', model, 160+i*16, 0, 0, len(name), len(model))
            model.extend(name)
        self.providers = {}
        for stem in (mixed, ordinary):
            self.providers[stem + '.m2'] = bytes(model)
            self.providers[stem + '00.skin'] = skin([(1, 0), (1, 1)])
            self.providers[stem + '01.skin'] = skin([(2, 0)] if stem == mixed else [(1, 0), (1, 1)])

    def read(self, name):
        return self.providers[name]


class Preservation(unittest.TestCase):
    def test_mixed_model_kept_whole_and_another_sky_cleaned(self):
        mixed = r'Environments\Stars\Fiord'
        ordinary = r'Environments\Stars\Dedicated'
        assets = Assets(mixed, ordinary)
        strings = b'\0' + (mixed + '.mdx').encode() + b'\0'
        db = art.DBC_NAME.format
        source = {db('Light'): table([[1, 571, 0, 0, 0, 0, 0, 1, 2] + [0]*6]),
                  db('LightParams'): table([[1, 0, 1], [2, 0, 2]]),
                  db('LightSkybox'): table([[1, 1, 0], [2, len(strings), 0]],
                                           strings + (ordinary + '.mdx').encode() + b'\0')}
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            art.write_archive(root/'in.mpq', source)
            with self.assertRaisesRegex(ValueError, 'experimental-preserve'):
                moon.build(root/'in.mpq', root/'default', assets)
            kept = []
            moon.build(root/'in.mpq', root/'opt', assets, kept)
            out = art.read_archive(root/'opt/patch-z.mpq')
            skies = DBC(out[db('LightSkybox')])
            self.assertEqual(kept[0]['model'], mixed + '.mdx')
            self.assertEqual(kept[0]['mixed_batches'][0]['textures'], ['neksys_stars.blp', 'arathi_moon.blp'])
            self.assertEqual(sky_name(skies, skies.index[1]), mixed + '.mdx')
            self.assertFalse(any(mixed.lower() in k.lower() for k in out))
            clone = sky_name(skies, skies.index[2])[:-4]
            self.assertNotEqual(clone, ordinary)
            for lod in ('00.skin', '01.skin'):
                self.assertEqual(struct.unpack_from('<I', out[clone + lod], 36)[0], 1)
                self.assertEqual(out[clone + lod][48:72], assets.read(ordinary + lod)[48:72])

            source[db('Light')] = table([[1, 571, 0, 0, 0, 0, 0, 1] + [0]*7])
            art.write_archive(root/'mixed.mpq', source)
            kept = []
            with self.assertRaises(Skip):
                moon.build(root/'mixed.mpq', root/'only-mixed', assets, kept)
            self.assertEqual([item['model'] for item in kept], [mixed + '.mdx'])


if __name__ == '__main__':
    unittest.main()
