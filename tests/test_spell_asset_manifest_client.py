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
from build_spell_asset_manifest import build, fnv1a64, runtime_reference


class ClientManifestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets, cls.stock = Assets(), Assets(archives='stock')
        cls.expected = json.loads((fp.FIXTURES / 'cold_blood_assets.json').read_text(encoding='utf-8'))
        cls.actual = build(cls.assets, cls.stock)
        cls.stock_report = build(cls.stock,cls.stock)

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
        textures = {t['path']: next(m for m in t['mips'] if max(m['width'],m['height'])<=64) for t in self.actual['textures']}
        for model in self.actual['models']:
            for particle in model['particles']:
                # Native authored middle key, independently multiplied by the
                # actual decoded texture RGB*alpha moments for SRCALPHA/ONE.
                stamp, colour = particle['rgb_lifetime_0_255'][1]
                alpha_stamp, (alpha,) = particle['alpha_lifetime_0_32767'][1]
                self.assertEqual(stamp, alpha_stamp)
                self.assertEqual(stamp, 16384)
                moments = textures[particle['texture']]['rgb_alpha_mean']
                rgb = [colour[k]/255 * alpha/32767 * moments[k] for k in range(3)]
                self.assertGreater(rgb[2], max(rgb[0], rgb[1]))
                self.assertGreater(rgb[2], 0)
                self.assertEqual(particle['alpha_lifetime_0_32767'][-1][1][0] * rgb[2], 0)

    def test_raw_upload_hashes_and_native_codec(self):
        expected = {t['path']: t for t in self.expected['textures']}
        native_results={}
        with tempfile.TemporaryDirectory(prefix='northlight-cold-blood-') as temp:
            for optimize in ('O0', 'O2'):
                decoder = Path(temp) / (optimize + '.exe')
                subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-' + optimize, '-I' + str(fp.SRC / 'replay'),
                                str(fp.TESTS / 'cold_blood_texture_probe.cpp'), '-o', str(decoder)], check=True, capture_output=True, env=fp.zig_env())
                for view,report,assets in (('stock',self.stock_report,self.stock),('active',self.actual,self.assets)):
                    for texture in report['textures']:
                        frozen = expected[texture['path']]
                        if view=='stock':self.assertEqual(texture['sha256'],frozen['sha256'])
                        self.assertLessEqual(len(texture['mips']),max(texture['width'],texture['height']).bit_length())
                        selected = next(m for m in texture['mips'] if max(m['width'], m['height']) <= 64)
                        blob = assets.read(texture['path']);level = selected['level']
                        offset = struct.unpack_from('<I', blob, 20+level*4)[0]
                        raw = blob[offset:offset+selected['bytes']]
                        self.assertEqual(hashlib.sha256(raw).hexdigest(), selected['raw_sha256'])
                        self.assertEqual(fnv1a64(raw), selected['raw_fnv1a64'])
                        actual = runtime_reference(raw, selected['width'], selected['height'], selected['pitch'], texture['native_format'], decoder)
                        if texture['sha256']==frozen['sha256']:
                            reference=frozen['runtime_reference']
                            self.assertEqual(selected['level'],reference['level'])
                            self.assertEqual(actual,{k:v for k,v in reference.items() if k!='level'})
                        else:
                            for key in ('rgb_mean','rgb_alpha_mean'):
                                for a,b in zip(actual[key],selected[key]):self.assertAlmostEqual(a,b,delta=1/255)
                            self.assertAlmostEqual(actual['alpha_mean'],selected['alpha_mean'],delta=1/255)
                        key=(view,texture['path'])
                        if optimize=='O0':native_results[key]=actual
                        else:self.assertEqual(actual,native_results[key])
        print('PASS stock frozen hashes and custom native BC decodes at O0/O2:',len(native_results),'textures')

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
