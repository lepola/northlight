#!/usr/bin/env python3
"""CPU checks for the shipped Vibrant 32^3 table pack; uses the C++ renderer baker, no client or GPU."""
import sys
import struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import unittest

import northlight_paths as fp
fp.use_source_modules()
import build_appearance_lut_pack as pack_builder


class AppearanceLutPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = fp.REPO / 'client-config/appearance-presets/Vibrant.ini'
        cls.source_bytes = cls.source.read_bytes()
        cls.pack = pack_builder.build_pack(cls.source)
        cls.report = pack_builder.validate_pack(cls.pack, source_ini=cls.source_bytes, verify_tables=True)

    def test_actual_bake_has_consistent_source_index_and_table_hashes(self):
        self.assertGreater(self.report['entry_count'], 0)
        self.assertEqual(self.report['math_schema_version'], 1)
        self.assertEqual(self.report['cube_size'], 32)
        self.assertEqual(self.report['voxel_format'], 'RGBA16_UNORM_LE')
        self.assertEqual(self.report['uncompressed_table_bytes'], self.report['entry_count'] * 32 ** 3 * 8)
        self.assertEqual(self.report['source_ini_sha256'], pack_builder.hashlib.sha256(self.source_bytes).hexdigest())
        self.assertTrue(all(length == pack_builder.VOLUME_BYTES
                            for _, _, _, length, _ in self.report['entries']))
        self.assertTrue(all(pack_builder.stable_hash(key) == key_hash
                            for key_hash, key, _, _, _ in self.report['entries']))

    def test_signed_selective_colour_key_fields_are_preserved(self):
        signed = [value for _, key, _, _, _ in self.report['entries']
                  for offset in (44, 54, 64) for value in struct.unpack_from('<5h', key, offset)]
        self.assertTrue(any(value < 0 for value in signed))

    def test_rebuild_is_byte_identical(self):
        self.assertEqual(pack_builder.build_pack(self.source), self.pack)

    def test_lazy_index_check_and_touched_table_digest(self):
        damaged = bytearray(self.pack)
        _, _, _, _, _, _, _, count, _, tables_offset, *_ = pack_builder.PACK_HEADER.unpack_from(damaged)
        self.assertGreater(count, 0)
        damaged[tables_offset] ^= 1
        # Index-only loading is cheap and succeeds; the specific table read rejects its damaged digest.
        pack_builder.validate_pack(damaged, source_ini=self.source_bytes, verify_tables=False)
        with self.assertRaisesRegex(ValueError, 'table digest mismatch'):
            pack_builder.validate_pack(damaged, source_ini=self.source_bytes, verify_tables=True)


if __name__ == '__main__':
    unittest.main()
