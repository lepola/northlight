#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=client,stormlib,zig
"""Read actual Cold Blood MPQ assets; verify native offline codec at O0/O2."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from build_spell_asset_manifest import build, fnv1a64, runtime_reference, texture_manifest


class ClientManifestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets, cls.stock = Assets(), Assets(archives='stock')
        cls.expected = json.loads((fp.FIXTURES / 'cold_blood_assets.json').read_text(encoding='utf-8'))
        cls.actual = build(cls.assets, cls.stock)

    @classmethod
    def tearDownClass(cls):
        cls.assets.close()
        cls.stock.close()

    def test_actual_dbc_graph_and_original_models(self):
        self.assertEqual(self.actual['spell'], self.expected['spell'])
        self.assertEqual({(g['stage'], g['kit_id'], g['effect_id'], g['attachment_id']) for g in self.actual['spell']['graph']},
                         {('cast', 3770, 126, 22), ('state', 3771, 121, 22)})
        for actual, expected in zip(self.actual['models'], self.expected['models']):
            self.assertEqual(actual['stock']['sha256'], expected['stock']['sha256'])
            self.assertTrue(actual['stock_prefix_preserved_except_global_sequence_and_light_headers'])
            self.assertEqual(actual['vertex_count'], 20)
            self.assertEqual(len(actual['particles']), 4)
            self.assertEqual(len(actual['bones']), 10)
            self.assertEqual(actual['ribbon_count'], 0)
            self.assertEqual([s['id'] for s in actual['sequences']], [0])  # No aura decay159.
            for particle in actual['particles']:
                self.assertTrue(particle['qualified'])
                self.assertEqual(particle['blend'], 4)
                self.assertLess(particle['bone'], len(actual['bones']))
                r, g, b = particle['asset_average_diagnostic']['normalized_linear_rgb']
                self.assertGreater(b, max(r, g))
                self.assertEqual(particle['alpha_lifetime_0_32767'][-1][1], (0,))

    def test_real_midlife_colour_times_uploaded_texture_is_blue(self):
        textures = {t['path']: t for t in self.expected['textures']}
        for model in self.actual['models']:
            for particle in model['particles']:
                # Native authored middle key, independently multiplied by the
                # actual decoded texture RGB*alpha moments for SRCALPHA/ONE.
                stamp, colour = particle['rgb_lifetime_0_255'][1]
                alpha_stamp, (alpha,) = particle['alpha_lifetime_0_32767'][1]
                self.assertEqual(stamp, alpha_stamp)
                self.assertEqual(stamp, 16384)
                moments = textures[particle['texture']]['runtime_reference']['rgb_alpha_mean']
                rgb = [colour[k]/255 * alpha/32767 * moments[k] for k in range(3)]
                self.assertGreater(rgb[2], max(rgb[0], rgb[1]))
                self.assertGreater(rgb[2], 0)
                self.assertEqual(particle['alpha_lifetime_0_32767'][-1][1][0] * rgb[2], 0)

    def test_raw_upload_hashes_and_native_codec(self):
        expected = {t['path']: t for t in self.expected['textures']}
        cases = []
        for texture in self.actual['textures']:
            path = texture['path']
            frozen = expected[path]
            if texture['sha256'] != frozen['sha256']:
                # HD archives can replace the effect. Exercise the replacement as well as the pinned original.
                cases.append((self.assets, texture, None))
                original = texture_manifest(self.stock, path)
                self.assertEqual(original['sha256'], frozen['sha256'], path)
                cases.append((self.stock, original, frozen['runtime_reference']))
            else:
                cases.append((self.assets, texture, frozen['runtime_reference']))
        decoded = {}
        with tempfile.TemporaryDirectory(prefix='northlight-cold-blood-') as temp:
            for optimize in ('O0', 'O2'):
                decoder = Path(temp) / (optimize + '.exe')
                subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-' + optimize, '-I' + str(fp.SRC / 'replay'),
                                str(fp.TESTS / 'cold_blood_texture_probe.cpp'), '-o', str(decoder)], check=True, capture_output=True, env=fp.zig_env())
                for archives, texture, reference in cases:
                    selected = next(m for m in texture['mips'] if max(m['width'], m['height']) <= 64)
                    if reference is not None:
                        self.assertEqual(selected['level'], reference['level'])
                    blob = archives.read(texture['path'])
                    level = selected['level']
                    offset = struct.unpack_from('<I', blob, 20+level*4)[0]
                    size = struct.unpack_from('<I', blob, 84+level*4)[0]
                    raw = blob[offset:offset+size]
                    self.assertEqual(hashlib.sha256(raw).hexdigest(), selected['raw_sha256'])
                    self.assertEqual(fnv1a64(raw), selected['raw_fnv1a64'])
                    actual = runtime_reference(raw, selected['width'], selected['height'], selected['pitch'], texture['native_format'], decoder)
                    if reference is not None:
                        self.assertEqual(actual, {k: v for k, v in reference.items() if k != 'level'})
                    if optimize == 'O0':
                        decoded[texture['sha256']] = actual
                    else:
                        self.assertEqual(actual, decoded[texture['sha256']])

    def test_local_fixture_is_exact_mesh_data_and_explicitly_not_a_draw_capture(self):
        with tempfile.TemporaryDirectory(prefix='northlight-cold-blood-mesh-') as temp:
            local = Path(temp)
            report = build(self.assets, self.stock, local=local)
            for model in report['models']:
                fixture = json.loads((local / (Path(model['path']).name + '.fixture.json')).read_text(encoding='utf-8'))
                self.assertIn('not a captured native particle draw', fixture['kind'])
                self.assertEqual((fixture['vertex_stride'], len(fixture['vertices']), len(fixture['skin_triangle_indices'])), (48, 20, 30))
                raw = (local / (Path(model['path']).name + '.vertices.bin')).read_bytes()
                rebuilt = b''.join(struct.pack('<3f4B4B3f4f', *v) for v in fixture['vertices'])
                self.assertEqual(raw, rebuilt)
                self.assertTrue(all(i < 20 for i in fixture['skin_triangle_indices']))


if __name__ == '__main__':
    unittest.main()
